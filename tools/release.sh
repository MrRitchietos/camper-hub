#!/bin/sh
# Build a release without secrets: tools/release.sh 1.2.0  (then publish release/firmware.bin on GitHub)
set -e
V="$1"; [ -n "$V" ] || { echo "usage: tools/release.sh 1.2.0"; exit 1; }
cd "$(dirname "$0")/.."
sed -i "s/-DFW_VERSION=.*/-DFW_VERSION=\\\\\"$V\\\\\"/" platformio.ini
PLATFORMIO_BUILD_FLAGS="-DRELEASE_BUILD" ${PIO:-pio} run -e cyd
if [ -f include/secrets.hpp ]; then              # never ship the home WiFi
  for k in HOME_SSID HOME_PASS; do
    s=$(sed -n "s/#define $k \"\(.*\)\"/\1/p" include/secrets.hpp)
    if [ -n "$s" ] && strings .pio/build/cyd/firmware.bin | grep -qF "$s"; then echo "$k found in the build, stop"; exit 1; fi
  done
fi
mkdir -p release && cp .pio/build/cyd/firmware.bin release/firmware.bin
echo "built release/firmware.bin for v$V"
