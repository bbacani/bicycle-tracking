#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
// #include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "mqtt_client.h"
#include "esp_modem_api.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"

#include "esp_sntp.h"

#include "sdkconfig.h"

#include "cJSON.h"
#include "driver/gpio.h"

#if defined(CONFIG_EXAMPLE_FLOW_CONTROL_NONE)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_NONE
#elif defined(CONFIG_EXAMPLE_FLOW_CONTROL_SW)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_SW
#elif defined(CONFIG_EXAMPLE_FLOW_CONTROL_HW)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_HW
#endif

static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int GOT_DATA_BIT = BIT2;

#ifdef CONFIG_EXAMPLE_MODEM_DEVICE_CUSTOM
esp_err_t esp_modem_get_time(esp_modem_dce_t *dce_wrap, char *p_time);
#endif

#define TOPIC_BATTERY "/bicycle/battery-status"
#define TOPIC_GPS "/bicycle/gps-coordinates"

// TODO Select appropriate numbers
#define BOARD_NUM_CELLS_MAX 6
#define BOARD_NUM_THERMISTORS_MAX 2

/**
 * Current BMS status including measurements and error flags
 */
typedef struct
{
    uint16_t state;  ///< Current state of the battery
    bool chg_enable; ///< Manual enable/disable setting for charging
    bool dis_enable; ///< Manual enable/disable setting for discharging

    uint16_t connected_cells; ///< \brief Actual number of cells connected (might
                              ///< be less than BOARD_NUM_CELLS_MAX)

    float cell_voltages[BOARD_NUM_CELLS_MAX]; ///< Single cell voltages (V)
    float cell_voltage_max;                   ///< Maximum cell voltage (V)
    float cell_voltage_min;                   ///< Minimum cell voltage (V)
    float cell_voltage_avg;                   ///< Average cell voltage (V)
    float pack_voltage;                       ///< Battery external pack voltage (V)
    float stack_voltage;                      ///< Battery internal stack voltage (V)

    float pack_current; ///< \brief Battery pack current, charging direction
                        ///< has positive sign (A)

    float bat_temps[BOARD_NUM_THERMISTORS_MAX]; ///< Battery temperatures (°C)
    float bat_temp_max;                         ///< Maximum battery temperature (°C)
    float bat_temp_min;                         ///< Minimum battery temperature (°C)
    float bat_temp_avg;                         ///< Average battery temperature (°C)
    float mosfet_temp;                          ///< MOSFET temperature (°C)
    float ic_temp;                              ///< Internal BMS IC temperature (°C)
    float mcu_temp;                             ///< MCU temperature (°C)

    bool full;  ///< CV charging to cell_chg_voltage finished
    bool empty; ///< Battery is discharged below cell_dis_voltage

    float soc; ///< Calculated State of Charge (%)

    uint32_t balancing_status; ///< holds on/off status of balancing switches
    time_t no_idle_timestamp;  ///< Stores last time of current > idle threshold

    uint32_t error_flags; ///< Bit array for different BmsErrorFlag errors

    time_t timestamp;
} BmsStatus;

/**
 * Current GPS location
 */
typedef struct
{
    float latitude;
    float longitude;
    time_t timestamp;
} Location;

static const char *TAG = "mqtt_tracker";

static esp_mqtt_client_handle_t client;

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org"); // You can add more servers if needed
    esp_sntp_init();
}

void set_timezone(void)
{
    setenv("TZ", "UTC0", 1); // Set the timezone as UTC
    tzset();                 // Apply the timezone setting
}

