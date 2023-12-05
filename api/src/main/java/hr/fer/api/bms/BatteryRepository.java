package hr.fer.api.bms;


import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.rest.core.annotation.RepositoryRestResource;

@RepositoryRestResource(collectionResourceRel = "batteries", path = "batteries")
public interface BatteryRepository extends JpaRepository<BatteryReading, Long> {
}
