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
xcpclient --udp --dest-addr "$TARGET_HOST"  --a2l "$A2LFILE" --mea pressure
if [ $? -ne 0 ] || [ ! -f "$A2LFILE" ]; then
    echo "❌ FAILED: xcpclient failed"
    exit 1
fi


