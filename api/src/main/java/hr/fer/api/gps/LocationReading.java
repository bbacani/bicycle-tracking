package hr.fer.api.gps;

import jakarta.persistence.*;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.math.BigDecimal;
import java.time.Instant;

@Entity
@Table(name = "locations")
@AllArgsConstructor
@NoArgsConstructor
@Data
public class LocationReading {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long locationId;

    private BigDecimal latitude;

    private BigDecimal longitude;

    private Instant timestamp;
}
