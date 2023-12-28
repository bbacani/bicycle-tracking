-- db/migration/V3__create_battery_reading_cell_voltages_table.sql

CREATE TABLE battery_reading_cell_voltages (
  cell_voltage FLOAT,
  cell_voltages_order INTEGER NOT NULL,
  battery_reading_battery_id BIGINT NOT NULL,
  PRIMARY KEY (cell_voltages_order, battery_reading_battery_id),
  FOREIGN KEY (battery_reading_battery_id) REFERENCES batteries (battery_id)
);
