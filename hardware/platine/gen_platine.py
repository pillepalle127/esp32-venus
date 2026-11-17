#!/usr/bin/env python3
# Erzeugt die Platine von ESP_venus als KiCad-Datei: Footprints, Netzliste und Platzierung
# aus einer Quelle. Leiterbahnen kommen danach mit route.sh.
#
# Aufruf: python3 gen_platine.py [Zieldatei.kicad_pcb]   (sonst esp_venus_klein_usbfrei.kicad_pcb)
# Braucht das Python-Modul pcbnew von KiCad 7 und die Footprint-Bibliothek von KiCad
# (Umgebungsvariable KICAD7_FOOTPRINT_DIR, sonst /usr/share/kicad/footprints).
#
# Alle Masse in mm, Ursprung links oben, x nach rechts, y nach unten, Blick von oben.
# Quellen der Masse: docs/platine.md.
import json
import os
import sys

import pcbnew
from pcbnew import FromMM, ToMM, VECTOR2I

HERE = os.path.dirname(os.path.abspath(__file__))
# Platine 100 x 100 mm (guenstigste Preisstufe bei JLCPCB), R und C als SMD in Bauform 0805,
# kleine RS485-Module, W5500, RJ45 fuer VE.Bus. Vor der COM-Buchse des S3-Boards bleibt Platz fuer Stecker und Kabel.
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
NAME = 'esp_venus_klein_usbfrei'
OUT = ARGS[0] if ARGS else os.path.join(HERE, NAME + '.kicad_pcb')
LIB = os.environ.get('KICAD7_FOOTPRINT_DIR', '/usr/share/kicad/footprints')

W, H = 100.0, 100.0
P = 2.54


def V(x, y):
    return VECTOR2I(FromMM(x), FromMM(y))


board = pcbnew.BOARD()

# ---------------------------------------------------------------- Footprints selbst gebaut

def new_fp(name, value):
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID('platine', name))
    fp.SetPosition(V(0, 0))
    fp.SetValue(value)
    fp.Value().SetLayer(pcbnew.F_Fab)
    fp.Value().SetTextSize(V(0.8, 0.8))
    fp.Value().SetTextThickness(FromMM(0.12))
    fp.Reference().SetTextSize(V(0.9, 0.9))
    fp.Reference().SetTextThickness(FromMM(0.14))
    return fp


def tht(fp, name, x, y, dia=1.7, drill=1.0, square=False):
    p = pcbnew.PAD(fp)
    p.SetNumber(name)
    p.SetShape(pcbnew.PAD_SHAPE_RECT if square else pcbnew.PAD_SHAPE_CIRCLE)
    p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    p.SetLayerSet(pcbnew.PAD.PTHMask())
    p.SetSize(V(dia, dia))
    p.SetDrillSize(V(drill, drill))
    p.SetPosition(V(x, y))
    p.SetPos0(V(x, y))
    fp.Add(p)


def npth(fp, x, y, drill):
    p = pcbnew.PAD(fp)
    p.SetNumber('')
    p.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
    p.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
    p.SetSize(V(drill, drill))
    p.SetDrillSize(V(drill, drill))
    p.SetPosition(V(x, y))
    p.SetPos0(V(x, y))
    fp.Add(p)


def smd(fp, name, x, y, w, h):
    p = pcbnew.PAD(fp)
    p.SetNumber(name)
    p.SetShape(pcbnew.PAD_SHAPE_RECT)
    p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
    p.SetLayerSet(pcbnew.PAD.SMDMask())
    p.SetSize(V(w, h))
    p.SetPosition(V(x, y))
    p.SetPos0(V(x, y))
    fp.Add(p)


def line(fp, x1, y1, x2, y2, layer=pcbnew.F_SilkS, width=0.15):
    s = pcbnew.FP_SHAPE(fp)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1))
    s.SetEnd(V(x2, y2))
    s.SetLayer(layer)
    s.SetWidth(FromMM(width))
    s.SetLocalCoord()
    fp.Add(s)


def rect(fp, x1, y1, x2, y2, layer=pcbnew.F_SilkS, width=0.15):
    line(fp, x1, y1, x2, y1, layer, width)
    line(fp, x2, y1, x2, y2, layer, width)
    line(fp, x2, y2, x1, y2, layer, width)
    line(fp, x1, y2, x1, y1, layer, width)


def outline(fp, x1, y1, x2, y2):
    rect(fp, x1, y1, x2, y2, pcbnew.F_SilkS)
    rect(fp, x1, y1, x2, y2, pcbnew.F_Fab, 0.1)
    rect(fp, x1 - 0.25, y1 - 0.25, x2 + 0.25, y2 + 0.25, pcbnew.F_CrtYd, 0.05)


def text(fp, s, x, y, size=0.8, layer=pcbnew.F_SilkS, angle=0):
    t = pcbnew.FP_TEXT(fp)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetTextSize(V(size, size))
    t.SetTextThickness(FromMM(size * 0.15))
    t.SetPosition(V(x, y))
    t.SetTextAngleDegrees(angle)
    t.SetKeepUpright(True)
    t.SetLocalCoord()
    fp.Add(t)


S3_LEFT = ['3V3', '3V3', 'RST', 'IO4', 'IO5', 'IO6', 'IO7', 'IO15', 'IO16', 'IO17', 'IO18', 'IO8', 'IO3',
           'IO46', 'IO9', 'IO10', 'IO11', 'IO12', 'IO13', 'IO14', '5V', 'GND']
S3_RIGHT = ['GND', 'IO43', 'IO44', 'IO1', 'IO2', 'IO42', 'IO41', 'IO40', 'IO39', 'IO38', 'IO37', 'IO36', 'IO35',
            'IO0', 'IO45', 'IO48', 'IO47', 'IO21', 'IO20', 'IO19', 'GND', 'GND']


def fp_s3():
    # Platine 27,94 x 57,15, Reihenabstand 25,40, Pin bis Querkante 1,905 (Masszeichnung des Haendlers)
    fp = new_fp('ESP32-S3_N16R8_2xUSB-C', 'ESP32-S3 N16R8')
    for i, n in enumerate(S3_LEFT):
        tht(fp, n, 0, i * P, square=(i == 0))
    for i, n in enumerate(S3_RIGHT):
        tht(fp, n, 25.4, i * P)
    outline(fp, -1.27, -1.905, 26.67, 55.245)
    # WROOM ragt 6,24 mm ueber die Antennenseite
    rect(fp, 3.7, -8.145, 21.7, -1.905, pcbnew.Dwgs_User, 0.1)
    text(fp, 'Antenne', 12.7, -5, 1.0, pcbnew.Dwgs_User)
    text(fp, 'ESP32-S3', 12.7, 20, 1.2)
    text(fp, '3V3', 3.2, 0, 0.8)
    text(fp, 'GND', 22.2, 0, 0.8)
    text(fp, '5V', 2.8, 20 * P, 0.8)
    text(fp, 'USB nativ', 6.2, 51.5, 0.8)
    text(fp, 'USB COM', 19.2, 51.5, 0.8)
    rect(fp, 1.7, 48.2, 10.7, 55.245, pcbnew.F_Fab, 0.1)
    rect(fp, 14.7, 48.2, 23.7, 55.245, pcbnew.F_Fab, 0.1)
    return fp


