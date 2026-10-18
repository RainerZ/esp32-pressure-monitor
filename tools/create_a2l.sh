#!/bin/bash

# A2L file creator 
# Creates the A2L file from the ELF file built with PlatformIO
# Transport layer UDP, the given IP address and port 5555 are written to the A2L file
#
# Prerequisites:
# - The firmware has been built with PlatformIO (pio run)
# - The local machine must have xcpclient installed:
#     cd XCPlite
#     ./build.sh rust_tools cargo_install

# IP address of the ESP32, written to the A2L file
TARGET_HOST="192.168.0.154"

# ELF file built by PlatformIO
ELFFILE=".pio/build/lilygo-t-display-s3/firmware.elf"
if [ ! -f "$ELFFILE" ]; then
    echo "❌ FAILED: ELF file $ELFFILE not found, build the firmware with PlatformIO first"
    exit 1
fi
echo "ELF file $ELFFILE"

# A2L file
A2LFILE="CANape/pressure_monitor.a2l"
echo "A2L file $A2LFILE"

# LOG file
LOGFILE="CANape/pressure_monitor.log"
echo "LOG file $LOGFILE"


# Remove the A2L file of a previous run, so a failed generation can not leave a stale A2L file behind
rm -f "$A2LFILE"

# --verbose=1 logs the ELF/DWARF sections and the compilation units to stdout
xcpclient --offline --udp --dest-addr "$TARGET_HOST" --elf "$ELFFILE" --a2l "$A2LFILE" --embed-aml --elf-unit-filter pressure_monitor_cpp --default-event=fastTask --log-level=3 --verbose=1 > "$LOGFILE"
if [ $? -ne 0 ] || [ ! -f "$A2LFILE" ]; then
    echo "❌ FAILED: xcpclient could not create the A2L file $A2LFILE"
    exit 1
fi

echo "✅ SUCCESS: Created a new A2L file $A2LFILE"