void obtain_time(void)
{
    initialize_sntp();
    set_timezone(); // Set the timezone before synchronizing time

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

// Function to generate random float within a given range
float generateRandomFloat(float min, float max)
{
    return ((float)rand() / RAND_MAX) * (max - min) + min;
}

// Function to generate random BmsStatus data
BmsStatus generateRandomBmsStatus()
{
    BmsStatus status;

    // Populate BmsStatus with random data
    status.state = rand() % 100;    // Adjust as needed
    status.chg_enable = rand() % 2; // 0 or 1
    status.dis_enable = rand() % 2; // 0 or 1

    status.connected_cells = rand() % (BOARD_NUM_CELLS_MAX + 1);

    for (int i = 0; i < BOARD_NUM_CELLS_MAX; ++i)
    {
        status.cell_voltages[i] = ((float)rand() / RAND_MAX) * 4.2;
    }

    status.cell_voltage_max = ((float)rand() / RAND_MAX) * 4.2;
    status.cell_voltage_min = ((float)rand() / RAND_MAX) * 4.2;
    status.cell_voltage_avg = ((float)rand() / RAND_MAX) * 4.2;
    status.pack_voltage = ((float)rand() / RAND_MAX) * 50.0;
    status.stack_voltage = ((float)rand() / RAND_MAX) * 50.0;
    status.pack_current = ((float)rand() / RAND_MAX) * 10.0;

    for (int i = 0; i < BOARD_NUM_THERMISTORS_MAX; ++i)
    {
        status.bat_temps[i] = ((float)rand() / RAND_MAX) * 100.0;
    }

    status.bat_temp_max = ((float)rand() / RAND_MAX) * 100.0;
    status.bat_temp_min = ((float)rand() / RAND_MAX) * 100.0;
    status.bat_temp_avg = ((float)rand() / RAND_MAX) * 100.0;
    status.mosfet_temp = ((float)rand() / RAND_MAX) * 100.0;
    status.ic_temp = ((float)rand() / RAND_MAX) * 100.0;
    status.mcu_temp = ((float)rand() / RAND_MAX) * 100.0;

    status.full = rand() % 2;
    status.empty = rand() % 2;

    status.soc = ((float)rand() / RAND_MAX) * 100.0;

    status.balancing_status = rand();

    status.no_idle_timestamp = time(NULL); // Use current time

    status.error_flags = rand();

    status.timestamp = time(NULL); // Use current time

    return status;
}

// Function to generate random Location data
Location generateRandomLocation()
{
    Location location;

    // Populate Location with random data
    location.latitude = generateRandomFloat(-90.0, 90.0);
    location.longitude = generateRandomFloat(-180.0, 180.0);
    location.timestamp = time(NULL); // Use current time

    return location;
}

char *convertBmsStatusToJSON(const BmsStatus *status)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "state", status->state);
    cJSON_AddBoolToObject(root, "chg_enable", status->chg_enable);
    cJSON_AddBoolToObject(root, "dis_enable", status->dis_enable);
    cJSON_AddNumberToObject(root, "connected_cells", status->connected_cells);

    // Add cell voltage array
    cJSON *cellVoltagesArray = cJSON_CreateArray();
    for (int i = 0; i < BOARD_NUM_CELLS_MAX; i++)
    {
        cJSON_AddItemToArray(cellVoltagesArray, cJSON_CreateNumber(status->cell_voltages[i]));
    }
    cJSON_AddItemToObject(root, "cell_voltages", cellVoltagesArray);

    cJSON_AddNumberToObject(root, "cell_voltage_max", status->cell_voltage_max);
    cJSON_AddNumberToObject(root, "cell_voltage_min", status->cell_voltage_min);
    cJSON_AddNumberToObject(root, "cell_voltage_avg", status->cell_voltage_avg);
    cJSON_AddNumberToObject(root, "pack_voltage", status->pack_voltage);
    cJSON_AddNumberToObject(root, "stack_voltage", status->stack_voltage);
    cJSON_AddNumberToObject(root, "pack_current", status->pack_current);

    // Add battery temperature array
    cJSON *batTempsArray = cJSON_CreateArray();
    for (int i = 0; i < BOARD_NUM_THERMISTORS_MAX; i++)
    {
        cJSON_AddItemToArray(batTempsArray, cJSON_CreateNumber(status->bat_temps[i]));
    }
    cJSON_AddItemToObject(root, "bat_temps", batTempsArray);

    cJSON_AddNumberToObject(root, "bat_temp_max", status->bat_temp_max);
    cJSON_AddNumberToObject(root, "bat_temp_min", status->bat_temp_min);
    cJSON_AddNumberToObject(root, "bat_temp_avg", status->bat_temp_avg);
    cJSON_AddNumberToObject(root, "mosfet_temp", status->mosfet_temp);
    cJSON_AddNumberToObject(root, "ic_temp", status->ic_temp);
    cJSON_AddNumberToObject(root, "mcu_temp", status->mcu_temp);

    cJSON_AddBoolToObject(root, "full", status->full);
    cJSON_AddBoolToObject(root, "empty", status->empty);
    cJSON_AddNumberToObject(root, "soc", status->soc);
    cJSON_AddNumberToObject(root, "balancing_status", status->balancing_status);

    // Format timestamp as RFC3339 string
    char timestampString[21]; // Adjust the size as needed
    strftime(timestampString, sizeof(timestampString), "%Y-%m-%dT%H:%M:%SZ", gmtime(&status->no_idle_timestamp));

    cJSON_AddStringToObject(root, "no_idle_timestamp", timestampString);

    cJSON_AddNumberToObject(root, "error_flags", status->error_flags);

    strftime(timestampString, sizeof(timestampString), "%Y-%m-%dT%H:%M:%SZ", gmtime(&status->timestamp));

    cJSON_AddStringToObject(root, "timestamp", timestampString);

    char *jsonString = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return jsonString;
}

