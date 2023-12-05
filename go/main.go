package main

import (
	"context"
	"encoding/json"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgxpool"
)

type BatteryData struct {
	State           uint16    `json:"state"`
	ChgEnable       bool      `json:"chg_enable"`
	DisEnable       bool      `json:"dis_enable"`
	ConnectedCells  uint16    `json:"connected_cells"`
	CellVoltages    []float32 `json:"cell_voltages"`
	CellVoltageMax  float32   `json:"cell_voltage_max"`
	CellVoltageMin  float32   `json:"cell_voltage_min"`
	CellVoltageAvg  float32   `json:"cell_voltage_avg"`
	PackVoltage     float32   `json:"pack_voltage"`
	StackVoltage    float32   `json:"stack_voltage"`
	PackCurrent     float32   `json:"pack_current"`
	BatTemps        []float32 `json:"bat_temps"`
	BatTempMax      float32   `json:"bat_temp_max"`
	BatTempMin      float32   `json:"bat_temp_min"`
	BatTempAvg      float32   `json:"bat_temp_avg"`
	MosfetTemp      float32   `json:"mosfet_temp"`
	IcTemp          float32   `json:"ic_temp"`
	McuTemp         float32   `json:"mcu_temp"`
	IsFull          bool      `json:"full"`
	IsEmpty         bool      `json:"empty"`
	Soc             float32   `json:"soc"`
	BalancingStatus uint32    `json:"balancing_status"`
	NoIdleTimestamp time.Time `json:"no_idle_timestamp"`
	ErrorFlags      uint32    `json:"error_flags"`
	Timestamp       time.Time `json:"timestamp"`
}

type LocationData struct {
	Latitude  float64   `json:"latitude"`
	Longitude float64   `json:"longitude"`
	Timestamp time.Time `json:"timestamp"`
}

func getEnvVar(key string, defaultValue string) string {
	value, exists := os.LookupEnv(key)
	if !exists {
		return defaultValue
	}
	return value
}

const (
	logFilePath = "app.log"

	batteryTopic  = "/bicycle/battery-status"
	locationTopic = "/bicycle/gps-coordinates"

	batteryTable                    = "batteries"
	batteryReadingBatTempsTable     = "battery_reading_bat_temps"
	batteryReadingCellVoltagesTable = "battery_reading_cell_voltages"
	locationTable                   = "locations"
)

var (
	dbpool     *pgxpool.Pool
	logger     *log.Logger
	logFile, _ = os.OpenFile(logFilePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0666)
)

func initLogger() {
	logger = log.New(logFile, "", log.LstdFlags|log.Lshortfile)
}

func setupPostgres(connString string) *pgxpool.Pool {
	pool, err := pgxpool.New(context.Background(), connString)
	if err != nil {
		logger.Fatal(err)
	}

	return pool
}

func setupMQTTClient(brokerURL string) mqtt.Client {
	opts := mqtt.NewClientOptions().AddBroker(brokerURL)
	client := mqtt.NewClient(opts)
	if token := client.Connect(); token.Wait() && token.Error() != nil {
		logger.Fatal(token.Error())
	}

	tokenBattery := client.Subscribe(batteryTopic, 0, func(client mqtt.Client, msg mqtt.Message) {
		handleBatteryMessage(msg.Payload())
	})

	if tokenBattery.Wait() && tokenBattery.Error() != nil {
		logger.Fatal(tokenBattery.Error())
	}

	tokenLocation := client.Subscribe(locationTopic, 0, func(client mqtt.Client, msg mqtt.Message) {
		handleLocationMessage(msg.Payload())
	})

	if tokenLocation.Wait() && tokenLocation.Error() != nil {
		logger.Fatal(tokenLocation.Error())
	}

	return client
}

