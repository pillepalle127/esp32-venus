#!/usr/bin/env python3
# Einfache 3D-Modelle (VRML) fuer alle Bauteile der Platine, im Projektordner unter 3d/.
# KiCad findet sie ueber ${KIPRJMOD}, unabhaengig von Version und installierter 3D-Bibliothek.
# Aendert nur die 3D-Zuordnung der Footprints, nicht das Kupfer.
# Aufruf: python3 modelle_3d.py esp_venus_klein_usbfrei.kicad_pcb [vorschau.png]
#
# Hoehen sind geschaetzt, ausser dem Abstand der Module zur Platine (Kunststoff der Stiftleiste, 2,5 mm).
import math
import os
import sys

import pcbnew

PCB = sys.argv[1]
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
HERE = os.path.dirname(os.path.abspath(PCB))
OUTDIR = os.path.join(HERE, '3d')
os.makedirs(OUTDIR, exist_ok=True)
T = pcbnew.ToMM

# ---------------------------------------------------------------- Hoehen in mm (geschaetzt, siehe docs/platine.md)
STANDOFF = 2.5        # Kunststoff der Stiftleiste unter einem Modul
MOD_PCB = 1.6         # Leiterplatte eines Moduls
PIN_BELOW = 3.0       # Stift unter der Platine
HEADER_PIN = 8.5      # Stift einer Stiftleiste ueber der Platine
IC_SOIC = 1.6
WROOM = 3.2
USBC = 3.3
RJ45_W5500 = 13.5
RJ45_J1 = 13.5
B0505S = 10.2
CMT_PCB = 1.0

COL = {
    'pcb_mod': (0.05, 0.25, 0.55), 'black': (0.08, 0.08, 0.08), 'gold': (0.85, 0.68, 0.25),
    'metal': (0.75, 0.75, 0.78), 'cap': (0.72, 0.55, 0.35), 'res': (0.12, 0.12, 0.12),
    'tant': (0.85, 0.55, 0.05), 'plastic': (0.15, 0.15, 0.15), 'b0505': (0.1, 0.1, 0.12),
    'cmt': (0.1, 0.45, 0.25), 'rj': (0.7, 0.7, 0.72),
}