SC_SINGLE = ['VCC', 'GND', 'RESET', 'CS', 'SI', 'SO', 'IRQ', 'SPI', 'SCLK', 'VSS']
SC_INNER = ['GP7', 'GP6', 'GP5', 'GP4', 'GP3', 'GP2', 'GP1', 'GP0']
SC_OUTER = ['TXA', 'RXA', 'RTSA', 'CTSA', 'TXB', 'RXB', 'RTSB', 'CTSB']


def fp_sc16():
    # CJMCU-752 von oben: Einzelreihe links, Doppelreihe 8 und 9 Raster daneben, um 1 Raster versetzt
    fp = new_fp('CJMCU-752', 'SC16IS752')
    for i, n in enumerate(SC_SINGLE):
        tht(fp, n, 0, i * P, square=(i == 0))
    for i, n in enumerate(SC_INNER):
        tht(fp, n, 8 * P, (i + 1) * P)
    for i, n in enumerate(SC_OUTER):
        tht(fp, n, 9 * P, (i + 1) * P)
    outline(fp, -1.77, -2.17, 9 * P + 1.77, 9 * P + 2.17)
    text(fp, 'SC16IS752', 10.5, 11.4, 1.0)
    text(fp, 'VCC', 3.3, 0, 0.8)
    text(fp, 'TXA', 17.2, P, 0.8)
    return fp


def fp_adum(value):
    # 15 x 10 mm, Reihenabstand 12,70. Von oben, Bauteile oben: Seite 1 links, Seite 2 rechts
    fp = new_fp('ADuM1201_Modul', value)
    for i, n in enumerate(['U1', 'A0', 'B1', 'G1']):
        tht(fp, n, 0, i * P, square=(i == 0))
    for i, n in enumerate(['U2', 'A1', 'B0', 'G2']):
        tht(fp, n, 12.7, i * P)
    outline(fp, -1.15, -1.19, 13.85, 8.81)
    text(fp, 'U1', 2.4, 0, 0.8)
    text(fp, 'U2', 10.3, 0, 0.8)
    text(fp, 'ADuM', 6.35, 3.81, 0.9)
    return fp


def fp_tja():
    # 22 x 11 mm. TX bis CANH 19,8 mm (gemessen), CANH neben TX, CANL neben RX
    fp = new_fp('TJA1050_Modul', 'TJA1050')
    for i, n in enumerate(['VCC', 'TX', 'RX', 'GND']):
        tht(fp, n, 0, i * P, square=(i == 0))
    tht(fp, 'CANH', 19.8, P)
    tht(fp, 'CANL', 19.8, 2 * P)
    outline(fp, -1.1, -1.69, 20.9, 9.31)
    text(fp, 'VCC', 3.2, 0, 0.8)
    text(fp, 'TJA1050', 10, 3.81, 0.9)
    return fp


def fp_b0505s(value):
    # Mornsun B_S-1WR3: SIP-4, Raster 2,54, Gehaeuse 11,6 x 6,0, Bohrung 1,0
    fp = new_fp('B0505S-1WR3', value)
    for i, n in enumerate(['1', '2', '3', '4']):
        tht(fp, n, i * P, 0, dia=1.7, drill=1.0, square=(i == 0))
    outline(fp, -1.99, -4.95, 9.61, 1.05)
    text(fp, '1', -1.2, -3.6, 0.8)
    return fp


CMT_NAMES = {1: 'GND', 2: 'GND', 3: 'GND', 4: 'GND', 5: 'GPIO1', 6: 'GPIO2', 7: 'GPIO3', 8: 'VCC', 9: 'GND',
             10: 'GND', 11: 'SCLK', 12: 'SDIO', 13: 'CSB', 14: 'FCSB', 15: 'GND', 16: 'GND', 17: 'ANT', 18: 'GND'}


def cmt_pads():
    # Lage der Pads von oben, Ursprung Modulmitte. Modul 14 x 20, Raster 1,27.
    ys = [2.0 + 1.27 * k for k in range(6)] + [16.46, 17.73, 19.0]
    pos = {}
    for k, n in enumerate([10, 11, 12, 13, 14, 15, 16, 17, 18]):
        pos[n] = (-7.0, ys[k] - 10.0)
    for k, n in enumerate([9, 8, 7, 6, 5, 4, 3, 2, 1]):
        pos[n] = (7.0, ys[k] - 10.0)
    return pos


def fp_cmt():
    # Pad 17 (ANT) bekommt kein Pad: Die Antenne haengt an der IPX-Buchse des Moduls.
    fp = new_fp('CMT2300A_10298-V1.0', 'CMT2300A')
    for n, (x, y) in cmt_pads().items():
        if n == 17:
            continue
        # Pad 0,9 mm unter dem Modul, 1,0 mm davor zum Loeten von Hand
        smd(fp, str(n), x + (-0.05 if x < 0 else 0.05), y, 1.9, 0.8)
    outline(fp, -7.0, -10.0, 7.0, 10.0)
    text(fp, 'CMT2300A', 0, -0.4, 0.9)
    text(fp, '1', 5.2, 9.0, 0.8)
    text(fp, 'IPX', -3.6, 6.4, 0.8)
    return fp


def fp_usbc():
    """USB-C-Modul 12 x 13,8 mm, Stiftleiste V, D-, D+, G (Haendlerfoto vom 16.11.2026).
    Lage der Stifte aus dem Foto ausgemessen: Reihe 2,9 mm von der Kante gegenueber der Buchse, mittig.
    Unverdreht zeigt die Buchse nach unten (+y)."""
    fp = new_fp('USB-C_Modul_12x13.8', 'USB-C CH348')
    for i, n in enumerate(['1', '2', '3', '4']):
        tht(fp, n, i * P, 0, square=(i == 0))
    x0 = 1.5 * P - 6.0
    outline(fp, x0, -2.9, x0 + 12.0, 10.9)
    rect(fp, x0 + 1.5, 6.0, x0 + 10.5, 13.4, pcbnew.Dwgs_User, 0.1)
    text(fp, 'V', 0, 1.9, 0.8)
    text(fp, 'G', 3 * P, 1.9, 0.8)
    text(fp, 'USB-C', 1.5 * P, 4.5, 0.9)
    return fp


def fp_diode_sma_sod123():
    """Diode wahlweise im Gehaeuse SMA (S1M) oder SOD-123 (1N4148W). Ein Pad je Seite reicht vom
    inneren Rand des SOD-123-Pads bis ueber das SMA-Pad hinaus. Pad 1 ist die Kathode."""
    fp = new_fp('D_SMA_SOD-123_kombiniert', 'S1M oder 1N4148W')
    for n, sx in (('1', -1), ('2', 1)):
        smd(fp, n, sx * 2.4, 0, 2.4, 1.8)
    outline(fp, -3.8, -1.6, 3.8, 1.6)
    line(fp, -0.9, -1.6, -0.9, 1.6)
    text(fp, 'K', -2.4, -2.4, 0.7)
    return fp


