#!/usr/bin/env python3
# Schaltplan (KiCad 7) der Platine "klein, USB frei" mit echten Symbolen, ein Blatt A3.
# Die Netze kommen aus der Platinendatei. Am Ende exportiert kicad-cli die Netzliste des Schaltplans,
# und das Skript vergleicht sie Pin fuer Pin mit der Platine.
# Aufruf: python3 gen_kicad_sch.py   -> schaltplan_usbfrei.kicad_sch, schaltplan_usbfrei.pdf
import os
import re
import subprocess
import sys
import tempfile
import uuid

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'esp_venus_klein_usbfrei.kicad_pcb')
OUT = os.path.join(HERE, 'schaltplan_usbfrei.kicad_sch')
PDF = os.path.join(HERE, 'schaltplan_usbfrei.pdf')
SYMDIR = os.environ.get('KICAD7_SYMBOL_DIR', '/usr/share/kicad/symbols')
LIBNAME = 'esp_venus'
G = 2.54

# ---------------------------------------------------------------- Netze aus der Platine

board = pcbnew.LoadBoard(PCB)
PADNET = {}
for fp in board.GetFootprints():
    for p in fp.Pads():
        if p.GetNumber():
            PADNET[(fp.GetReference(), p.GetNumber())] = p.GetNetname()
VALUE = {fp.GetReference(): fp.GetValue() for fp in board.GetFootprints()}

# ---------------------------------------------------------------- S-Ausdruecke


