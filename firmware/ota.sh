#!/bin/bash
# Testfirmware bauen, ELF-Datei ablegen und per WLAN aufspielen.
# Aufruf: firmware/ota.sh [IP], Voreinstellung 192.168.1.29 (ESP_venus)
# Die ELF-Datei braucht man, um einen Coredump zu entschluesseln. Sie liegt unter
# firmware/elf/<ersten 16 Stellen ihrer SHA-256>.elf, so wie die Firmware sie meldet.
set -euo pipefail
IP="${1:-192.168.1.29}"
DIR="$(cd "$(dirname "$0")" && pwd)"
PIO="$HOME/projects/tasmota/.venv/bin/pio"

touch "$DIR/src/main.cpp"     # Build-Zeit (BUILD) neu setzen
"$PIO" run -d "$DIR" -e s3 | tail -1
ELF="$DIR/.pio/build/s3/firmware.elf"
SHA="$(sha256sum "$ELF" | cut -c1-16)"
mkdir -p "$DIR/elf"
cp "$ELF" "$DIR/elf/$SHA.elf"
echo "ELF abgelegt: firmware/elf/$SHA.elf"

curl -s -m 120 -w "  HTTP %{http_code}, %{time_total} s\n" -F "fw=@$DIR/.pio/build/s3/firmware.bin" "http://$IP/update"
echo "warte auf den Neustart ..."
for i in $(seq 1 60); do
    sleep 2
    J="$(curl -s -m 3 "http://$IP/api/status" || true)"
    if [ -n "$J" ] && echo "$J" | python3 -c "import json,sys; d=json.load(sys.stdin); sys.exit(0 if d['sys']['up'] < 60 else 1)" 2>/dev/null; then
        echo "$J" | python3 -c "
import json,sys
y=json.load(sys.stdin)['sys']
print('laeuft:', y['build'], '|', y.get('part'), y.get('fwstate'), '| ELF', y.get('elf'), '| Neustartgrund', y['reset'])
print('ELF passt zur abgelegten Datei' if y.get('elf')=='$SHA' else 'ELF der Firmware: %s, abgelegt: $SHA' % y.get('elf'))"
        exit 0
    fi
done
echo "ESP_venus meldet sich nicht nach 120 s"
exit 1