def fp_rs485_klein(value):
    """Kleines RS485-Modul mit MAX13487 (RS485 V2.05), Ansicht von oben wie auf dem Haendlerfoto:
    Stiftleiste 1 x 5 links, von oben GND, TXD, RXD, VCC, EN (EN hat das eckige Pad).
    Stiftleiste 1 x 3 rechts, 6 Raster daneben, von oben B, A, GND (GND eckig).
    Auf beiden Fotos (oben und unten) liegt die 3er-Reihe 0,44 bis 0,65 mm zur EN-Seite versetzt
    gegenueber TXD, RXD, VCC. Deshalb hier 0,5 mm Versatz und Bohrung 1,2 mm statt 1,0 mm.
    Alles aus Fotos ausgemessen, Platine etwa 18 x 12,6 mm."""
    fp = new_fp('RS485_MAX13487_klein', value)
    for i, n in enumerate(['GND', 'TXD', 'RXD', 'VCC', 'EN']):
        tht(fp, n, 0, i * P, square=(n == 'EN'))
    for i, n in enumerate(['B', 'A', 'GND']):
        tht(fp, n, 6 * P, (i + 1) * P + 0.5, dia=1.8, drill=1.2, square=(n == 'GND'))
    for p_ in fp.Pads():
        if abs(ToMM(p_.GetPos0().x) - 6 * P) < 0.01:
            # A, B und GND liegen auf derselben Busseite, untereinander reichen 0,3 mm
            p_.SetLocalClearance(FromMM(0.3))
    outline(fp, -1.3, -1.6, 6 * P + 1.5, 4 * P + 1.4)
    text(fp, 'TXD', 2.1, P, 0.7)
    text(fp, 'A', 6 * P - 1.7, 2 * P + 0.5, 0.7)
    return fp


def fp_jumper3(value):
    """Loetbruecke mit 3 Pads, Abstand 1 mm, damit sie mit einem Tropfen Zinn zu schliessen ist und die
    Abstaende der Busseite (0,8 mm) einhaelt. Pad 2 in der Mitte."""
    fp = new_fp('Loetbruecke_3', value)
    for i, n in enumerate(['1', '2', '3']):
        smd(fp, n, (i - 1) * 2.0, 0, 1.0, 1.5)
    outline(fp, -2.75, -1.0, 2.75, 1.0)
    text(fp, '1', -2.0, -1.8, 0.7)
    return fp


def fp_w5500():
    """W5500-Modul USR-ES1, Ansicht von oben, RJ45 nach rechts. Laut Handbuch des Herstellers (Diagramm 3-1
    und 3-3): Reihen 20,32 mm auseinander, Platine 25,0 x 23,0 mm, Pin 1 liegt 6,40 mm von der Kante an der
    Buchse, Pin 6 liegt 5,80 mm von der anderen Kante, die Buchse steht 2,5 mm ueber.
    J1 (hier oben), Pin 6 bis 1: INT, CS, SCK, MOSI, GND, GND. J2 (unten): MISO, RST, NC, 3V3, 3V3, GND."""
    fp = new_fp('W5500_USR-ES1', 'W5500')
    for i, n in enumerate(['INT', 'CS', 'SCK', 'MOSI', 'GND', 'GND']):
        tht(fp, n, i * P, 0, square=(i == 5))
    for i, n in enumerate(['MISO', 'RST', 'NC', '3V3', '3V3', 'GND']):
        tht(fp, n, i * P, 8 * P, square=(i == 5))
    outline(fp, -5.8, -1.34, 5 * P + 6.4, 8 * P + 1.34)
    rect(fp, 2.5, 2.1, 5 * P + 8.9, 18.2, pcbnew.Dwgs_User, 0.1)
    text(fp, 'RJ45 LAN', 12.0, 10.0, 0.9, pcbnew.Dwgs_User)
    text(fp, 'INT', 0, 1.9, 0.7)
    text(fp, 'MISO', 0, 8 * P - 1.9, 0.7)
    return fp


def lib(libname, name, value):
    fp = pcbnew.FootprintLoad(os.path.join(LIB, libname + '.pretty'), name)
    if fp is None:
        raise SystemExit(f'Footprint fehlt: {libname}:{name}')
    fp.SetValue(value)
    fp.Value().SetLayer(pcbnew.F_Fab)
    return fp


def header(n, value):
    return lib('Connector_PinHeader_2.54mm', f'PinHeader_1x{n:02d}_P2.54mm_Vertical', value)


def res(value):
    return lib('Resistor_THT', 'R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal', value)


def cap(value):
    return lib('Capacitor_THT', 'C_Disc_D5.0mm_W2.5mm_P5.00mm', value)


def sot23(value):
    return lib('Package_TO_SOT_SMD', 'SOT-23_Handsoldering', value)


# ---------------------------------------------------------------- Platzierung

parts = {}      # ref -> footprint


# Wo die Bauteilnummer steht, bezogen auf Pin 1 des unverdrehten Bauteils
REF_POS = {'ESP32-S3_N16R8_2xUSB-C': (12.7, 23.0), 'CJMCU-752': (10.5, 14.0), 'ADuM1201_Modul': (6.35, 6.0),
           'TJA1050_Modul': (10.0, 6.0), 'B0505S-1WR3': (3.81, -2.5),
           'CMT2300A_10298-V1.0': (0.0, 2.2), 'R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal': (5.08, 0.0),
           'C_Disc_D5.0mm_W2.5mm_P5.00mm': (2.5, 0.0),
           'SOT-23_Handsoldering': (3.9, 0.0)}


SMD_FP = {'R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal': ('Resistor_SMD', 'R_0805_2012Metric_Pad1.20x1.40mm_HandSolder'),
          'C_Disc_D5.0mm_W2.5mm_P5.00mm': ('Capacitor_SMD', 'C_0805_2012Metric_Pad1.18x1.45mm_HandSolder')}
# R und C werden als bedrahtete Teile aufgerufen und hier durch 0805 ersetzt. Die Freilaufdioden haben
# einen eigenen Footprint fuer SMA oder SOD-123.
for _l, _n in SMD_FP.values():
    REF_POS[_n] = (0.0, -1.7)


def place(ref, fp, x, y, rot=0):
    name = str(fp.GetFPID().GetLibItemName())
    if name in SMD_FP:
        # SMD-Teil in die Mitte zwischen die beiden THT-Pads setzen, gleiche Richtung
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
        ps = list(fp.Pads())
        x = sum(ToMM(p_.GetPosition().x) for p_ in ps) / len(ps)
        y = sum(ToMM(p_.GetPosition().y) for p_ in ps) / len(ps)
        val = fp.GetValue()
        fp = lib(*SMD_FP[name], val)
    fp.SetReference(ref)
    board.Add(fp)
    fp.SetPosition(V(x, y))
    r = fp.Reference()
    r.SetTextSize(V(0.8, 0.8))
    r.SetTextThickness(FromMM(0.12))
    name = str(fp.GetFPID().GetLibItemName())
    if name in REF_POS:
        dx, dy = REF_POS[name]
        r.SetPosition(V(x + dx, y + dy))
        r.SetLocalCoord()
    else:
        # Stecker und Befestigungsloecher sind auf der Platine eigens beschriftet
        r.SetLayer(pcbnew.F_Fab)
    fp.SetOrientationDegrees(rot)
    parts[ref] = fp


