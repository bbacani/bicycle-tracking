-- db/migration/V4__create_battery_reading_bat_temps_table.sql

CREATE TABLE battery_reading_bat_temps (
  bat_temps FLOAT,
  bat_temps_order INTEGER NOT NULL,
  battery_reading_battery_id BIGINT NOT NULL,
  PRIMARY KEY (bat_temps_order, battery_reading_battery_id),
  FOREIGN KEY (battery_reading_battery_id) REFERENCES batteries (battery_id)
);