// Function to convert Location to JSON string
char *convertLocationToJSON(const Location *location)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "latitude", location->latitude);
    cJSON_AddNumberToObject(root, "longitude", location->longitude);

    // Format timestamp as RFC3339 string
    char timestampString[21]; // Adjust the size as needed
    strftime(timestampString, sizeof(timestampString), "%Y-%m-%dT%H:%M:%SZ", gmtime(&location->timestamp));

    cJSON_AddStringToObject(root, "timestamp", timestampString);

    char *jsonString = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return jsonString;
}

// Function to publish BmsStatus to the specified topic
void publishBatteryStatus()
{
    BmsStatus status = generateRandomBmsStatus();
    char *jsonString = convertBmsStatusToJSON(&status);
    esp_mqtt_client_publish(client, TOPIC_BATTERY, jsonString, strlen(jsonString), 1, 0);
    free(jsonString); // Free the allocated memory for the JSON string
}

// Function to publish Location to the specified topic
void publishGPS()
{
    Location location = generateRandomLocation();
    char *jsonString = convertLocationToJSON(&location);
    esp_mqtt_client_publish(client, TOPIC_GPS, jsonString, strlen(jsonString), 1, 0);
    free(jsonString); // Free the allocated memory for the JSON string
}

// Task to periodically publish messages to the MQTT broker
void mqtt_publish_task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        // Publish BmsStatus to the battery topic
        publishBatteryStatus();

        // Publish Location to the GPS topic
        publishGPS();

        vTaskDelayUntil(&xLastWakeTime, CONFIG_MESSAGE_PERIOD * 1000 / portTICK_PERIOD_MS); // MESSAGE_PERIOD in seconds
    }
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
        break;
    default:
        break;
    }
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %" PRIu32, event_id);
    if (event_id == NETIF_PPP_ERRORUSER)
    {
        /* User interrupted event from esp-netif */
        esp_netif_t **p_netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", *p_netif);
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %" PRIu32, event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP)
    {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
    }
    else if (event_id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
    }
    else if (event_id == IP_EVENT_GOT_IP6)
    {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Waiting for MQTT data");
}

#define MODEM_RST 5
#define MODEM_PWKEY 4
#define MODEM_POWER_ON 23

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    /* Init and register system/core components */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1 << MODEM_RST) | (1 << MODEM_PWKEY) | (1 << MODEM_POWER_ON);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_set_level(MODEM_PWKEY, 0);
    gpio_set_level(MODEM_RST, 1);
    gpio_set_level(MODEM_POWER_ON, 1);

    vTaskDelay(15000 / portTICK_PERIOD_MS);

    // Synchronize time with NTP server
    // obtain_time();

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(example_connect());

    /* Configure the PPP netif */
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_EXAMPLE_MODEM_PPP_APN);
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif);

    event_group = xEventGroupCreate();

    /* Configure the DTE */