# Alle Stiftleisten liegen auf einem Raster von 1,27 mm, damit der Router zwischen zwei Pins durchkommt.
def g(n):
    return n * 1.27


# Grundaufstellung, danach unten fuer die freie COM-Buchse umgestellt
def at(ref, fp, x, y, rot=0):
    """SMD-Teil mit der Mitte auf x, y"""
    place(ref, fp, x, y, rot)
    parts[ref].SetPosition(V(x, y))
    parts[ref].SetOrientationDegrees(rot)
    if ref[0] in 'RCD':
        parts[ref].Reference().SetPosition(V(x - 3.0, y) if rot == 0 else V(x + 2.0, y))

def chain(n, label, x3, o, adum, ps):
    """RS485-Kette: Modul gedreht, 3er-Reihe (Bus) links bei x3, 5er-Reihe rechts. o = Hoehe von GND der 5er-Reihe.
    Daneben ADuM1201 (Seite 2 zum Modul), darunter B0505S, Cin, Cout und Lastwiderstand."""
    x5 = x3 + 6 * P
    place(f'U{6 + n}', fp_rs485_klein(f'RS485 {label}'), x5, o, 180)
    s2 = x5 + 2 * P
    place(adum, fp_adum(f'ADuM1201 {label}'), s2 + 5 * P, o, 180)
    place(ps, fp_b0505s(f'B0505S {label}'), s2 + 3.5 * P, o + 3.81, 180)
    return x5, s2

CHAINS = [('P1', g(13), 'Pack 1', 1), ('P2', g(30), 'Pack 2', 2)]
for key, o, label, n in CHAINS:
    x5, s2 = chain(n, label, g(6), o, f'U{2 + n}', f'PS{n}')
    place(f'X{10 + n}', header(3, f'{label} A B GND'), g(2), o - 3 * P, 0)
    at(f'C{2 * n - 1}', cap('4,7u Cin'), s2 + 5 * P + 1.5, o + 6.35, 90)
    at(f'C{2 * n}', cap('10u Cout'), s2 - 2.2, o + 5.0, 90)
    at(f'R{n}', res('Last, unbestueckt'), s2 - 4.8, o + 5.0, 90)

# VE.Bus unten links: RJ45 an der Unterkante, Oeffnung nach unten
place('J1', lib('Connector_RJ', 'RJ45_Amphenol_54602-x08_Horizontal', 'RJ45 VE.Bus'), g(4), g(68), 0)
# Alternative zu J1: Stiftleiste rechts daneben, liegend, Belegung wie X11 und X12 (1 GND, 2 A, 3 B)
place('X13', header(3, 'VE.Bus A B GND'), g(18), g(76), 90)
oV = g(70)
x5, s2 = chain(3, 'VE.Bus', g(16), oV, 'U5', 'PS3')
at('C5', cap('4,7u Cin'), s2 + 5 * P + 1.5, oV + 6.35, 90)
at('C6', cap('10u Cout'), s2 - 2.2, oV + 5.0, 90)
at('R3', res('Last, unbestueckt'), s2 - 4.8, oV + 5.0, 90)

# CAN links in der Mitte, TJA1050 gedreht: CANH/CANL oben, Stiftleiste unten
oC = g(47)
place('X5', header(3, 'CAN'), g(2), g(41), 0)
place('U10', fp_tja(), g(7), g(59), 90)
sC = g(22)      # Seite 2 des ADuM, wie bei den Packs
place('U6', fp_adum('ADuM1201 CAN'), sC + 5 * P, oC, 180)
place('PS4', fp_b0505s('B0505S CAN'), sC + 3.5 * P, oC + 3.81, 180)
at('C7', cap('4,7u Cin'), sC + 5 * P + 1.5, oC + 6.35, 90)
at('C8', cap('10u Cout'), sC - 2.2, oC + 5.0, 90)
at('R4', res('Last, unbestueckt'), sC - 4.8, oC + 5.0, 90)

# S3 rechts oben, Antenne ueber der Oberkante. Links daneben SC16IS752, darunter CMT2300A und E-Paper
S3X, S3Y = g(57), g(2)
place('U1', fp_s3(), S3X, S3Y, 0)
place('U2', fp_sc16(), g(54), g(22), 180)
CMTX, CMTY = g(43), g(34)
place('U11', fp_cmt(), CMTX, CMTY, 180)
place('X10', header(8, 'E-Paper'), g(54), g(29), 0)

# Zaehler und BME280 nebeneinander im Abstand von 7,62 mm
XS = [('X2', 4, 'MT681'), ('X3', 4, 'BME280 1'), ('X4', 4, 'BME280 2')]
XS_X0, XS_Y = g(36), g(46)
for k, (ref, n_, val) in enumerate(XS):
    place(ref, header(n_, val), XS_X0 + k * 3 * P, XS_Y, 0)

# Relaistreiber in einer Reihe darunter
for n, x0 in ((1, 44.5), (2, 59.5)):
    at(f'R{4 + 2 * n - 1}', res('100R'), x0, 70.49)
    at(f'R{4 + 2 * n}', res('10k'), x0, 73.03)
    at(f'Q{n}', sot23('AO3402'), x0 + 5.6, 71.76)
    at(f'D{n}', fp_diode_sma_sod123(), x0 + 10.6, 72.4, 90)
    parts[f'D{n}'].Reference().SetLayer(pcbnew.F_SilkS)

# 5 V und Relais als Spalte, rechts daneben das W5500-Modul
for k, ref in enumerate(('X1', 'X6', 'X7')):
    place(ref, header(2, ref), g(52), g(63) + k * 2 * P, 90)

# W5500 unten rechts, RJ45 nach rechts ueber den Rand
place('U12', fp_w5500(), g(63), g(61), 0)

# USB-C fuer den CH348 am rechten Rand ueber dem W5500, Buchse nach rechts
place('X8', fp_usbc(), g(70), g(57), 90)
# 5-V-Schalter fuer X8 links daneben
at('R9', res('100R'), 76.2, 60.96)
at('R10', res('10k'), 76.2, 63.5)
at('R11', res('10k'), 76.2, 66.04)
at('R12', res('100k'), 76.2, 68.58)
at('Q4', sot23('AO3402'), 81.5, 62.0)
at('Q3', sot23('AO3401A'), 81.5, 68.0)
at('C9', cap('100n'), 81.5, 72.5, 0)

for n, (x, y) in enumerate([(3.5, 3.5), (85.09, 30.48), (3.0, 66.0), (69.85, 96.52)], 1):
    place(f'H{n}', lib('MountingHole', 'MountingHole_3.2mm_M3', 'M3'), x, y, 0)

