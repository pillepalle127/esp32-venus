#!/bin/sh
# Fertigungsdaten aus einer Platinendatei: Gerber (RS-274X) und Bohrdaten (Excellon), dazu ein ZIP zum Hochladen.
# Braucht kicad-cli (KiCad 7) und python3. Die Platine muss fertig geroutet sein (route.py), die Flaechen gefuellt.
#
# Aufruf, als normaler Benutzer im Ordner des Repos:
#   sh hardware/platine/fertigung.sh hardware/platine/esp_venus_klein_usbfrei.kicad_pcb
# Ergebnis: hardware/platine/fertigung/<Name>/ und hardware/platine/fertigung/<Name>.zip
set -e
PCB="$1"
[ -f "$PCB" ] || { echo "Platinendatei fehlt: $PCB"; exit 1; }
NAME=$(basename "$PCB" .kicad_pcb)
OUT="$(dirname "$PCB")/fertigung/$NAME"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT"
# Kupfer, Loetstopp, Bestueckungsdruck und Umriss. Druck ueber Pads wird abgezogen.
kicad-cli pcb export gerbers -o "$OUT/" --no-x2 --subtract-soldermask \
    -l F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts "$PCB"
# Bohrungen: durchkontaktiert (PTH) und nicht durchkontaktiert (NPTH) getrennt, in mm, dazu ein Bohrplan
kicad-cli pcb export drill -o "$OUT/" --format excellon --drill-origin absolute --excellon-units mm \
    --excellon-separate-th --generate-map --map-format pdf "$PCB"
python3 - "$OUT" <<'PY'
import os, sys, zipfile
out = sys.argv[1]
with zipfile.ZipFile(out + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(out)):
        if not f.endswith('.pdf'):
            z.write(os.path.join(out, f), f)
print(out + '.zip')
PY
ls -l "$OUT"