#if defined(CONFIG_EXAMPLE_SERIAL_CONFIG_UART)
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    /* setup UART specific configuration based on kconfig options */
    dte_config.uart_config.tx_io_num = CONFIG_EXAMPLE_MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = CONFIG_EXAMPLE_MODEM_UART_RX_PIN;
    dte_config.uart_config.rts_io_num = CONFIG_EXAMPLE_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = CONFIG_EXAMPLE_MODEM_UART_CTS_PIN;
    dte_config.uart_config.flow_control = EXAMPLE_FLOW_CONTROL;
    dte_config.uart_config.rx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE;
    dte_config.uart_config.tx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_TX_BUFFER_SIZE;
    dte_config.uart_config.event_queue_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_QUEUE_SIZE;
    dte_config.task_stack_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_STACK_SIZE;
    dte_config.task_priority = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_PRIORITY;
    dte_config.dte_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE / 2;

#if CONFIG_EXAMPLE_MODEM_DEVICE_BG96 == 1
    ESP_LOGI(TAG, "Initializing esp_modem for the BG96 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_BG96, &dte_config, &dce_config, esp_netif);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_SIM800 == 1
    ESP_LOGI(TAG, "Initializing esp_modem for the SIM800 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, esp_netif);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_SIM7000 == 1
    ESP_LOGI(TAG, "Initializing esp_modem for the SIM7000 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_SIM7070 == 1
    ESP_LOGI(TAG, "Initializing esp_modem for the SIM7070 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7070, &dte_config, &dce_config, esp_netif);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_SIM7600 == 1
    ESP_LOGI(TAG, "Initializing esp_modem for the SIM7600 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, esp_netif);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_CUSTOM == 1
    ESP_LOGI(TAG, "Initializing esp_modem with custom module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_CUSTOM, &dte_config, &dce_config, esp_netif);
#else
    ESP_LOGI(TAG, "Initializing esp_modem for a generic module...");
    esp_modem_dce_t *dce = esp_modem_new(&dte_config, &dce_config, esp_netif);
#endif
    assert(dce);
    if (dte_config.uart_config.flow_control == ESP_MODEM_FLOW_CONTROL_HW)
    {
        esp_err_t err = esp_modem_set_flow_control(dce, 2, 2); // 2/2 means HW Flow Control.
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set the set_flow_control mode");
            return;
        }
        ESP_LOGI(TAG, "HW set_flow_control OK");
    }

#else
#error Invalid serial connection to modem.
#endif

    xEventGroupClearBits(event_group, CONNECT_BIT | GOT_DATA_BIT);

    /* Run the modem demo app */
#if CONFIG_EXAMPLE_NEED_SIM_PIN == 1
    // check if PIN needed
    bool pin_ok = false;
    if (esp_modem_read_pin(dce, &pin_ok) == ESP_OK && pin_ok == false)
    {
        if (esp_modem_set_pin(dce, CONFIG_EXAMPLE_SIM_PIN) == ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            abort();
        }
    }
#endif

    int rssi, ber;
    esp_err_t err = esp_modem_get_signal_quality(dce, &rssi, &ber);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_modem_get_signal_quality failed with %d %s", err, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Signal quality: rssi=%d, ber=%d", rssi, ber);

#ifdef CONFIG_EXAMPLE_MODEM_DEVICE_CUSTOM
    {
        char time[64];
        err = esp_modem_get_time(dce, time);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_modem_get_time failed with %d %s", err, esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "esp_modem_get_time: %s", time);
    }
#endif

    err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_modem_set_mode(ESP_MODEM_MODE_DATA) failed with %d", err);
        return;
    }

    /* Wait for IP address */
    ESP_LOGI(TAG, "Waiting for IP address");
    xEventGroupWaitBits(event_group, CONNECT_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* Start MQTT client */
    ESP_LOGI(TAG, "Starting MQTT client");
    mqtt_app_start();

    // Create the MQTT publish task
    xTaskCreate(&mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 5, NULL);

    /* Wait for establishing connection */
    ESP_LOGI(TAG, "Waiting for establishing connection");
    xEventGroupWaitBits(event_group, GOT_DATA_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}
