#!/bin/bash
chown -R mosquitto:mosquitto /mosquitto
/usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf