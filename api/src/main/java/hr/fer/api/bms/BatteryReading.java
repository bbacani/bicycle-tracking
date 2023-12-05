package hr.fer.api.bms;

import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

import jakarta.persistence.*;
import java.time.Instant;

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
    @Column(name = "cell_voltage")
    private float[] cellVoltages;

    private float cellVoltageMax;
    private float cellVoltageMin;
    private float cellVoltageAvg;
    private float packVoltage;
    private float stackVoltage;

    private float packCurrent;

    @ElementCollection
    @Column(name = "bat_temps")
    private float[] batTemps;

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