# Die COM-Buchse sitzt am unteren Ende des S3-Boards bei x = 86 bis 96 mm. Davor bleibt ein Streifen
# x = 84,5 bis 98 mm, y = 57,8 bis 80 mm ganz frei, dahinter laeuft nur noch das Kabel.
def mv(ref, x, y, rot=0):
    parts[ref].SetPosition(V(x, y))
    parts[ref].SetOrientationDegrees(rot)
    if ref[0] in 'RCD' and ref[:2] != 'DS':
        parts[ref].Reference().SetPosition(V(x - 3.0, y) if rot == 0 else V(x, y - 2.6))

XS_X0 = g(35) + 1.0      # 1 mm mehr Platz links und rechts fuer die Gehaeuse der Gegenstecker
for k, (ref, n_, val) in enumerate(XS):
    mv(ref, XS_X0 + k * 3 * P, XS_Y)
mv('C7', 40.2, oC + 8.9, 90)
# Relaistreiber als zwei kleine Bloecke unter der nativen USB-Buchse, die frei bleibt
for n in (1, 2):
    x0, y0 = 64.0 + (n - 1) * 10.5, 59.5
    mv(f'R{4 + 2 * n - 1}', x0 + 1.9, y0)
    mv(f'R{4 + 2 * n}', x0 + 1.9, y0 + P)
    mv(f'Q{n}', x0 + 7.0, y0 + 1.27)
    mv(f'D{n}', x0 + 5.6, y0 + 5.6, 0)
# 5-V-Schalter als Reihe darunter
for k, ref in enumerate(('R9', 'R10', 'R11', 'R12', 'C9')):
    mv(ref, 61.0 + k * 2.2, 71.0, 90)
mv('Q4', 73.5, 71.0)
mv('Q3', 79.5, 71.0)
# W5500 an der Unterkante, RJ45 nach unten. Links daneben X1, X6, X7 senkrecht
mv('U12', g(65), g(63), -90)
for k, ref in enumerate(('X1', 'X6', 'X7')):
    mv(ref, g(46), g(62) + k * 5 * 1.27, 0)
# USB-C fuer den CH348 unten rechts, Buchse nach unten wie die beiden RJ45. Der rechte Rand bleibt frei.
mv('X8', g(69), g(70), 0)
mv('X10', g(53.5), g(30))
for ref, (x, y) in (('H1', (3.0, 23.3)), ('H2', (3.0, 45.0)), ('H3', (3.0, 66.0)), ('H4', (91.0, 82.2))):
    mv(ref, x, y)

# Eigener Regler fuer 3,3 V (LM1117-3.3, SOT-223), wahlweise per Loetbruecke JP1:
# 1-2: der Regler des S3-Boards versorgt alles (U13, C11, C12 unbestueckt)
# 2-3: U13 versorgt alle Module und Stecker an +3V3, das S3-Board nur sich selbst
place('U13', lib('Package_TO_SOT_SMD', 'SOT-223-3_TabPin2', 'LM1117-3.3'), 49.0, 72.3, 0)
at('C11', cap('10u Vin'), 43.6, 72.3, 90)
place('C12', lib('Capacitor_Tantalum_SMD', 'CP_EIA-3528-21_Kemet-B_Pad1.50x2.35mm_HandSolder', '10u Tantal'), 55.4, 72.3, 270)
at('JP1', fp_jumper3('3V3: 1-2 S3, 2-3 U13'), 58.7, 72.3, 90)

# Abblock-C am CMT2300A, direkt neben VCC (Pad 8)
at('C10', cap('100n CMT'), CMTX - 9.0, CMTY + 6.73, 90)

# ---------------------------------------------------------------- Netzliste