def parse(s):
    toks = re.findall(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+', s)
    stack = [[]]
    for t in toks:
        if t == '(':
            stack.append([])
        elif t == ')':
            x = stack.pop()
            stack[-1].append(x)
        else:
            stack[-1].append(t)
    return stack[0]


def dump(x):
    if isinstance(x, list):
        return '(' + ' '.join(dump(i) for i in x) + ')'
    return x


def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def f(v):
    return f'{v:.4f}'.rstrip('0').rstrip('.')


def uid():
    return str(uuid.uuid4())


# ---------------------------------------------------------------- Bibliothekssymbole

_libcache = {}


def libfile(name):
    if name not in _libcache:
        _libcache[name] = parse(open(os.path.join(SYMDIR, name + '.kicad_sym')).read())[0]
    return _libcache[name]


def find_sym(lib, name):
    for x in libfile(lib):
        if isinstance(x, list) and x[0] == 'symbol' and x[1] == q(name):
            return x
    raise KeyError(name)


LIBSYMS = {}     # Name -> S-Ausdruck
PINS = {}        # Name -> {Nummer: (x, y, Winkel)}


def rename(sym, old, new):
    sym = [list(i) if isinstance(i, list) else i for i in sym]
    sym[1] = q(f'{LIBNAME}:{new}')
    for i, x in enumerate(sym):
        if isinstance(x, list) and x[0] == 'symbol':
            sym[i] = [x[0], x[1].replace(old, new, 1)] + x[2:]
    return sym


def collect_pins(sym):
    pins = {}

    def walk(x):
        if isinstance(x, list):
            if x and x[0] == 'pin':
                at = next(i for i in x if isinstance(i, list) and i[0] == 'at')
                num = next(i for i in x if isinstance(i, list) and i[0] == 'number')[1].strip('"')
                pins.setdefault(num, (float(at[1]), float(at[2]), float(at[3])))
            for i in x:
                walk(i)
    walk(sym)
    return pins


def use_lib(lib, name, new=None):
    new = new or name
    if new in LIBSYMS:
        return new
    s = find_sym(lib, name)
    ext = next((i for i in s if isinstance(i, list) and i[0] == 'extends'), None)
    if ext:
        parent = ext[1].strip('"')
        p = find_sym(lib, parent)
        props = [i for i in s if isinstance(i, list) and i[0] == 'property']
        body = [i for i in p if not (isinstance(i, list) and i[0] == 'property')]
        s = body[:2] + [i for i in body[2:] if not isinstance(i, list) or i[0] != 'symbol'] + props + \
            [i for i in body if isinstance(i, list) and i[0] == 'symbol']
        s = rename(s, parent, new)
    else:
        s = rename(s, name, new)
    LIBSYMS[new] = s
    PINS[new] = collect_pins(s)
    return new


def power_sym(net):
    """Eigenes Versorgungssymbol je Netz: Masse-Form fuer *GND, Pfeil fuer alles andere"""
    name = 'PWR_' + net.replace('+', 'P')
    if name in LIBSYMS:
        return name
    base = 'GND' if net.endswith('GND') else '+5V'
    s = find_sym('power', base)
    s = rename(s, base, name)
    txt = dump(s)
    txt = txt.replace(f'(property "Value" {q(base)}', f'(property "Value" {q(net)}')
    txt = txt.replace(f'(name {q(base)}', f'(name {q(net)}')
    s = parse(txt)[0]
    LIBSYMS[name] = s
    PINS[name] = collect_pins(s)
    return name


def box_sym(name, left=(), right=(), top=(), bottom=(), title_w=8, numbers=False, stacked=None):
    """Modul als Kasten. Eintraege: (Nummer, Name) oder None fuer eine Luecke.
    stacked: {Nummer: [weitere Nummern]} liegen verdeckt auf demselben Pin (z. B. mehrere GND)."""
    stacked = stacked or {}

    def w(lst):
        return max([len(e[1]) for e in lst if e] + [0])
    side = max(w(left), w(right)) * 1.0 + 2.0
    hw = max(side + title_w / 2 + 1.0, (max(len(top), len(bottom)) + 1) * G / 2 + G)
    hw = G * int(hw / G + 0.999)
    n = max(len(left), len(right), 2)
    if top or bottom:
        # Platz fuer die senkrechten Pinnamen oben und unten, damit sie die Mitte frei lassen
        extra = max(w(top), w(bottom)) * 1.0 + 1.8
        extra = G / 2 * int(extra / (G / 2) + 0.999)
    else:
        extra = G / 2
    hh = n * G / 2 + extra
    pins = []

    def pin(num, nm, x, y, a, hide=False):
        pins.append(['pin', 'passive', 'line', ['at', f(x), f(y), str(a)], ['length', f(G)]] +
                    (['hide'] if hide else []) +
                    [['name', q(nm), ['effects', ['font', ['size', '1.27', '1.27']]]],
                     ['number', q(num), ['effects', ['font', ['size', '1.27', '1.27']]]]])
        for extra in stacked.get(num, []):
            pins.append(['pin', 'passive', 'line', ['at', f(x), f(y), str(a)], ['length', f(G)], 'hide',
                         ['name', q(nm), ['effects', ['font', ['size', '1.27', '1.27']]]],
                         ['number', q(extra), ['effects', ['font', ['size', '1.27', '1.27']]]]])

    def ys(k):
        return [(-(k - 1) / 2 + i) * G for i in range(k)]
    for (lst, sx, a) in ((left, -hw - G, 0), (right, hw + G, 180)):
        for e, y in zip(lst, ys(len(lst))):
            if e:
                pin(e[0], e[1], sx, -y, a)
    for (lst, sy, a) in ((top, hh + G, 270), (bottom, -hh - G, 90)):
        xs = [(-(len(lst) - 1) / 2 + i) * 3 * G for i in range(len(lst))]
        for e, x in zip(lst, xs):
            if e:
                pin(e[0], e[1], x, sy, a)
    eff = ['effects', ['font', ['size', '1.27', '1.27']]]
    s = ['symbol', q(f'{LIBNAME}:{name}')] + ([] if numbers else [['pin_numbers', 'hide']]) + \
        [['pin_names', ['offset', '0.762']], ['in_bom', 'yes'], ['on_board', 'yes'],
         ['property', '"Reference"', '"U"', ['at', '0', '0', '0'], eff + ['hide']],
         ['property', '"Value"', q(name), ['at', '0', '0', '0'], eff + ['hide']],
         ['property', '"Footprint"', '""', ['at', '0', '0', '0'], eff + ['hide']],
         ['property', '"Datasheet"', '""', ['at', '0', '0', '0'], eff + ['hide']],
         ['symbol', q(f'{name}_0_1'), ['rectangle', ['start', f(-hw), f(hh)], ['end', f(hw), f(-hh)],
                                       ['stroke', ['width', '0.254'], ['type', 'default']],
                                       ['fill', ['type', 'background']]]],
         ['symbol', q(f'{name}_1_1')] + pins]
    LIBSYMS[name] = s
    PINS[name] = collect_pins(s)
    return name, hw, hh


# ---------------------------------------------------------------- Zeichnen

ITEMS = []
INST = []       # (ref, symname, x, y, rot, mirror, fields)
ROOT = uid()
PWR_N = [0]


class Pin:
    def __init__(self, x, y, dx, dy):
        self.x, self.y, self.dx, self.dy = round(x, 4), round(y, 4), dx, dy

    @property
    def p(self):
        return (self.x, self.y)


def tf(vx, vy, rot, mirror):
    # Bibliothek: y nach oben. Schaltplan: y nach unten.
    x, y = vx, -vy
    for _ in range((rot // 90) % 4):
        x, y = y, -x
    if mirror == 'x':
        y = -y
    elif mirror == 'y':
        x = -x
    return x, y


def place(ref, sym, x, y, rot=0, mirror=None, value=None, fields=None, show_value=True):
    """fields: Liste (Text, dx, dy) relativ zum Bezugspunkt, waagerecht und mittig"""
    pins = {}
    for num, (px, py, a) in PINS[sym].items():
        ox, oy = tf(px, py, rot, mirror)
        import math
        bx, by = tf(math.cos(math.radians(a)), math.sin(math.radians(a)), rot, mirror)
        pins[num] = Pin(x + ox, y + oy, -round(bx), -round(by))
    INST.append((ref, sym, x, y, rot, mirror, value if value is not None else VALUE.get(ref, ''), fields, show_value))
    return pins


def wire(*pts):
    pts = [p.p if isinstance(p, Pin) else p for p in pts]
    for a, b in zip(pts, pts[1:]):
        if a != b:
            ITEMS.append(['wire', ['pts', ['xy', f(a[0]), f(a[1])], ['xy', f(b[0]), f(b[1])]],
                          ['stroke', ['width', '0'], ['type', 'default']], ['uuid', uid()]])


def junction(p):
    p = p.p if isinstance(p, Pin) else p
    ITEMS.append(['junction', ['at', f(p[0]), f(p[1])], ['diameter', '0'], ['color', '0', '0', '0', '0'],
                  ['uuid', uid()]])


def nc(pin):
    ITEMS.append(['no_connect', ['at', f(pin.x), f(pin.y)], ['uuid', uid()]])


def label(net, p, d):
    ang = {(1, 0): 0, (0, -1): 90, (-1, 0): 180, (0, 1): 270}[d]
    just = {0: 'left', 90: 'left', 180: 'right', 270: 'right'}[ang]
    ITEMS.append(['label', q(net), ['at', f(p[0]), f(p[1]), str(ang)], ['fields_autoplaced'],
                  ['effects', ['font', ['size', '1.27', '1.27']], ['justify', just, 'bottom']], ['uuid', uid()]])


def power(net, p, d):
    """Versorgungssymbol an p, zeigt in Richtung d"""
    sym = power_sym(net)
    gnd = net.endswith('GND')
    # natuerliche Richtung: GND nach unten, sonst nach oben
    rot = ({(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270} if gnd else
           {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270})[d]
    PWR_N[0] += 1
    tx = p[0] + d[0] * (5.0 if d[0] else 0)
    ty = p[1] + (4.2 if (gnd and d == (0, 1)) else -3.6 if d == (0, -1) else 0)
    if d[0]:
        ty = p[1] - 1.0
        tx = p[0] + d[0] * (3.6 + 0.5 * len(net))
    INST.append((f'#PWR{PWR_N[0]:03d}', sym, p[0], p[1], rot, None, net, [(net, tx - p[0], ty - p[1])], False))


POWER_NETS = set()


def stub(pin, net, n=1):
    end = (pin.x + pin.dx * G * n, pin.y + pin.dy * G * n)
    wire(pin.p, end)
    if net in POWER_NETS:
        power(net, end, (pin.dx, pin.dy))
    else:
        label(net, end, (pin.dx, pin.dy))
    return end


def text(s, x, y, size=1.27, bold=False):
    font = ['font', ['size', f(size), f(size)]] + (['bold'] if bold else [])
    ITEMS.append(['text', q(s), ['at', f(x), f(y), '0'], ['effects', font, ['justify', 'left', 'bottom']],
                  ['uuid', uid()]])


def frame(x0, y0, x1, y1, title):
    ITEMS.append(['polyline', ['pts', ['xy', f(x0), f(y0)], ['xy', f(x1), f(y0)], ['xy', f(x1), f(y1)],
                               ['xy', f(x0), f(y1)], ['xy', f(x0), f(y0)]],
                  ['stroke', ['width', '0'], ['type', 'dash']], ['fill', ['type', 'none']], ['uuid', uid()]])
    text(title, x0 + 1.5, y0 + 3.5, 1.8, True)


def S(ref, pins, num, n=1):
    """Stummel mit Netz aus der Platine, offene Pins bekommen ein X"""
    net = PADNET[(ref, num)]
    if not net:
        nc(pins[num])
        return None
    return stub(pins[num], net, n)


def setfields(fields, show_value=False):
    INST[-1] = INST[-1][:7] + (fields, show_value)


# ---------------------------------------------------------------- Symbole

for n in ('R', 'C', 'C_Polarized', 'D', 'Q_NMOS_GSD', 'Q_PMOS_GSD'):
    use_lib('Device', n)
use_lib('Jumper', 'SolderJumper_3_Open')
use_lib('Regulator_Linear', 'AMS1117-3.3', 'LM1117-3.3')
for k in (2, 3, 4, 8):
    use_lib('Connector_Generic', f'Conn_01x{k:02d}')

POWER_NETS |= {'GND', '+5V', '+3V3', '3V3_ESP', '3V3_LDO'}
for c in ('P1', 'P2', 'VE', 'CAN'):
    POWER_NETS |= {f'{c}_5V', f'{c}_GND'}

S3L = ['RST', 'IO4', 'IO5', 'IO6', 'IO7', 'IO15', 'IO16', 'IO17', 'IO18', 'IO8', 'IO3', 'IO46', 'IO9', 'IO10',
       'IO11', 'IO12', 'IO13', 'IO14']
S3R = ['IO43', 'IO44', 'IO1', 'IO2', 'IO42', 'IO41', 'IO40', 'IO39', 'IO38', 'IO37', 'IO36', 'IO35', 'IO0', 'IO45',
       'IO48', 'IO47', 'IO21', 'IO20', 'IO19']
box_sym('ESP32-S3', [(n, n) for n in S3L], [(n, n) for n in S3R], [('3V3', '3V3'), ('5V', '5V')],
        [('GND', 'GND')], title_w=16)
box_sym('ADuM1201', [('A0', 'A0'), ('B1', 'B1')], [('A1', 'A1'), ('B0', 'B0')], [('U1', 'U1'), ('U2', 'U2')],
        [('G1', 'G1'), ('G2', 'G2')], title_w=9)
box_sym('RS485', [('TXD', 'TX'), ('RXD', 'RX')], [('A', 'A'), ('B', 'B')], [('VCC', 'VCC'), ('EN', 'EN')],
        [('GND', 'GND')], title_w=9)
box_sym('TJA1050', [('RX', 'RX'), ('TX', 'TX')], [('CANH', 'CANH'), ('CANL', 'CANL')], [('VCC', 'VCC')],
        [('GND', 'GND')], title_w=8)
box_sym('B0505S', [('2', '+VIN'), None, None, ('1', 'GND')], [('4', '+VO'), None, None, ('3', '0V')], title_w=0)
SCL = [('CS', 'CS'), ('SI', 'SI'), ('SO', 'SO'), ('SCLK', 'SCLK'), ('IRQ', 'IRQ'), None, ('RESET', 'RESET'),
       ('SPI', 'I2C/SPI')]
SCR = [('TXA', 'TXA'), ('RXA', 'RXA'), ('RTSA', 'RTSA'), ('CTSA', 'CTSA'), ('TXB', 'TXB'), ('RXB', 'RXB'),
       ('RTSB', 'RTSB'), ('CTSB', 'CTSB')] + [(f'GP{i}', f'GP{i}') for i in range(8)]
box_sym('SC16IS752', SCL, SCR, [('VCC', 'VCC')], [('GND', 'GND'), ('VSS', 'VSS')], title_w=10)
box_sym('W5500', [('CS', 'CS'), ('SCK', 'SCK'), ('MOSI', 'MOSI'), ('MISO', 'MISO'), ('RST', 'RST'),
                  ('INT', 'INT')], [('NC', 'NC')], [('3V3', '3V3')], [('GND', 'GND')], title_w=7)
box_sym('CMT2300A', [('11', 'SCLK'), ('12', 'SDIO'), ('13', 'CSB'), ('14', 'FCSB')],
        [('5', ''), ('6', ''), ('7', '')], [('8', 'VCC')], [('1', 'GND')], title_w=10, numbers=True,
        stacked={'1': ['2', '3', '4', '9', '10', '15', '16', '18']})
box_sym('USB-C', [('1', 'V'), ('2', 'D-'), ('3', 'D+'), ('4', 'G')], [], title_w=5)
box_sym('RJ45', [(str(i), str(i)) for i in range(1, 9)], [], title_w=5)


def module(ref, sym, x, y, title, sub=None):
    pins = place(ref, sym, x, y)
    setfields([(ref, 0, -1.4), (title, 0, 1.4)] + ([(sub, 0, 4.0)] if sub else []))
    return pins


def short(v):
    v = {'Last, unbestueckt': 'unbest.', 'S1M oder 1N4148W': 'S1M/1N4148W'}.get(v, v)
    for s_ in (' Cin', ' Cout', ' Vin', ' Tantal', ' CMT'):
        v = v.replace(s_, '')
    return v


def rc(ref, sym, x, y, rot=0, mirror=None, side='r'):
    """R, C, D mit Bezeichner und Wert. side: r/l daneben (senkrechtes Bauteil), o darueber (waagerechtes)"""
    v = short(VALUE.get(ref, ''))
    if side == 'o':
        fl = [(ref, 0, -4.3), (v, 0, -2.4)]
    else:
        w = max(len(ref), len(v)) * 0.55 + 2.0
        fx = w if side == 'r' else -w
        fl = [(ref, fx, -1.3), (v, fx, 1.3)]
    pins = place(ref, sym, x, y, rot, mirror, value=v, fields=fl)
    return pins


def fet(ref, sym, x, y, mirror=None):
    p = place(ref, sym, x, y, 0, mirror, value=VALUE[ref])
    setfields([(ref, 6.6, -1.3), (VALUE[ref], 7.6, 1.3)])
    return p


# ---------------------------------------------------------------- Busketten

XC = 170.18          # Pins der Stecker rechts


def chain(y, title, ps, cin, cout, rl, adum, mod, conn, key, note):
    frame(12.7, y - 20.32, 190.5, y + 20.32, title)
    text(note, 14.2, y + 18.8, 1.1)
    # Wandler, Eingangs-C links, Ausgangs-C und Last rechts, direkt verdrahtet
    p = module(ps, 'B0505S', 38.1, y, 'B0505S')
    ci = rc(cin, 'C', 22.86, y, side='l')
    wire(ci['1'], p['2'])
    wire(ci['2'], p['1'])
    power('+5V', ci['1'].p, (0, -1))
    power('GND', ci['2'].p, (0, 1))
    co = rc(cout, 'C', 53.34, y)
    r = rc(rl, 'R', 63.5, y)
    wire(p['4'], co['1'], r['1'])
    wire(p['3'], co['2'], r['2'])
    junction(co['1'])
    junction(co['2'])
    power(f'{key}_5V', r['1'].p, (0, -1))
    power(f'{key}_GND', r['2'].p, (0, 1))
    # ADuM1201
    a = module(adum, 'ADuM1201', 106.68, y, 'ADuM1201')
    for k in ('A0', 'B1', 'U1', 'U2', 'G1', 'G2'):
        S(adum, a, k)
    # Bus-Modul
    if mod == 'U10':
        m = module('U10', 'TJA1050', 144.78, y, 'TJA1050')
        wire(a['A1'], m['RX'])
        wire(a['B0'], m['TX'])
        S('U10', m, 'VCC')
        S('U10', m, 'GND')
        bus = [m['CANH'], m['CANL']]
    else:
        m = module(mod, 'RS485', 144.78, y, 'MAX13487')
        wire(a['A1'], m['TXD'])
        wire(a['B0'], m['RXD'])
        e = S(mod, m, 'VCC')
        en = (m['EN'].x, e[1])
        wire(m['EN'], en, e)
        junction(e)
        S(mod, m, 'GND')
        bus = [m['A'], m['B']]
    # Stecker
    if conn == 'J1':
        c = place('J1', 'RJ45', 0, 0)
        INST.pop()
        c = place('J1', 'RJ45', XC - c['4'].x, bus[0].y - c['4'].y)
        setfields([('J1', 0, -1.4), ('RJ45', 0, 1.4), ('VE.Bus', 0, 4.0)])
        wire(bus[0], c['4'])
        wire(bus[1], c['5'])
        for n in ('1', '2', '6', '7', '8'):
            nc(c[n])
        S('J1', c, '3')
        # X13: Stiftleiste als Alternative zu J1, unter dem Bus-Modul, ueber Netznamen verbunden
        label('VE_A', bus[0].p, (1, 0))
        label('VE_B', bus[1].p, (1, 0))
        x = place('X13', 'Conn_01x03', 124.46, y + 12.7, mirror='y', value='')
        setfields([('X13', -1.0, -4.9), ('VE.Bus alternativ', -1.0, 4.9)])
        for n in '123':
            S('X13', x, n)
    else:
        c = place(conn, 'Conn_01x03', 0, 0)
        INST.pop()
        first = '2'
        c = place(conn, 'Conn_01x03', XC + 5.08, bus[0].y - c[first].y, value='')
        nm = {'X11': 'Pack 1', 'X12': 'Pack 2', 'X5': 'CAN'}[conn]
        cy0 = INST[-1][3]
        setfields([(conn, 1.0, c['1'].y - cy0 - 2.6), (nm, 1.0, c['3'].y - cy0 + 2.8)])
        wire(bus[0], c['2'])
        wire(bus[1], c['3'])
        S(conn, c, '1')


Y0 = 35.56
DY = 43.18
chain(Y0, 'Pack 1, RS485 an UART B des SC16IS752', 'PS1', 'C1', 'C2', 'R1', 'U3', 'U7', 'X11', 'P1',
      'TX des Moduls = RO (Pin 1), RX = DI (Pin 4). R1: Last, unbestueckt, bei Bedarf 250 Ohm.')
chain(Y0 + DY, 'Pack 2, RS485 an UART A des SC16IS752', 'PS2', 'C3', 'C4', 'R2', 'U4', 'U8', 'X12', 'P2',
      'wie Pack 1. R2: Last, unbestueckt.')
chain(Y0 + 2 * DY, 'VE.Bus, RS485 an UART1 des ESP32-S3', 'PS3', 'C5', 'C6', 'R3', 'U5', 'U9', 'J1', 'VE',
      'J1: Pin 4 A, Pin 5 B, Pin 3 GND der VE.Bus-Seite. Pin 2 (+12 V) und die uebrigen offen. R3 unbestueckt.')
chain(Y0 + 3 * DY, 'CAN, TJA1050 an TWAI des ESP32-S3', 'PS4', 'C7', 'C8', 'R4', 'U6', 'U10', 'X5', 'CAN',
      'X5: 1 GND der CAN-Seite, 2 CANH, 3 CANL. R4 unbestueckt.')

# ---------------------------------------------------------------- Stromversorgung

py = 208.28
frame(12.7, py - 15.24, 190.5, py + 38.1, 'Versorgung 5 V und 3,3 V')
x1 = place('X1', 'Conn_01x02', 25.4, py + G, 180, value='')
setfields([('X1', -1.0, -5.0), ('5 V ein', -1.0, 2.6)])
assert PADNET[('X1', '2')] == '+5V' and PADNET[('X1', '1')] == 'GND'
wire(x1['1'], (x1['1'].x + G, x1['1'].y))
power('GND', (x1['1'].x + G, x1['1'].y), (0, 1))
u = place('U13', 'LM1117-3.3', 81.28, py, value='LM1117-3.3')
setfields([('U13', 0, -5.4), ('LM1117-3.3', 0, -3.2)])
c11 = rc('C11', 'C', 63.5, py + 3.81)
wire(x1['2'], (45.72, py), c11['1'], u['3'])
junction((45.72, py))
junction(c11['1'])
power('+5V', (45.72, py), (0, -1))
power('GND', c11['2'].p, (0, 1))
power('GND', u['1'].p, (0, 1))
c12 = rc('C12', 'C_Polarized', 99.06, py + 3.81)
jp = place('JP1', 'SolderJumper_3_Open', 124.46, py, mirror='y', value='')
setfields([('JP1', 0, -3.4)])
wire(u['2'], c12['1'], (109.22, py), jp['3'])
junction(c12['1'])
junction((109.22, py))
power('3V3_LDO', (109.22, py), (0, -1))
power('GND', c12['2'].p, (0, 1))
wire(jp['1'], (jp['1'].x + 2 * G, jp['1'].y))
power('3V3_ESP', (jp['1'].x + 2 * G, jp['1'].y), (0, -1))
wire(jp['2'], (jp['2'].x, py + 7.62), (jp['2'].x + 4 * G, py + 7.62))
power('+3V3', (jp['2'].x + 4 * G, py + 7.62), (0, -1))
text('3V3_ESP: Ausgang des Reglers auf dem S3-Board.  +3V3: alle Module und Stecker.', 14.2, py + 19.05, 1.27)
text('JP1 1-2: der Regler des S3-Boards versorgt alles. U13, C11, C12 bleiben unbestueckt.', 14.2, py + 22.86, 1.27)
text('JP1 2-3: U13 versorgt +3V3, das S3-Board nur sich selbst.', 14.2, py + 26.67, 1.27)
text('Nie alle drei Pads verbinden.', 14.2, py + 30.48, 1.27, True)
text('C12: 10 uF Tantal, ESR 0,3 bis 22 Ohm (Datenblatt TI).', 14.2, py + 34.29, 1.27)

# ---------------------------------------------------------------- ESP32-S3

frame(195.58, 15.24, 279.4, 127.0, 'ESP32-S3 N16R8, eingeloetet')
e = module('U1', 'ESP32-S3', 236.22, 71.12, 'ESP32-S3 N16R8', 'Pins wie Stiftleisten')
for n in S3L + S3R + ['3V3', '5V', 'GND']:
    S('U1', e, n)
text('USB-OTG-Bruecke auf dem Board offen lassen.', 197.12, 124.46, 1.1)

# ---------------------------------------------------------------- SPI: SC16IS752 und W5500

frame(284.48, 15.24, 408.94, 106.68, 'SPI: SC16IS752 (2 UART) und W5500 (LAN)')
s = module('U2', 'SC16IS752', 320.04, 63.5, 'SC16IS752', 'CJMCU-752')
for n in ('CS', 'SI', 'SO', 'SCLK', 'IRQ', 'RESET', 'SPI', 'VCC', 'GND', 'VSS') + tuple(x[0] for x in SCR):
    S('U2', s, n)
text('VCC an 5 V, Regler auf dem Modul. I2C/SPI an GND = SPI.', 286.0, 104.14, 1.1)
w = module('U12', 'W5500', 381.0, 45.72, 'W5500', 'USR-ES1')
for n in ('CS', 'SCK', 'MOSI', 'MISO', 'RST', 'INT', 'NC', '3V3', 'GND'):
    S('U12', w, n)
text('MISO hochohmig, solange CS high.', 358.14, 71.12, 1.1)

# ---------------------------------------------------------------- Funk

frame(284.48, 111.76, 408.94, 160.02, 'Funk 868 MHz: CMT2300A (SPI 3-Draht)')
cm = module('U11', 'CMT2300A', 327.66, 137.16, 'CMT2300A', '10298-V1.0')
for n in ('11', '12', '13', '14', '5', '6', '7', '1'):
    S('U11', cm, n)
top = (cm['8'].x, cm['8'].y - G)
c10 = rc('C10', 'C', 350.52, top[1] + 3.81)
wire(cm['8'], top, c10['1'])
junction(top)
power('+3V3', top, (0, -1))
power('GND', c10['2'].p, (0, 1))
text('C10 direkt an Pad 8 (VCC).', 340.36, 152.4, 1.1)
text('Antenne an der IPX-Buchse des Moduls.', 340.36, 156.21, 1.1)

# ---------------------------------------------------------------- Sensoren, Anzeige

frame(195.58, 132.08, 279.4, 198.12, 'Sensoren und E-Paper')
for ref, x, y, title in (('X2', 220.98, 144.78, 'MT681'), ('X3', 220.98, 162.56, 'BME280 1'),
                         ('X4', 220.98, 180.34, 'BME280 2')):
    c = place(ref, 'Conn_01x04', x, y, value='')
    setfields([(ref, 1.0, -4.9), (title, 1.0, 9.9)])
    for n in '1234':
        S(ref, c, n)
c = place('X10', 'Conn_01x08', 264.16, 152.4, value='')
setfields([('X10', 1.0, -10.0), ('E-Paper', 1.0, 15.1)])
for n in '12345678':
    S('X10', c, n)

# ---------------------------------------------------------------- Relaistreiber


def relay(ox, oy, ref_r, ref_rg, ref_q, ref_d, ref_x, title, name):
    frame(ox - 3.81, oy - 17.78, ox + 50.8, oy + 25.4, title)
    r = rc(ref_r, 'R', ox + 7.62, oy + 10.16, 90, side='o')
    q_ = fet(ref_q, 'Q_NMOS_GSD', ox + 20.32, oy + 10.16)
    rg = rc(ref_rg, 'R', ox + 12.7, oy + 13.97, 180, side='l')
    S(ref_r, r, '1')
    wire(r['2'], rg['2'], q_['1'])
    junction(rg['2'])
    power('GND', rg['1'].p, (0, 1))
    power('GND', q_['2'].p, (0, 1))
    d = rc(ref_d, 'D', q_['3'].x, oy - 3.81, 270, side='l')
    x = place(ref_x, 'Conn_01x02', ox + 38.1, oy - 2.54, value='')
    setfields([(ref_x, 1.0, -3.4), (name, 1.0, 5.6)])
    wire(q_['3'], d['2'], x['2'])
    junction(d['2'])
    power('+5V', d['1'].p, (0, -1))
    wire(x['1'], (x['1'].x - G, x['1'].y))
    power('+5V', (x['1'].x - G, x['1'].y), (0, -1))


relay(287.02, 182.88, 'R5', 'R6', 'Q1', 'D1', 'X6', 'Luefter (X6), 12 V an Relais', 'Luefter')
relay(347.98, 182.88, 'R7', 'R8', 'Q2', 'D2', 'X7', 'Relais 2 (X7)', 'Relais 2')

# ---------------------------------------------------------------- 5-V-Schalter fuer X8


def usb(ux, uy):
    frame(ux - 3.81, uy - 7.62, ux + 66.04, uy + 33.02, '5-V-Schalter und USB-C fuer den CH348')
    r9 = rc('R9', 'R', ux + 7.62, uy + 20.32, 90, side='o')
    q4 = fet('Q4', 'Q_NMOS_GSD', ux + 20.32, uy + 20.32)
    r10 = rc('R10', 'R', ux + 12.7, uy + 24.13, 180, side='l')
    S('R9', r9, '1')
    wire(r9['2'], r10['2'], q4['1'])
    junction(r10['2'])
    power('GND', r10['1'].p, (0, 1))
    power('GND', q4['2'].p, (0, 1))
    r11 = rc('R11', 'R', q4['3'].x, uy + 11.43, 180, side='l')
    r12 = rc('R12', 'R', q4['3'].x, uy + 3.81, 0, side='l')
    power('+5V', r12['1'].p, (0, -1))
    q3 = fet('Q3', 'Q_PMOS_GSD', ux + 35.56, uy + 7.62, mirror='x')
    c9 = rc('C9', 'C', ux + 27.94, uy + 11.43, side='r')
    wire(r11['2'], c9['1'], q3['1'])
    junction(r11['2'])
    junction(c9['1'])
    power('+5V', q3['2'].p, (0, -1))
    node = (q3['3'].x, c9['2'].y)
    x8 = module('X8', 'USB-C', 0, 0, 'USB-C')
    INST.pop()
    x8 = module('X8', 'USB-C', ux + 55.88, node[1] - x8['1'].y, 'USB-C', 'CH348')
    wire(c9['2'], node, x8['1'])
    wire(q3['3'], node)
    junction(node)
    for n in ('2', '3', '4'):
        S('X8', x8, n)


usb(200.66, 213.36)

# ---------------------------------------------------------------- Datei schreiben

lib_syms = ['lib_symbols'] + list(LIBSYMS.values())
out = ['kicad_sch', ['version', '20230121'], ['generator', 'eeschema'], ['uuid', ROOT], ['paper', '"A3"'],
       ['title_block', ['title', '"ESP_venus v1, Platine klein, USB frei"'], ['date', '"2026-11-17"'],
        ['rev', '"1"'], ['company', '"Ruehle  pillepalle127"'],
        ['comment', '1', '"Erzeugt mit gen_kicad_sch.py aus esp_venus_klein_usbfrei.kicad_pcb"']],
       lib_syms]
out += ITEMS
for ref, sym, x, y, rot, mirror, value, fields, show_value in INST:
    pins = PINS[sym]
    eff = ['effects', ['font', ['size', '1.27', '1.27']]]
    hidden = eff + ['hide']
    props = []
    pwr = ref.startswith('#')
    fl = fields or []
    # Text waagerecht: Winkel des Feldes = Drehung des Symbols
    fa = str(rot % 180)
    if pwr:
        props.append(['property', '"Reference"', q(ref), ['at', f(x), f(y), '0'], hidden])
        t, dx, dy = fl[0]
        props.append(['property', '"Value"', q(value), ['at', f(x + dx), f(y + dy), fa], eff])
    else:
        rt = fl[0] if fl else (ref, 0, -3)
        props.append(['property', '"Reference"', q(ref), ['at', f(x + rt[1]), f(y + rt[2]), fa], eff])
        if len(fl) > 1:
            vt = fl[1]
            props.append(['property', '"Value"', q(vt[0]), ['at', f(x + vt[1]), f(y + vt[2]), fa], eff])
        else:
            props.append(['property', '"Value"', q(value), ['at', f(x), f(y), fa], hidden])
        for k, extra in enumerate(fl[2:]):
            props.append(['property', q(f'Info{k}'), q(extra[0]), ['at', f(x + extra[1]), f(y + extra[2]), fa],
                          eff])
    props.append(['property', '"Footprint"', '""', ['at', f(x), f(y), '0'], hidden])
    props.append(['property', '"Datasheet"', '""', ['at', f(x), f(y), '0'], hidden])
    at = ['at', f(x), f(y), str(rot)]
    s = ['symbol', ['lib_id', q(f'{LIBNAME}:{sym}')], at] + ([['mirror', mirror]] if mirror else []) + \
        [['unit', '1'], ['in_bom', 'no' if pwr else 'yes'], ['on_board', 'no' if pwr else 'yes'], ['dnp', 'no'],
         ['uuid', uid()]] + props + [['pin', q(n), ['uuid', uid()]] for n in pins] + \
        [['instances', ['project', '"schaltplan_usbfrei"',
                        ['path', q('/' + ROOT), ['reference', q(ref)], ['unit', '1']]]]]
    out.append(s)
out.append(['sheet_instances', ['path', '"/"', ['page', '"1"']]])


def pretty(x, ind=0):
    if not isinstance(x, list):
        return x
    if x and x[0] in ('symbol', 'kicad_sch', 'lib_symbols') and any(isinstance(i, list) for i in x):
        head = [i for i in x if not isinstance(i, list)]
        return '(' + ' '.join(head) + '\n' + '\n'.join(' ' * (ind + 2) + pretty(i, ind + 2)
                                                         for i in x if isinstance(i, list)) + '\n' + ' ' * ind + ')'
    return dump(x)


open(OUT, 'w').write(pretty(out) + '\n')
print(OUT, len(INST), 'Symbole', len(ITEMS), 'Elemente')

# ---------------------------------------------------------------- Pruefung gegen die Platine

with tempfile.TemporaryDirectory() as td:
    net = os.path.join(td, 'x.net')
    subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', net, OUT],
                   check=True, capture_output=True)
    nl = parse(open(net).read())[0]
nets = next(i for i in nl if isinstance(i, list) and i[0] == 'nets')
sch_groups = {}
for n in nets[1:]:
    nodes = [i for i in n if isinstance(i, list) and i[0] == 'node']
    name = next(i for i in n if isinstance(i, list) and i[0] == 'name')[1].strip('"')
    pins = frozenset((next(j for j in nd if j[0] == 'ref')[1].strip('"'),
                      next(j for j in nd if j[0] == 'pin')[1].strip('"')) for nd in nodes)
    if len(pins) > 1:
        sch_groups[pins] = name
pcb_groups = {}
for (r, pn), nn in PADNET.items():
    if nn and not r.startswith('H'):
        pcb_groups.setdefault(nn, set()).add((r, pn))
pcb_sets = {frozenset(v): k for k, v in pcb_groups.items() if len(v) > 1}
ok = True
for sset, name in pcb_sets.items():
    if sset not in sch_groups:
        ok = False
        best = max(sch_groups, key=lambda g: len(g & sset))
        print('ABWEICHUNG', name, 'Platine', sorted(sset - best), 'Schaltplan zusaetzlich', sorted(best - sset))
for sset, name in sch_groups.items():
    if sset not in pcb_sets:
        ok = False
        print('NUR IM SCHALTPLAN', name, sorted(sset))
print('Netze Platine', len(pcb_sets), 'Schaltplan', len(sch_groups), 'gleich' if ok else 'VERSCHIEDEN')

subprocess.run(['kicad-cli', 'sch', 'export', 'pdf', '-o', PDF, OUT], check=True, capture_output=True)
print(PDF)
sys.exit(0 if ok else 1)