class Model:
    def __init__(self):
        self.boxes = []    # (x0, y0, z0, x1, y1, z1, farbe), lokale Koordinaten, y nach unten

    def box(self, x0, y0, z0, x1, y1, z1, col):
        self.boxes.append((min(x0, x1), min(y0, y1), min(z0, z1), max(x0, x1), max(y0, y1), max(z0, z1), col))

    def wrl(self):
        out = ['#VRML V2.0 utf8', '# esp_venus, vereinfachtes Modell, Hoehen geschaetzt']
        k = 1 / 2.54     # KiCad: 1 VRML-Einheit = 0,1 Zoll
        for x0, y0, z0, x1, y1, z1, col in self.boxes:
            pts = [(x, -y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
            faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
            c = COL[col]
            out.append('Shape { appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f '
                       'specularColor 0.2 0.2 0.2 shininess 0.3 } }' % c)
            out.append(' geometry IndexedFaceSet { solid FALSE coord Coordinate { point [ ' +
                       ', '.join('%.4f %.4f %.4f' % (x * k, y * k, z * k) for x, y, z in pts) + ' ] }')
            out.append('  coordIndex [ ' + ' '.join('%d %d %d %d -1' % f for f in faces) + ' ] } }')
        return '\n'.join(out) + '\n'


def local(fp, gx, gy):
    px, py = T(fp.GetPosition().x), T(fp.GetPosition().y)
    a = math.radians(fp.GetOrientationDegrees())
    dx, dy = gx - px, gy - py
    return (dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a))


def pads(fp):
    res = []
    for p in fp.Pads():
        x, y = local(fp, T(p.GetPosition().x), T(p.GetPosition().y))
        res.append((x, y, p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH))
    return res


def layer_bbox(fp, layer):
    """Umriss: kleinstes Rechteck um alle Linien einer Lage, lokal"""
    pts = []
    for g in fp.GraphicalItems():
        if g.GetLayer() == layer and isinstance(g, pcbnew.FP_SHAPE):
            pts.append(local(fp, T(g.GetStart().x), T(g.GetStart().y)))
            pts.append(local(fp, T(g.GetEnd().x), T(g.GetEnd().y)))
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


P = 2.54
# Lage markanter Teile auf den Modulen, lokal zum Footprint, dieselben Werte wie in gen_platine.py
ESP_WROOM = (3.7, -8.145, 21.7, -8.145 + 25.5)          # WROOM 18 x 25,5 mm, ragt 6,24 mm ueber
ESP_USB = [(1.7, 48.2, 10.7, 55.245), (14.7, 48.2, 23.7, 55.245)]
W5500_RJ45 = (2.5, 2.1, 5 * P + 8.9, 18.2)
USBC_X0 = 1.5 * P - 6.0
USBC_JACK = (USBC_X0 + 1.5, 6.0, USBC_X0 + 10.5, 13.4)


def module(fp, m, ic=(5.0, 4.0), extra=()):
    """Modul auf Stiftleisten: Kunststoff, Stifte, Leiterplatte, IC in der Mitte"""
    x0, y0, x1, y1 = layer_bbox(fp, pcbnew.F_Fab)
    zb = STANDOFF
    zt = zb + MOD_PCB
    m.box(x0, y0, zb, x1, y1, zt, 'pcb_mod')
    for x, y, tht in pads(fp):
        if tht:
            m.box(x - 1.27, y - 1.27, 0, x + 1.27, y + 1.27, STANDOFF, 'plastic')
            m.box(x - 0.32, y - 0.32, -PIN_BELOW, x + 0.32, y + 0.32, zt + 0.8, 'gold')
    if ic:
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        m.box(cx - ic[0] / 2, cy - ic[1] / 2, zt, cx + ic[0] / 2, cy + ic[1] / 2, zt + IC_SOIC, 'black')
    for (ex0, ey0, ex1, ey1, h, col) in extra:
        m.box(ex0, ey0, zt, ex1, ey1, zt + h, col)


def header_model(fp, m):
    for x, y, _ in pads(fp):
        m.box(x - 1.27, y - 1.27, 0, x + 1.27, y + 1.27, 2.5, 'plastic')
        m.box(x - 0.32, y - 0.32, -PIN_BELOW, x + 0.32, y + 0.32, HEADER_PIN, 'gold')


def build(fp):
    name = str(fp.GetFPID().GetLibItemName())
    m = Model()
    if name == 'ESP32-S3_N16R8_2xUSB-C':
        extra = [ESP_WROOM + (WROOM, 'metal')] + [u + (USBC, 'metal') for u in ESP_USB]
        module(fp, m, ic=None, extra=extra)
    elif name == 'CJMCU-752':
        module(fp, m, ic=(9.7, 4.4))
    elif name in ('ADuM1201_Modul', 'RS485_MAX13487_klein', 'TJA1050_Modul'):
        module(fp, m, ic=(4.9, 3.9))
    elif name == 'W5500_USR-ES1':
        module(fp, m, ic=None, extra=[W5500_RJ45 + (RJ45_W5500, 'rj')])
    elif name == 'USB-C_Modul_12x13.8':
        module(fp, m, ic=None, extra=[USBC_JACK + (USBC, 'metal')])
    elif name == 'CMT2300A_10298-V1.0':
        x0, y0, x1, y1 = layer_bbox(fp, pcbnew.F_Fab)
        m.box(x0, y0, 0, x1, y1, CMT_PCB, 'cmt')
        m.box(-2.5, -2.5, CMT_PCB, 2.5, 2.5, CMT_PCB + 0.9, 'black')
    elif name == 'B0505S-1WR3':
        x0, y0, x1, y1 = layer_bbox(fp, pcbnew.F_Fab)
        m.box(x0, y0, 0.5, x1, y1, B0505S, 'b0505')
        for x, y, _ in pads(fp):
            m.box(x - 0.3, y - 0.15, -PIN_BELOW, x + 0.3, y + 0.15, 0.5, 'gold')
    elif name.startswith('PinHeader_1x'):
        header_model(fp, m)
    elif name.startswith('RJ45_Amphenol'):
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        pts = [local(fp, T(bb.GetLeft()), T(bb.GetTop())), local(fp, T(bb.GetRight()), T(bb.GetBottom()))]
        m.box(pts[0][0], pts[0][1], 0, pts[1][0], pts[1][1], RJ45_J1, 'rj')
    elif name.startswith('R_0805'):
        m.box(-1.0, -0.62, 0, 1.0, 0.62, 0.5, 'res')
    elif name.startswith('C_0805'):
        m.box(-1.0, -0.62, 0, 1.0, 0.62, 1.0, 'cap')
    elif name.startswith('CP_EIA-3528'):
        m.box(-1.75, -1.4, 0, 1.75, 1.4, 1.9, 'tant')
        m.box(-1.75, -1.4, 1.9, -1.1, 1.4, 1.95, 'black')    # Plusmarkierung an Pad 1
    elif name.startswith('SOT-23'):
        m.box(-0.65, -1.45, 0.05, 0.65, 1.45, 1.05, 'black')
    elif name.startswith('SOT-223'):
        m.box(-1.75, -3.25, 0.05, 1.75, 3.25, 1.65, 'black')
        m.box(1.75, -1.5, 0, 3.6, 1.5, 0.25, 'metal')
        for y in (-2.3, 0, 2.3):
            m.box(-3.6, y - 0.35, 0, -1.75, y + 0.35, 0.25, 'metal')
    elif name == 'D_SMA_SOD-123_kombiniert':
        m.box(-2.15, -1.3, 0, 2.15, 1.3, 2.1, 'black')
        m.box(-2.15, -1.3, 2.1, -1.5, 1.3, 2.15, 'metal')   # Kathodenring an Pad 1
    else:
        return None
    return m


board = pcbnew.LoadBoard(PCB)
done, skipped = [], []
ALL = []
for fp in board.GetFootprints():
    ref = fp.GetReference()
    m = build(fp)
    models = fp.Models()
    models.clear()
    if m is None:
        skipped.append(ref)
        continue
    fn = f'{ref}.wrl'
    open(os.path.join(OUTDIR, fn), 'w').write(m.wrl())
    md = pcbnew.FP_3DMODEL()
    md.m_Filename = '${KIPRJMOD}/3d/' + fn
    models.push_back(md)
    done.append(ref)
    # fuer die Vorschau in Platinenkoordinaten
    px, py = T(fp.GetPosition().x), T(fp.GetPosition().y)
    a = math.radians(fp.GetOrientationDegrees())
    for x0, y0, z0, x1, y1, z1, col in m.boxes:
        corners = []
        for (lx, ly) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            gx = px + lx * math.cos(a) + ly * math.sin(a)
            gy = py - lx * math.sin(a) + ly * math.cos(a)
            corners.append((gx, gy))
        ALL.append((corners, z0, z1, col))
board.Save(PCB)
print(len(done), 'Modelle in', OUTDIR, '| ohne Modell:', ' '.join(sorted(skipped)))

# ---------------------------------------------------------------- Vorschau (eigener kleiner Renderer)
if PREVIEW:
    from PIL import Image, ImageDraw
    W_, H_ = 100.0, 100.0
    az, el = math.radians(-35), math.radians(40)
    S = 13.0

    def proj(x, y, z):
        # Platine: x rechts, y nach unten (zum Betrachter), z nach oben
        X = x * math.cos(az) - y * math.sin(az)
        Y = x * math.sin(az) + y * math.cos(az)
        sx = X
        sy = Y * math.sin(el) - z * math.cos(el)
        depth = Y * math.cos(el) + z * math.sin(el)
        return sx, sy, depth
    groups = []      # (Tiefe, [(Polygon, Farbe)]), hinten zuerst

    def face(poly3, col, shade):
        pp = [proj(*p) for p in poly3]
        return (sum(p[2] for p in pp) / len(pp), [(p[0], p[1]) for p in pp],
                tuple(int(255 * min(1, c * shade)) for c in col))
    # Platine zuerst, alles andere liegt darueber
    groups.append((-1e9, [face([(0, 0, 0), (W_, 0, 0), (W_, H_, 0), (0, H_, 0)], (0.1, 0.45, 0.2), 1.0),
                          face([(0, H_, 0), (W_, H_, 0), (W_, H_, -1.6), (0, H_, -1.6)], (0.1, 0.4, 0.2), 0.7),
                          face([(W_, 0, 0), (W_, H_, 0), (W_, H_, -1.6), (W_, 0, -1.6)], (0.1, 0.4, 0.2), 0.55),
                          face([(0, 0, 0), (0, H_, 0), (0, H_, -1.6), (0, 0, -1.6)], (0.1, 0.4, 0.2), 0.55)], None))
    for corners, z0, z1, col in ALL:
        if z1 <= 0:
            continue
        z0 = max(z0, 0)
        c = COL[col]
        sides = []
        for i in range(4):
            (xa, ya), (xb, yb) = corners[i], corners[(i + 1) % 4]
            nx, ny = yb - ya, -(xb - xa)
            sh = 0.55 + 0.35 * abs(nx) / (abs(nx) + abs(ny) + 1e-9)
            sides.append(face([(xa, ya, z0), (xb, yb, z0), (xb, yb, z1), (xa, ya, z1)], c, sh))
        sides.sort(key=lambda f: f[0])
        top = face([(x, y, z1) for x, y in corners], c, 1.0)
        cx = sum(x for x, _ in corners) / 4
        cy = sum(y for _, y in corners) / 4
        xs_ = [x for x, _ in corners]
        ys_ = [y for _, y in corners]
        groups.append((proj(cx, cy, (z0 + z1) / 2)[2], sides + [top], (min(xs_), min(ys_), max(xs_), max(ys_), z0, z1)))
    board_g = groups[0]
    rest = sorted(groups[1:], key=lambda g: g[0])
    # Was auf einem anderen Teil liegt, kommt danach (sonst deckt z. B. die Modulplatine die WROOM-Abdeckung zu)
    for a_ in sorted(rest, key=lambda g: g[2][4]):
        ia = rest.index(a_)
        x0a, y0a, x1a, y1a, z0a, _ = a_[2]
        last = -1
        for j, b_ in enumerate(rest):
            x0b, y0b, x1b, y1b, _, z1b = b_[2]
            if b_ is not a_ and z1b <= z0a + 0.01 and x0a < x1b and x0b < x1a and y0a < y1b and y0b < y1a:
                last = max(last, j)
        if last > ia:
            rest.pop(ia)
            rest.insert(last, a_)
    groups = [board_g] + rest
    xs = [p[0] for g in groups for f in g[1] for p in f[1]]
    ys = [p[1] for g in groups for f in g[1] for p in f[1]]
    mx, my = min(xs), min(ys)
    img = Image.new('RGB', (int((max(xs) - mx) * S) + 40, int((max(ys) - my) * S) + 40), (245, 245, 245))
    dr = ImageDraw.Draw(img)
    for _, fs, _b in groups:
        for d, pts, col in fs:
            dr.polygon([((x - mx) * S + 20, (y - my) * S + 20) for x, y in pts], fill=col,
                       outline=tuple(max(0, c - 40) for c in col))
    img.save(PREVIEW)
    print(PREVIEW)