func handleBatteryMessage(payload []byte) {
	logger.Printf("JSON payload: %+v\n", string(payload))

	var batteryData BatteryData
	err := json.Unmarshal(payload, &batteryData)
	if err != nil {
		logger.Println("Error parsing JSON payload (battery):", err)
		return
	}
	logger.Printf("Parsed JSON payload (battery): %+v\n", batteryData)

	tx, err := dbpool.Begin(context.Background())
	if err != nil {
		logger.Println("Error beginning PostgreSQL transaction (battery):", err)
		return
	}
	defer tx.Rollback(context.Background())

	var batteryID int64
	err = tx.QueryRow(context.Background(), `
		INSERT INTO `+batteryTable+` (
			bat_temp_avg, bat_temp_max, bat_temp_min,
			cell_voltage_avg, cell_voltage_max, cell_voltage_min,
			chg_enable, connected_cells, dis_enable,
			ic_temp, is_empty, is_full,
			mcu_temp, mosfet_temp, pack_current,
			pack_voltage, soc, stack_voltage,
			state, balancing_status, error_flags,
			no_idle_timestamp, timestamp
		) VALUES (
			$1, $2, $3, $4, $5, $6, $7, $8, $9, $10,
			$11, $12, $13, $14, $15, $16, $17, $18, $19,
			$20, $21, $22, $23
		) RETURNING battery_id
	`, batteryData.BatTempAvg, batteryData.BatTempMax, batteryData.BatTempMin,
		batteryData.CellVoltageAvg, batteryData.CellVoltageMax, batteryData.CellVoltageMin,
		batteryData.ChgEnable, batteryData.ConnectedCells, batteryData.DisEnable,
		batteryData.IcTemp, batteryData.IsEmpty, batteryData.IsFull,
		batteryData.McuTemp, batteryData.MosfetTemp, batteryData.PackCurrent,
		batteryData.PackVoltage, batteryData.Soc, batteryData.StackVoltage,
		batteryData.State, batteryData.BalancingStatus, batteryData.ErrorFlags,
		batteryData.NoIdleTimestamp, batteryData.Timestamp,
	).Scan(&batteryID)
	if err != nil {
		logger.Println("Error inserting data into PostgreSQL (battery):", err)
		return
	}

	for i, temp := range batteryData.BatTemps {
		query := `
        INSERT INTO ` + batteryReadingBatTempsTable + ` (bat_temps, bat_temps_order, battery_reading_battery_id)
        VALUES ($1, $2, $3)
    `
		_, err := tx.Exec(context.Background(), query, temp, i+1, batteryID)
		if err != nil {
			logger.Printf("Error executing query (battery temps): %s\nQuery: %s\n", err, query)
			return
		}
	}

	for i, voltage := range batteryData.CellVoltages {
		query := `
        INSERT INTO ` + batteryReadingCellVoltagesTable + ` (cell_voltage, cell_voltages_order, battery_reading_battery_id)
        VALUES ($1, $2, $3)
    `
		_, err := tx.Exec(context.Background(), query, voltage, i+1, batteryID)
		if err != nil {
			logger.Printf("Error executing query (battery voltages): %s\nQuery: %s\n", err, query)
			return
		}
	}

	err = tx.Commit(context.Background())
	if err != nil {
		logger.Println("Error committing PostgreSQL transaction (battery):", err)
		return
	}
}

func handleLocationMessage(payload []byte) {
	logger.Printf("JSON payload: %+v\n", string(payload))
	var locationData LocationData
	err := json.Unmarshal(payload, &locationData)
	if err != nil {
		logger.Println("Error parsing JSON payload (location):", err)
		return
	}
	logger.Printf("Parsed JSON payload (battery): %+v\n", locationData)

	_, err = dbpool.Exec(context.Background(), `
		INSERT INTO `+locationTable+` (latitude, longitude, timestamp)
		VALUES ($1, $2, $3)
	`, locationData.Latitude, locationData.Longitude, locationData.Timestamp)

	if err != nil {
		logger.Println("Error inserting data into PostgreSQL (location):", err)
		return
	}
}

func main() {
	initLogger()
	defer logFile.Close()

	mqttBroker := getEnvVar("MQTT_BROKER", "tcp://localhost:1883")
	postgresConn := getEnvVar("POSTGRES_CONN", "host=localhost user=postgres password=postgres dbname=database sslmode=disable")

	dbpool = setupPostgres(postgresConn)
	defer dbpool.Close()

	client := setupMQTTClient(mqttBroker)
	defer client.Disconnect(250)

	log.Println("Press Ctrl+C to exit")

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	defer close(c)

	<-c
	log.Println("Exiting...")
}
