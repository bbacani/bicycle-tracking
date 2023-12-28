package hr.fer.api.bms;

import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

import jakarta.persistence.*;
import java.time.Instant;
import java.util.List;

@Entity
@Table(name = "batteries")
@AllArgsConstructor
@NoArgsConstructor
@Data
public class BatteryReading {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long batteryId;

    private int state;
    private boolean chgEnable;
    private boolean disEnable;

    private int connectedCells;

    @ElementCollection
    @CollectionTable(name = "battery_reading_cell_voltages", joinColumns = @JoinColumn(name = "battery_reading_battery_id"))
    @OrderColumn(name = "cell_voltages_order")
    @Column(name = "cell_voltage")
    private List<Float> cellVoltages;

    private float cellVoltageMax;
    private float cellVoltageMin;
    private float cellVoltageAvg;
    private float packVoltage;
    private float stackVoltage;

    private float packCurrent;

    @ElementCollection
    @CollectionTable(name = "battery_reading_bat_temps", joinColumns = @JoinColumn(name = "battery_reading_battery_id"))
    @OrderColumn(name = "bat_temps_order")
    @Column(name = "bat_temps")
    private List<Float> batTemps;

    private float batTempMax;
    private float batTempMin;
    private float batTempAvg;
    private float mosfetTemp;
    private float icTemp;
    private float mcuTemp;

    private boolean is_full;
    private boolean is_empty;

    private float soc;

    private long balancingStatus;

    private Instant noIdleTimestamp;

    private long errorFlags;

    private Instant timestamp;
}