NETS = {
    'GND': [('U1', 'GND'), ('U2', 'GND'), ('U2', 'SPI'), ('U2', 'VSS'),
            ('U3', 'G1'), ('U4', 'G1'), ('U5', 'G1'), ('U6', 'G1'),
            ('PS1', '1'), ('PS2', '1'), ('PS3', '1'), ('PS4', '1'),
            ('C1', '2'), ('C3', '2'), ('C5', '2'), ('C7', '2'),
            ('X1', '1'), ('X2', '1'), ('X3', '1'), ('X4', '1'), ('X10', '1'),
            ('U11', '1'), ('U11', '2'), ('U11', '3'), ('U11', '4'), ('U11', '9'), ('U11', '10'),
            ('U11', '15'), ('U11', '16'), ('U11', '18'),
            ('Q1', '2'), ('Q2', '2'), ('R6', '1'), ('R8', '1'),
            ('X8', '4'), ('Q4', '2'), ('R10', '1')],
    '+5V': [('U1', '5V'), ('X1', '2'), ('PS1', '2'), ('PS2', '2'), ('PS3', '2'), ('PS4', '2'),
            ('C1', '1'), ('C3', '1'), ('C5', '1'), ('C7', '1'), ('U2', 'VCC'),
            ('X6', '1'), ('X7', '1'), ('D1', '1'), ('D2', '1'), ('Q3', '2'), ('R12', '1')],
    '+3V3': [('U1', '3V3'), ('U3', 'U1'), ('U4', 'U1'), ('U5', 'U1'), ('U6', 'U1'), ('U2', 'RESET'),
             ('X2', '2'), ('X3', '2'), ('X4', '2'), ('X10', '2'), ('U11', '8')],
    # VE.Bus, UART1
    'VEBUS_RX': [('U1', 'IO4'), ('U5', 'A0')],
    'VEBUS_TX': [('U1', 'IO5'), ('U5', 'B1')],
    # CAN, TWAI
    'TWAI_RX': [('U1', 'IO13'), ('U6', 'A0')],
    'TWAI_TX': [('U1', 'IO14'), ('U6', 'B1')],
    # SC16IS752, SPI2
    'SC_CS': [('U1', 'IO2'), ('U2', 'CS')],
    'SC_SI': [('U1', 'IO42'), ('U2', 'SI')],
    'SC_SO': [('U1', 'IO41'), ('U2', 'SO')],
    'SC_SCLK': [('U1', 'IO38'), ('U2', 'SCLK')],
    # Pack 1 an UART B, Pack 2 an UART A
    'UARTB_RX': [('U2', 'RXB'), ('U3', 'A0')],
    'UARTB_TX': [('U2', 'TXB'), ('U3', 'B1')],
    'UARTA_RX': [('U2', 'RXA'), ('U4', 'A0')],
    'UARTA_TX': [('U2', 'TXA'), ('U4', 'B1')],
    # Zaehler und BME280
    'MT_HAUS': [('U1', 'IO7'), ('X2', '3')],
    'MT_WP': [('U1', 'IO6'), ('X2', '4')],
    'I2C_SDA': [('U1', 'IO16'), ('X3', '3'), ('X4', '3')],
    'I2C_SCL': [('U1', 'IO15'), ('X3', '4'), ('X4', '4')],
    # CMT2300A
    'CMT_CSB': [('U1', 'IO8'), ('U11', '13')],
    'CMT_SDIO': [('U1', 'IO3'), ('U11', '12')],
    'CMT_SCLK': [('U1', 'IO46'), ('U11', '11')],
    'CMT_FCSB': [('U1', 'IO9'), ('U11', '14')],
    # Relaistreiber
    'FAN': [('U1', 'IO11'), ('R5', '1')],
    'FAN_G': [('R5', '2'), ('R6', '2'), ('Q1', '1')],
    'FAN_SW': [('Q1', '3'), ('D1', '2'), ('X6', '2')],
    'REL2': [('U1', 'IO12'), ('R7', '1')],
    'REL2_G': [('R7', '2'), ('R8', '2'), ('Q2', '1')],
    'REL2_SW': [('Q2', '3'), ('D2', '2'), ('X7', '2')],
    # USB-Host zum CH348, 5 V geschaltet
    'USB_DM': [('U1', 'IO19'), ('X8', '2')],
    'USB_DP': [('U1', 'IO20'), ('X8', '3')],
    'USB_EN': [('U1', 'IO21'), ('R9', '1')],
    'USB_EN_G': [('R9', '2'), ('R10', '2'), ('Q4', '1')],
    'USB_SW': [('Q4', '3'), ('R11', '1')],
    'USB_PG': [('R11', '2'), ('R12', '2'), ('Q3', '1'), ('C9', '1')],
    'USB_VBUS': [('Q3', '3'), ('C9', '2'), ('X8', '1')],
    # E-Paper
    'EPD_BUSY': [('U1', 'IO1'), ('X10', '3')],
    'EPD_RST': [('U1', 'IO18'), ('X10', '4')],
    'EPD_DC': [('U1', 'IO17'), ('X10', '5')],
    'EPD_CS': [('U1', 'IO47'), ('X10', '6')],
    'EPD_MOSI': [('U1', 'IO40'), ('X10', '7')],
    'EPD_SCK': [('U1', 'IO39'), ('X10', '8')],
    # CAN-Seite, isoliert
    'CAN_5V': [('PS4', '4'), ('U6', 'U2'), ('U10', 'VCC'), ('C8', '1'), ('R4', '1')],
    'CAN_GND': [('PS4', '3'), ('U6', 'G2'), ('U10', 'GND'), ('C8', '2'), ('R4', '2'), ('X5', '1')],
    'CAN_TXD': [('U6', 'B0'), ('U10', 'TX')],
    'CAN_RXD': [('U10', 'RX'), ('U6', 'A1')],
    'CAN_H': [('U10', 'CANH'), ('X5', '2')],
    'CAN_L': [('U10', 'CANL'), ('X5', '3')],
}
# Busseiten der drei RS485-Ketten, isoliert. TXD am Modul ist der Ausgang des Empfaengers (RO, Pin 1)
# und geht an den Eingang A1. Der Ausgang B0 geht an RXD, den Eingang des Treibers (DI, Pin 4).
for key, n in (('P1', 1), ('P2', 2), ('VE', 3)):
    NETS[f'{key}_5V'] = [(f'PS{n}', '4'), (f'U{2 + n}', 'U2'), (f'U{6 + n}', 'VCC'), (f'C{2 * n}', '1'), (f'R{n}', '1')]
    NETS[f'{key}_GND'] = [(f'PS{n}', '3'), (f'U{2 + n}', 'G2'), (f'U{6 + n}', 'GND'), (f'C{2 * n}', '2'), (f'R{n}', '2')]
    NETS[f'{key}_R'] = [(f'U{6 + n}', 'TXD'), (f'U{2 + n}', 'A1')]
    NETS[f'{key}_D'] = [(f'U{2 + n}', 'B0'), (f'U{6 + n}', 'RXD')]

# Kleine RS485-Module: EN an VCC (RE und SHDN brauchen High), Bus ueber Stecker bzw. RJ45
for key, n, xs in (('P1', 1, 'X11'), ('P2', 2, 'X12'), ('VE', 3, None)):
    u = f'U{6 + n}'
    NETS[f'{key}_5V'].append((u, 'EN'))
    # TXD des Moduls ist RO (Pin 1, Ausgang), RXD ist DI (Pin 4, Eingang): auf dem Haendlerfoto
    # an den Leiterbahnen abgelesen. TXD an A1 (Eingang des ADuM), B0 (Ausgang) an RXD.
    if xs:
        NETS[f'{key}_A'] = [(u, 'A'), (xs, '2')]
        NETS[f'{key}_B'] = [(u, 'B'), (xs, '3')]
        NETS[f'{key}_GND'].append((xs, '1'))
# VE.Bus an RJ45: Pin 4 A, Pin 5 B, Pin 3 GND (esp32ess). Pin 2 (+12 V) und die uebrigen bleiben offen
NETS['VE_A'] = [('U9', 'A'), ('J1', '4')]
NETS['VE_B'] = [('U9', 'B'), ('J1', '5')]
NETS['VE_GND'].append(('J1', '3'))
NETS['VE_A'].append(('X13', '2'))
NETS['VE_B'].append(('X13', '3'))
NETS['VE_GND'].append(('X13', '1'))
# W5500 am SPI des SC16IS752, CS an GPIO 10, RST an RST des S3-Boards, INT frei (Abfrage ohne INT)
NETS['GND'].append(('U12', 'GND'))
NETS['+3V3'].append(('U12', '3V3'))
NETS['SC_SI'].append(('U12', 'MOSI'))
NETS['SC_SO'].append(('U12', 'MISO'))
NETS['SC_SCLK'].append(('U12', 'SCK'))
NETS['ETH_CS'] = [('U1', 'IO10'), ('U12', 'CS')]
NETS['ETH_RST'] = [('U1', 'RST'), ('U12', 'RST')]

NETS['+3V3'].append(('C10', '1'))
NETS['GND'].append(('C10', '2'))
NETS['3V3_ESP'] = [p_ for p_ in NETS['+3V3'] if p_[0] == 'U1'] + [('JP1', '1')]
NETS['+3V3'] = [p_ for p_ in NETS['+3V3'] if p_[0] != 'U1'] + [('JP1', '2')]
NETS['3V3_LDO'] = [('U13', '2'), ('C12', '1'), ('JP1', '3')]
NETS['+5V'] += [('U13', '3'), ('C11', '1')]
NETS['GND'] += [('U13', '1'), ('C11', '2'), ('C12', '2')]

# Richtung der Pins fuer die Pruefung: out, in, io, pwr, pas (passiv)
DIR = {}
for r in ('U3', 'U4', 'U5', 'U6'):
    DIR.update({(r, 'A0'): 'out', (r, 'B0'): 'out', (r, 'A1'): 'in', (r, 'B1'): 'in'})
for r in ('U7', 'U8', 'U9'):
    DIR.update({(r, 'TXD'): 'out', (r, 'RXD'): 'in'})
