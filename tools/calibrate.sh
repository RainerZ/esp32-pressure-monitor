#!/bin/bash

# Test measurement

# IP address of the ESP32, written to the A2L file
TARGET_HOST="192.168.0.154"

# A2L file
A2LFILE="CANape/pressure_monitor.a2l"
if [ ! -f "$A2LFILE" ]; then
    echo "❌ FAILED: A2l file $ELFFILE not found"
    exit 1
fi
echo "A2L file $A2LFILE"


# --verbose=1 logs the ELF/DWARF sections and the compilation units to stdout
xcpclient --udp --dest-addr 192.168.0.154 --log-level=2 --a2l "$A2LFILE"   --cal parameters.sensor_voltage_point1 0.6
xcpclient --udp --dest-addr 192.168.0.154 --log-level=2 --a2l "$A2LFILE"   --cal parameters.pressure_point1 0.0
xcpclient --udp --dest-addr 192.168.0.154 --log-level=2 --a2l "$A2LFILE"   --cal parameters.sensor_voltage_point2 3.0
xcpclient --udp --dest-addr 192.168.0.154 --log-level=2 --a2l "$A2LFILE"   --cal parameters.pressure_point2 10.0


xcpclient --udp --dest-addr 192.168.0.154 --log-level=2 --a2l CANape/pressure_monitor.a2l   --list-cal .




