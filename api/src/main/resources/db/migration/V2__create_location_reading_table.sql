-- db/migration/V2__create_location_reading_table.sql

CREATE TABLE locations (
   location_id SERIAL PRIMARY KEY,
   latitude DOUBLE PRECISION NOT NULL,
   longitude DOUBLE PRECISION NOT NULL,
   timestamp TIMESTAMP NOT NULL
);
