#!/usr/bin/env python3
# Stueckliste aus einer Platinendatei: gleiche Teile zusammengefasst, mit Bezeichnern und Bauform.
# Aufruf: python3 stueckliste.py esp_venus_klein_usbfrei.kicad_pcb  -> esp_venus_klein_usbfrei_stueckliste.md
import os
import sys

import pcbnew

PCB = sys.argv[1]
OUT = PCB.replace('.kicad_pcb', '_stueckliste.md')
board = pcbnew.LoadBoard(PCB)

# Footprint -> (Art, Bauform, Hinweis)
ART = {
    'ESP32-S3_N16R8_2xUSB-C': ('Modul', 'ESP32-S3-Board N16R8 mit 2 × USB-C', 'Stiftleisten 2 × 22 direkt eingelötet'),
    'CJMCU-752': ('Modul', 'SC16IS752 (CJMCU-752), Quarz 1,8432 MHz', 'Stiftleisten 1 × 10 und 2 × 8'),
    'ADuM1201_Modul': ('Modul', 'ADuM1201-Modul, 15 × 10 mm', 'Stiftleisten 2 × 4'),
    'RS485_MAX13487_klein': ('Modul', 'RS485-Modul MAX13487 „RS485 V2.05“, etwa 18 × 12,6 mm', 'Stiftleisten 1 × 5 und 1 × 3'),
    'TJA1050_Modul': ('Modul', 'TJA1050-Modul, 22 × 11 mm', 'Stiftleiste 1 × 4 und 2 Stifte'),
    'CMT2300A_10298-V1.0': ('Modul', 'CMT2300A, Modul 10298-V1.0', 'SMD aufgelötet, Antenne an der IPX-Buchse'),
    'W5500_USR-ES1': ('Modul', 'W5500-Modul USR-ES1 (W5500 Lite)', 'Stiftleisten 2 × 6'),
    'USB-C_Modul_12x13.8': ('Modul', 'USB-C-Modul 12 × 13,8 mm (V, D−, D+, G)', 'Stiftleiste 1 × 4'),
    'B0505S-1WR3': ('Bauteil', 'DC/DC-Wandler B0505S-1WR3, SIP-4', 'THT'),
    'SOT-23_Handsoldering': ('Bauteil', 'MOSFET, SOT-23', 'SMD'),
    'R_0805_2012Metric_Pad1.20x1.40mm_HandSolder': ('Bauteil', 'Widerstand 0805', 'SMD'),
    'C_0805_2012Metric_Pad1.18x1.45mm_HandSolder': ('Bauteil', 'Keramik-C 0805', 'SMD'),
    'D_SMA_SOD-123_kombiniert': ('Bauteil', 'Diode S1M (SMA) oder 1N4148W (SOD-123)', 'SMD'),
    'SOT-223-3_TabPin2': ('Bauteil', 'Spannungsregler LM1117-3.3, SOT-223', 'SMD, optional, nur mit JP1 auf 2-3'),
    'CP_EIA-3528-21_Kemet-B_Pad1.50x2.35mm_HandSolder': ('Bauteil', 'Tantal-C, Bauform B (3528)', 'SMD, optional, nur mit U13'),
    'Loetbruecke_3':('Lötbrücke', 'Lötbrücke, 3 Pads', 'kein Bauteil, mit Zinn schließen'),
    'RJ45_Amphenol_54602-x08_Horizontal': ('Stecker', 'RJ45-Buchse liegend, Bauform Amphenol 54602', 'THT'),
    'MountingHole_3.2mm_M3': ('Loch', 'Befestigungsloch M3', 'kein Bauteil'),
}


def art(fp):
    name = str(fp.GetFPID().GetLibItemName())
    if name.startswith('PinHeader_1x'):
        n = int(name.split('1x')[1][:2])
        return ('Stecker', f'Stiftleiste 1 × {n}, Raster 2,54 mm', 'THT')
    return ART.get(name, ('?', name, ''))


rows = {}
for fp in board.GetFootprints():
    a, form, note = art(fp)
    if a in ('Loch', 'Fläche'):
        continue
    val = fp.GetValue()
    ref = fp.GetReference()
    if a == 'Stecker' and ref.startswith('X'):
        val = ''
    if a == 'Modul' or form.startswith('DC/DC'):
        # gleiche Module zusammen, wofuer sie dienen steht beim Bezeichner
        if val and val != form.split(',')[0]:
            ref = f'{ref} ({val})'
        val = ''
    if val == 'Last, unbestueckt':
        val = 'unbestückt, bei Bedarf 250 Ω'
    key = (a, form, val, note)
    rows.setdefault(key, []).append(ref)


def refkey(r):
    r = r.split(' ')[0]
    head = r.rstrip('0123456789')
    return (head, int(r[len(head):]) if r[len(head):] else 0)


order = {'Modul': 0, 'Bauteil': 1, 'Stecker': 2, 'Lötbrücke': 3, '?': 4}
lines = [f'# Stückliste {os.path.basename(PCB)}', '',
         'Erzeugt aus der Platinendatei. Gleiche Teile mit gleichem Wert sind zusammengefasst.', '',
         '| Anzahl | Art | Teil | Wert | Bezeichner | Hinweis |', '|---|---|---|---|---|---|']
for (a, form, val, note), refs in sorted(rows.items(), key=lambda kv: (order.get(kv[0][0], 9), kv[0][1], kv[0][2])):
    refs = sorted(refs, key=refkey)
    lines.append(f'| {len(refs)} | {a} | {form} | {val} | {", ".join(refs)} | {note} |')
open(OUT, 'w').write('\n'.join(lines) + '\n')
print(OUT, len(rows), 'Zeilen')