DIR.update({('U10', 'TX'): 'in', ('U10', 'RX'): 'out', ('U10', 'CANH'): 'io', ('U10', 'CANL'): 'io'})
DIR.update({('U2', n): 'in' for n in ('CS', 'SI', 'SCLK', 'RXA', 'RXB', 'RESET', 'SPI', 'VSS')})
DIR.update({('U2', n): 'out' for n in ('SO', 'TXA', 'TXB')})
DIR.update({('U11', '11'): 'in', ('U11', '12'): 'io', ('U11', '13'): 'in', ('U11', '14'): 'in'})
S3_DIR = {'IO4': 'in', 'IO5': 'out', 'IO13': 'in', 'IO14': 'out', 'IO2': 'out', 'IO42': 'out', 'IO41': 'in',
          'IO38': 'out', 'IO7': 'in', 'IO6': 'in', 'IO16': 'io', 'IO15': 'io', 'IO8': 'out', 'IO3': 'io',
          'IO46': 'out', 'IO9': 'out', 'IO11': 'out', 'IO12': 'out', 'IO19': 'io', 'IO20': 'io', 'IO21': 'out',
          'IO1': 'in', 'IO18': 'out', 'IO17': 'out', 'IO47': 'out', 'IO40': 'out', 'IO39': 'out', 'IO10': 'io'}
DIR.update({('U1', k): v for k, v in S3_DIR.items()})
# MISO der W5500 ist nur bei aktivem CS getrieben, wie SO des SC16IS752
DIR.update({('U12', 'MOSI'): 'in', ('U12', 'SCK'): 'in', ('U12', 'CS'): 'in', ('U12', 'MISO'): 'tri',
            ('U1', 'IO10'): 'out'})
POWER = {'GND', '+5V', '+3V3', 'USB_VBUS', '3V3_ESP', '3V3_LDO'} | {n for n in NETS if n.endswith('_5V') or n.endswith('_GND')}


def check_netlist():
    seen = {}
    for net, pins in NETS.items():
        for pin in pins:
            if pin in seen:
                raise SystemExit(f'Pin {pin} doppelt: {seen[pin]} und {net}')
            seen[pin] = net
            ref, name = pin
            if ref not in parts:
                raise SystemExit(f'{net}: Bauteil {ref} fehlt')
            if not [p for p in parts[ref].Pads() if p.GetNumber() == name]:
                raise SystemExit(f'{net}: {ref} hat kein Pad {name}')
        if net in POWER:
            continue
        d = [DIR.get(p, 'pas') for p in pins]
        if d.count('out') > 1:
            raise SystemExit(f'{net}: mehr als ein Ausgang {pins}')
        if 'in' in d and 'out' not in d and 'io' not in d and 'pas' not in d:
            raise SystemExit(f'{net}: Eingang ohne Ausgang {pins}')
        if d.count('out') == 1 and 'in' not in d and 'pas' not in d and 'io' not in d:
            raise SystemExit(f'{net}: Ausgang ohne Eingang {pins}')
    # jeder Pin mit Richtung muss angeschlossen sein
    for pin in DIR:
        if pin not in seen:
            raise SystemExit(f'Pin mit Richtung nicht angeschlossen: {pin}')


check_netlist()

netinfo = {}
for net in NETS:
    ni = pcbnew.NETINFO_ITEM(board, net)
    board.Add(ni)
    netinfo[net] = ni
for net, pins in NETS.items():
    for ref, name in pins:
        for p in parts[ref].Pads():
            if p.GetNumber() == name:
                p.SetNet(netinfo[net])


# ---------------------------------------------------------------- Lage pruefen

# USB-C: Buchse muss nach unten ueber den Rand zeigen
bb = parts['X8'].GetBoundingBox(False, False)
if ToMM(bb.GetBottom()) < H:
    raise SystemExit('X8 zeigt nicht ueber den Rand')

# ---------------------------------------------------------------- Rand, Flaechen, Beschriftung

def board_rect(x1, y1, x2, y2, layer, width=0.1):
    for a, b in (((x1, y1), (x2, y1)), ((x2, y1), (x2, y2)), ((x2, y2), (x1, y2)), ((x1, y2), (x1, y1))):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(*a))
        s.SetEnd(V(*b))
        s.SetLayer(layer)
        s.SetWidth(FromMM(width))
        board.Add(s)


