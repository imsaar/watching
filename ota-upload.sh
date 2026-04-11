#!/bin/bash
# OTA firmware upload via ElegantOTA HTTP endpoint
# Usage: ./ota-upload.sh <IP> [firmware.bin]

IP="${1:?Usage: $0 <IP> [firmware.bin]}"
BIN="${2:-.pio/build/esp32-c3-devkitm-1/firmware.bin}"

if [ ! -f "$BIN" ]; then
    echo "Building firmware..."
    pio run -e esp32-c3-devkitm-1 || exit 1
fi

echo "Starting OTA to $IP..."
curl -s "http://$IP/ota/start?mode=firmware" > /dev/null

echo "Uploading $(basename $BIN) ($(wc -c < "$BIN" | tr -d ' ') bytes)..."
curl -X POST "http://$IP/ota/upload" \
    -F "firmware=@$BIN" \
    --progress-bar \
    -o /dev/null -w "HTTP %{http_code} in %{time_total}s\n"

echo "Done — device will reboot automatically."