def board_text(s, x, y, size=1.0, layer=pcbnew.F_SilkS, angle=0, just=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetTextSize(V(size, size))
    t.SetTextThickness(FromMM(size * 0.15))
    t.SetPosition(V(x, y))
    t.SetTextAngleDegrees(angle)
    if just == 'left':
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    if just == 'right':
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    board.Add(t)


board_rect(0, 0, W, H, pcbnew.Edge_Cuts)


def zone(points, layers, net=None, keepout=False, name=''):
    z = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    o = z.Outline()
    o.NewOutline()
    for x, y in points:
        o.Append(FromMM(x), FromMM(y))
    if keepout:
        z.SetIsRuleArea(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
    else:
        z.SetNet(netinfo[net])
        z.SetLocalClearance(FromMM(0.3))
        z.SetMinThickness(FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(FromMM(0.4))
        z.SetThermalReliefSpokeWidth(FromMM(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetZoneName(name)
    board.Add(z)
    return z


# GND-Flaeche nur auf der Seite des S3. Die Busseiten bekommen keine Flaeche.
S3_AREA = [(33.6, 0), (W, 0), (W, H), (46.3, H), (46.3, 76.6), (33.6, 76.6)]
zone(S3_AREA, [pcbnew.F_Cu, pcbnew.B_Cu], net='GND', name='GND S3')
# Kein Kupfer unter der Antenne des WROOM
ant = (S3X + 2.2, S3X + 23.4)
zone([(ant[0], 0), (ant[1], 0), (ant[1], 1.3), (ant[0], 1.3)], [pcbnew.F_Cu, pcbnew.B_Cu], keepout=True, name='Antenne')
# Pad 17 (ANT) des CMT2300A bleibt frei
ax, ay = CMTX + 7.0, CMTY - (17.73 - 10.0)
zone([(ax - 1.3, ay - 0.6), (ax + 1.6, ay - 0.6), (ax + 1.6, ay + 0.6), (ax - 1.3, ay + 0.6)],
     [pcbnew.F_Cu, pcbnew.B_Cu], keepout=True, name='ANT')

# Das CMT2300A liegt flach auf der Platine. Darunter bleibt die Oberseite ohne Kupfer: keine Bahn,
# keine Durchkontaktierung, keine Flaeche. Die Lötpads liegen ausserhalb (ab 6,1 mm von der Mitte).
zone([(CMTX - 5.9, CMTY - 9.9), (CMTX + 5.9, CMTY - 9.9), (CMTX + 5.9, CMTY + 9.9), (CMTX - 5.9, CMTY + 9.9)],
     [pcbnew.F_Cu], keepout=True, name='unter CMT2300A')

# Trennlinie zwischen S3 und Busseiten: Mitten unter ADuM1201 und B0505S laeuft ein Streifen ohne Kupfer.
# Er liegt in der Luecke zwischen Pin 2 (Vin) und Pin 3 (0V) der B0505S.
zone([(33.02 - 0.3, 0), (33.02 + 0.3, 0), (33.02 + 0.3, 77.5), (33.02 - 0.3, 77.5)],
     [pcbnew.F_Cu, pcbnew.B_Cu], keepout=True, name='Trennung Packs CAN')
zone([(45.72 - 0.3, 76.0), (45.72 + 0.3, 76.0), (45.72 + 0.3, H), (45.72 - 0.3, H)],
     [pcbnew.F_Cu, pcbnew.B_Cu], keepout=True, name='Trennung VE.Bus')
for k, (ref, t) in enumerate((('X1', 'X1 5V   G +'), ('X6', 'X6 Luefter  + S'), ('X7', 'X7 Relais 2  + S'))):
    board_text(ref, g(46) - 1.3, g(62) + k * 5 * 1.27, 0.7, just='right')
    board_text(t.split(' ', 1)[1].split()[-2] + t.split(' ', 1)[1].split()[-1], g(46) - 1.3, g(62) + k * 5 * 1.27 + P, 0.7, just='right')
XN = {'X2': ('X2 MT681', ['GND', '3V3', 'Haus', 'WP']),
      'X3': ('X3 BME', ['GND', '3V3', 'SDA', 'SCL']), 'X4': ('X4 BME', ['GND', '3V3', 'SDA', 'SCL'])}
for k, (ref, n_, val) in enumerate(XS):
    x0 = XS_X0 + k * 3 * P
    title, pins = XN[ref]
    board_text(title, x0, XS_Y - 2.0, 0.7)
    for i, t in enumerate(pins):
        board_text(t, x0 + 1.25, XS_Y + i * P, 0.7, just='left')
board_text('X5 CAN', g(2), g(41) - 2.2, 0.7)
for s_, i in zip(['G', 'H', 'L'], range(3)):
    board_text(s_, 0.6, g(41) + i * P, 0.7)
for key, o, label, n in CHAINS:
    board_text(f'X{10 + n} {label}', g(2), o - 3 * P - 2.2, 0.7, just='left')
    for s_, i in zip(['G', 'A', 'B'], range(3)):
        board_text(s_, 0.6, o - 3 * P + i * P, 0.7)
board_text('J1 VE.Bus', g(4) + 4.4, g(68) - 5.6, 0.8)
x13, y13 = ToMM(parts['X13'].GetPosition().x), ToMM(parts['X13'].GetPosition().y)
board_text('X13 VE.Bus', x13 - 1.3, y13 - 3.7, 0.7, just='left')
for s_, i in zip(['G', 'A', 'B'], range(3)):
    board_text(s_, x13 + i * P, y13 - 2.2, 0.7)
x10, y10 = ToMM(parts['X10'].GetPosition().x), ToMM(parts['X10'].GetPosition().y)
board_text('X10 E-Paper', x10, y10 - 2.1, 0.7)
for s_, i in zip(['GND', '3V3', 'BUSY', 'RST', 'DC', 'CS', 'MOSI', 'SCK'], range(8)):
    board_text(s_, x10 - 1.3, y10 + i * P, 0.7, just='right')
board_text('X8 USB-C CH348', 98.9, 93.0, 0.7, angle=90)
board_text('frei fuer COM-Stecker', 91.2, 73.0, 0.8, pcbnew.Dwgs_User)
board_rect(84.5, 57.8, 98.0, 80.0, pcbnew.Dwgs_User)
for i_, t_ in enumerate(('ESP_venus v1', 'Ruehle', 'pillepalle127', '2026')):
    board_text(t_, 91.25, 59.6 + i_ * 1.6, 1.0)
# Regler und Loetbruecke: was welche Stellung bedeutet
board_text('U13 LM1117 optional', 50.0, 76.4, 0.7)
board_text('JP1 1-2: S3   2-3: U13', 50.0, 77.7, 0.7)

# ---------------------------------------------------------------- Projektdatei mit Netzklassen

BUS = ['P1_*', 'P2_*', 'VE_*', 'CAN_*']
POWER_NETS = ['GND', '+5V', '+3V3', 'USB_VBUS', 'FAN_SW', 'REL2_SW', '3V3_ESP', '3V3_LDO']
for n in NETS:
    if any(n.startswith(b[:-1]) for b in BUS) == (n in POWER_NETS):
        assert not (n in POWER_NETS and any(n.startswith(b[:-1]) for b in BUS)), n


def netclass(name, clearance, width):
    return {'name': name, 'clearance': clearance, 'track_width': width, 'via_diameter': 0.8, 'via_drill': 0.4,
            'microvia_diameter': 0.3, 'microvia_drill': 0.1, 'diff_pair_width': 0.2, 'diff_pair_gap': 0.25,
            'diff_pair_via_gap': 0.25, 'bus_width': 12, 'wire_width': 6, 'line_style': 0,
            'pcb_color': 'rgba(0, 0, 0, 0.000)', 'schematic_color': 'rgba(0, 0, 0, 0.000)'}


pro = {
    'board': {'design_settings': {'rules': {
        'min_clearance': 0.15, 'min_track_width': 0.3, 'min_via_diameter': 0.7, 'min_via_annular_width': 0.13,
        'min_through_hole_diameter': 0.3, 'min_hole_to_hole': 0.5, 'min_copper_edge_clearance': 0.4,
        'min_hole_clearance': 0.25, 'min_silk_clearance': 0.0, 'min_text_height': 0.7, 'min_text_thickness': 0.08,
        'solder_mask_to_copper_clearance': 0.0, 'min_resolved_spokes': 1, 'allow_blind_buried_vias': False, 'allow_microvias': False}}},
    'net_settings': {
        # Abstand 0,15 mm, damit eine Bahn von 0,4 mm zwischen zwei Pins im Raster 2,54 mm passt.
        # Der Router haelt 0,05 mm mehr ein.
        'classes': [netclass('Default', 0.15, 0.4), netclass('Power', 0.15, 0.8), netclass('Bus', 0.8, 0.5)],
        'meta': {'version': 3},
        'net_colors': None,
        'netclass_assignments': None,
        'netclass_patterns': [{'netclass': 'Power', 'pattern': n} for n in POWER_NETS]
                             + [{'netclass': 'Bus', 'pattern': b} for b in BUS],
    },
    'meta': {'filename': os.path.basename(OUT).replace('.kicad_pcb', '.kicad_pro'), 'version': 1},
}
pcbnew.SaveBoard(OUT, board)
# SaveBoard legt eine leere Projektdatei an, deshalb erst danach schreiben
with open(OUT.replace('.kicad_pcb', '.kicad_pro'), 'w') as f:
    json.dump(pro, f, indent=2)

# Netzliste als Text, zum Lesen und zum Abgleich
with open(OUT.replace('.kicad_pcb', '_netze.txt'), 'w') as f:
    for net in sorted(NETS):
        f.write(net + ': ' + ', '.join(f'{r}.{n}' for r, n in NETS[net]) + '\n')
print(f'{OUT}: {len(parts)} Bauteile, {len(NETS)} Netze')
