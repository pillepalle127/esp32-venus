#!/usr/bin/env python3
# Legt die Leiterbahnen der Platine: Wegesuche (A*) auf einem Raster von 0,3175 mm, zwei Lagen.
# Danach fuellt KiCad die GND-Flaechen und prueft das Ergebnis (DRC), der Bericht steht in <Platine>_drc.txt.
#
# Aufruf: python3 route.py [Platine.kicad_pcb]
# Vorher gen_platine.py laufen lassen. Braucht pcbnew (KiCad 7) und numpy.
import heapq
import json
import math
import os
import sys
import time

import numpy as np
import pcbnew
from pcbnew import FromMM, ToMM, VECTOR2I

HERE = os.path.dirname(os.path.abspath(__file__))
# --seed N: andere Reihenfolge fuer gleich lange Netze. Hilft, wenn ein Lauf Netze offen laesst.
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
SEED = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else None
if SEED is not None:
    ARGS = [a for a in ARGS if a != str(SEED)] or ARGS
PCB = ARGS[0] if ARGS else os.path.join(HERE, 'esp_venus_klein_usbfrei.kicad_pcb')

G = 0.3175                  # Raster
MARGIN = 0.05               # Zuschlag auf den Abstand der Netzklasse
EDGE = 0.5                  # Kupfer bis Platinenrand
VIA_R, VIA_DRILL = 0.4, 0.4
VIA_COST = 40.0             # in Rasterschritten
HOLE_KEEP = 3.3             # um Loecher fuer M3 bleibt Platz fuer Kopf, Mutter oder Abstandshalter
BOTTOM = 1.6                # unten teurer: Die Unterseite soll moeglichst GND-Flaeche bleiben

board = pcbnew.LoadBoard(PCB)
old_tracks = list(board.GetTracks())    # Liste behalten, sonst raeumt Python die Objekte zu frueh ab
for t in old_tracks:
    board.RemoveNative(t)

# Platinenrand: Der Ursprung liegt links oben in der Ecke
W = max(ToMM(max(d.GetStart().x, d.GetEnd().x)) for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts)
H = max(ToMM(max(d.GetStart().y, d.GetEnd().y)) for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts)
NX, NY = int(W / G) + 1, int(H / G) + 1
XS = np.arange(NX) * G
YS = np.arange(NY) * G

pro = json.load(open(PCB.replace('.kicad_pcb', '.kicad_pro')))
CLASS = {c['name']: c for c in pro['net_settings']['classes']}


def cls(net):
    if not net:
        return CLASS['Default']
    return CLASS[board.FindNet(net).GetNetClassName()]


def clearance(a, b):
    return max(cls(a)['clearance'], cls(b)['clearance']) + MARGIN


# ---------------------------------------------------------------- Hindernisse
# Jedes Hindernis: Form, Lagen (Bit 0 oben, Bit 1 unten), Netz
obst = []        # ('c', x, y, r) | ('r', x, y, hw, hh) | ('s', x1, y1, x2, y2, r), layers, net
pads = {}        # Netz -> Liste von Pads
smd_own = {}


class Pad:
    def __init__(self, p, ref):
        self.ref = ref
        self.name = p.GetNumber()
        self.x, self.y = ToMM(p.GetPosition().x), ToMM(p.GetPosition().y)
        w, h = ToMM(p.GetSize().x), ToMM(p.GetSize().y)
        rot = round(p.GetOrientationDegrees()) % 180
        if rot == 90:
            w, h = h, w
        self.layers = (1 if p.IsOnLayer(pcbnew.F_Cu) else 0) | (2 if p.IsOnLayer(pcbnew.B_Cu) else 0)
        self.tht = p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH
        self.drill = ToMM(p.GetDrillSize().x) if self.tht else 0.0
        round_ = p.GetShape() in (pcbnew.PAD_SHAPE_CIRCLE, pcbnew.PAD_SHAPE_OVAL) and abs(w - h) < 0.01
        self.shape = ('c', self.x, self.y, w / 2) if round_ else ('r', self.x, self.y, w / 2, h / 2)
        self.net = p.GetNetname()

    def inside(self, shrink=0.05):
        """Rasterpunkte im Kupfer des Pads"""
        s = self.shape
        out = []
        r = (s[3] if s[0] == 'c' else max(s[3], s[4])) + G
        for iy in range(max(0, int((self.y - r) / G)), min(NY, int((self.y + r) / G) + 2)):
            for ix in range(max(0, int((self.x - r) / G)), min(NX, int((self.x + r) / G) + 2)):
                dx, dy = ix * G - self.x, iy * G - self.y
                if s[0] == 'c':
                    ok = math.hypot(dx, dy) <= s[3] - shrink
                else:
                    ok = abs(dx) <= s[3] - shrink and abs(dy) <= s[4] - shrink
                if ok:
                    out.append((ix, iy))
        return out


# KiCad speichert die Bauteile in zufaelliger Reihenfolge (nach UUID). Hier fest sortieren,
# damit derselbe Entwurf immer dasselbe Ergebnis gibt.
for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
    for p in sorted(fp.Pads(), key=lambda q: (q.GetNumber(), q.GetPosition().x, q.GetPosition().y)):
        if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            d = ToMM(p.GetDrillSize().x)
            r = HOLE_KEEP if d >= 3.0 else d / 2
            obst.append((('c', ToMM(p.GetPosition().x), ToMM(p.GetPosition().y), r), 3, None))
            continue
        pad = Pad(p, fp.GetReference())
        obst.append((pad.shape, pad.layers, pad.net))
        if pad.net:
            pads.setdefault(pad.net, []).append(pad)

keepouts = []    # (x1, y1, x2, y2, layers)
for z in board.Zones():
    if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
        b = z.GetBoundingBox()
        ly = (1 if z.IsOnLayer(pcbnew.F_Cu) else 0) | (2 if z.IsOnLayer(pcbnew.B_Cu) else 0)
        keepouts.append((ToMM(b.GetLeft()), ToMM(b.GetTop()), ToMM(b.GetRight()), ToMM(b.GetBottom()), ly))


def raster(mask, shape, layers, d):
    """Sperrt alle Rasterpunkte, die naeher als d an der Form liegen"""
    k = shape[0]
    if k == 'c':
        x1, y1, x2, y2 = shape[1], shape[2], shape[1], shape[2]
        ext = shape[3] + d
    elif k == 'r':
        x1, y1, x2, y2 = shape[1], shape[2], shape[1], shape[2]
        ext = max(shape[3], shape[4]) + d
    else:
        x1, y1, x2, y2 = min(shape[1], shape[3]), min(shape[2], shape[4]), max(shape[1], shape[3]), max(shape[2], shape[4])
        ext = shape[5] + d
    ix1, ix2 = max(0, int((x1 - ext) / G)), min(NX, int((x2 + ext) / G) + 2)
    iy1, iy2 = max(0, int((y1 - ext) / G)), min(NY, int((y2 + ext) / G) + 2)
    if ix1 >= ix2 or iy1 >= iy2:
        return
    X = XS[ix1:ix2][None, :]
    Y = YS[iy1:iy2][:, None]
    if k == 'c':
        hit = (X - shape[1]) ** 2 + (Y - shape[2]) ** 2 < (shape[3] + d) ** 2
    elif k == 'r':
        dx = np.maximum(np.abs(X - shape[1]) - shape[3], 0)
        dy = np.maximum(np.abs(Y - shape[2]) - shape[4], 0)
        hit = dx ** 2 + dy ** 2 < d ** 2
    else:
        ax, ay, bx, by, r = shape[1:]
        vx, vy = bx - ax, by - ay
        L2 = vx * vx + vy * vy
        if L2 == 0:
            hit = (X - ax) ** 2 + (Y - ay) ** 2 < (r + d) ** 2
        else:
            t = np.clip(((X - ax) * vx + (Y - ay) * vy) / L2, 0, 1)
            hit = (X - (ax + t * vx)) ** 2 + (Y - (ay + t * vy)) ** 2 < (r + d) ** 2
    for l in (0, 1):
        if layers & (1 << l):
            mask[l, iy1:iy2, ix1:ix2] |= hit


def build_masks(net):
    """blocked[l, y, x] fuer die Bahn dieses Netzes, viablock[y, x] fuer eine Durchkontaktierung"""
    hw = cls(net)['track_width'] / 2
    blocked = np.zeros((2, NY, NX), dtype=bool)
    viab = np.zeros((2, NY, NX), dtype=bool)
    for m, half in ((blocked, hw), (viab, max(VIA_R, hw))):
        e = int(math.ceil((EDGE + half) / G))
        m[:, :e, :] = True
        m[:, -e:, :] = True
        m[:, :, :e] = True
        m[:, :, -e:] = True
        for x1, y1, x2, y2, ly in keepouts:
            for l in (0, 1):
                if ly & (1 << l):
                    m[l, max(0, int(math.ceil((y1 - half) / G))):int((y2 + half) / G) + 1,
                      max(0, int(math.ceil((x1 - half) / G))):int((x2 + half) / G) + 1] = True
    for shape, layers, onet in obst:
        if onet == net:
            continue
        c = clearance(net, onet)
        raster(blocked, shape, layers, c + hw)
        raster(viab, shape, layers, c + max(VIA_R, hw))
    # keine Durchkontaktierung in eigenen SMD-Pads
    for p in pads[net]:
        if not p.tht:
            raster(viab, p.shape, 3, 0.2)
    # Bohrungen brauchen Abstand zueinander, auch im selben Netz (Platinenregel 0,5 mm)
    for x, y, r in drills:
        raster(viab, ('c', x, y, r), 3, VIA_DRILL / 2 + HOLE_GAP)
    return blocked, viab[0] | viab[1]


# ---------------------------------------------------------------- Wegesuche
DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]


VIA_MIN = int(math.ceil((VIA_DRILL + 0.55) / G))   # Rasterschritte zwischen zwei Bohrungen im selben Weg


def via_near(prev, n):
    """True, wenn der Weg kurz vor n schon die Lage gewechselt hat (zweite Bohrung zu nah)"""
    cur = n
    for _ in range(VIA_MIN + 2):
        p = prev.get(cur)
        if p is None:
            return False
        if p[0] != cur[0] and max(abs(p[1] - n[1]), abs(p[2] - n[2])) < VIA_MIN:
            return True
        cur = p
    return False


def astar(blocked, viaok, layerfree, sources, targets, tx, ty):
    """sources: Menge (l, ix, iy) mit Kosten 0. targets: Menge (l, ix, iy). Liefert Pfad oder None."""
    blk = [blocked[0].tobytes(), blocked[1].tobytes()]
    vok = viaok.tobytes()
    best = {}
    prev = {}
    heap = []

    def h(ix, iy):
        dx, dy = abs(ix - tx), abs(iy - ty)
        return (dx + dy) + (1.4142 - 2) * min(dx, dy)

    for s in sources:
        best[s] = 0.0
        heap.append((h(s[1], s[2]), 0.0, s))
    heapq.heapify(heap)
    while heap:
        f, c, n = heapq.heappop(heap)
        if c > best.get(n, 1e18):
            continue
        if n in targets:
            path = [n]
            while n in prev:
                n = prev[n]
                path.append(n)
            return path[::-1]
        l, ix, iy = n
        b = blk[l]
        for dx, dy, cost in DIRS:
            jx, jy = ix + dx, iy + dy
            if jx < 0 or jy < 0 or jx >= NX or jy >= NY:
                continue
            if b[jy * NX + jx]:
                continue
            if dx and dy and (b[iy * NX + jx] or b[jy * NX + ix]):
                continue
            if l == 1:
                cost *= BOTTOM
            m = (l, jx, jy)
            nc = c + cost
            if nc < best.get(m, 1e18):
                best[m] = nc
                prev[m] = n
                heapq.heappush(heap, (nc + h(jx, jy), nc, m))
        # Lagenwechsel
        o = 1 - l
        i2 = iy * NX + ix
        if not blk[o][i2]:
            if (ix, iy) in layerfree:
                vc = 0.0
            elif not vok[i2] and not via_near(prev, n):
                vc = VIA_COST
            else:
                vc = None
            if vc is not None:
                m = (o, ix, iy)
                nc = c + vc
                if nc < best.get(m, 1e18):
                    best[m] = nc
                    prev[m] = n
                    heapq.heappush(heap, (nc + h(ix, iy), nc, m))
    return None


def connector(a, b, diag_first):
    """Rasterpunkte von a nach b: ein schraeges und ein gerades Stueck"""
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    sx, sy = (dx > 0) - (dx < 0), (dy > 0) - (dy < 0)
    d = min(abs(dx), abs(dy))
    rest = max(abs(dx), abs(dy)) - d
    straight = (sx, 0) if abs(dx) > abs(dy) else (0, sy)
    steps = [(sx, sy)] * d + [straight] * rest if diag_first else [straight] * rest + [(sx, sy)] * d
    pts = [(ax, ay)]
    for ux, uy in steps:
        pts.append((pts[-1][0] + ux, pts[-1][1] + uy))
    return pts


def line_free(blocked_l, pts):
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        if blocked_l[by, bx]:
            return False
        if ax != bx and ay != by and (blocked_l[ay, bx] or blocked_l[by, ax]):
            return False
    return True


def smooth(blocked, path):
    """Treppen im Pfad durch ein schraeges und ein gerades Stueck ersetzen, Lage fuer Lage"""
    out = []
    i = 0
    while i < len(path):
        j = i
        while j + 1 < len(path) and path[j + 1][0] == path[i][0]:
            j += 1
        l = path[i][0]
        run = [(p[1], p[2]) for p in path[i:j + 1]]
        res = [run[0]]
        k = 0
        while k < len(run) - 1:
            m = len(run) - 1
            done = False
            while m > k + 1 and not done:
                for df in (True, False):
                    pts = connector(run[k], run[m], df)
                    if line_free(blocked[l], pts):
                        res.extend(pts[1:])
                        k = m
                        done = True
                        break
                if not done:
                    m -= 1
            if not done:
                res.append(run[k + 1])
                k += 1
        out.extend((l, x, y) for x, y in res)
        i = j + 1
    return out


def to_segments(path):
    """Pfad in gerade Stuecke und Durchkontaktierungen zerlegen"""
    segs, vias = [], []
    start = path[0]
    last = path[0]
    d = None
    for n in path[1:]:
        if n[0] != last[0]:
            if start != last:
                segs.append((start, last))
            vias.append((last[1], last[2]))
            start, d = n, None
        else:
            nd = (n[1] - last[1], n[2] - last[2])
            if d is not None and nd != d:
                segs.append((start, last))
                start = last
            d = nd
        last = n
    if start != last:
        segs.append((start, last))
    return segs, vias


tracks = []      # (x1, y1, x2, y2, layer, width, net)
vias = []        # (x, y, net)
failed = []


state = {}       # Netz -> Gruppen schon verbundener Pads mit ihren Rasterpunkten


def net_state(net):
    """Masken und Gruppen eines Netzes. Am Anfang ist jedes Pad eine eigene Gruppe."""
    if net in state:
        # seit dem letzten Mal sind andere Netze dazugekommen
        state[net]['blocked'], state[net]['viaok'] = build_masks(net)
        return state[net]
    blocked, viaok = build_masks(net)
    layerfree = set()
    groups = []
    for p in pads[net]:
        ins = p.inside()
        if p.tht:
            layerfree.update(ins)
        nodes = {(l, ix, iy) for ix, iy in ins for l in (0, 1) if p.layers & (1 << l) and not blocked[l, iy, ix]}
        groups.append({'pads': [p], 'nodes': nodes})
    st = {'blocked': blocked, 'viaok': viaok, 'layerfree': layerfree, 'groups': groups}
    state[net] = st
    return st


def group_of(st, p):
    for g_ in st['groups']:
        if p in g_['pads']:
            return g_
    raise KeyError(p.ref)


def link(net, st, src_groups, dst, p):
    """Verbindet die Gruppe dst (Zielpad p) mit der naechsten der Gruppen src_groups"""
    width = cls(net)['track_width']
    sources = set()
    for g_ in src_groups:
        sources |= g_['nodes']
    if not dst['nodes'] or not sources:
        failed.append((net, p.ref, p.name, 'kein freier Rasterpunkt im Pad'))
        return False
    path = astar(st['blocked'], st['viaok'], st['layerfree'], sources, dst['nodes'],
                 int(round(p.x / G)), int(round(p.y / G)))
    if path is None:
        failed.append((net, p.ref, p.name, 'kein Weg'))
        return False
    path = smooth(st['blocked'], path)
    segs, vs = to_segments(path)
    # Anschluss an die Padmitte an beiden Enden
    for node in (path[0], path[-1]):
        for q in pads[net]:
            if q.layers & (1 << node[0]) and (node[1], node[2]) in q.inside():
                x, y = node[1] * G, node[2] * G
                if math.hypot(x - q.x, y - q.y) > 0.01:
                    tracks.append((q.x, q.y, x, y, node[0], width, net))
                break
    for a, b in segs:
        tracks.append((a[1] * G, a[2] * G, b[1] * G, b[2] * G, a[0], width, net))
    for x, y in vs:
        if (x, y) not in st['layerfree']:
            vias.append((x * G, y * G, net))
    src = next(g_ for g_ in src_groups if path[0] in g_['nodes'])
    src['pads'] += dst['pads']
    src['nodes'] |= dst['nodes'] | set(path)
    for n in path:
        if (n[1], n[2]) in st['layerfree'] or (n[1], n[2]) in vs:
            src['nodes'].add((1 - n[0], n[1], n[2]))
    st['groups'].remove(dst)
    return True


def route_pad(net, ref, name):
    """Ein einzelnes Pad vorab an das naechste Pad seines Netzes anschliessen"""
    st = net_state(net)
    p = next(q for q in pads[net] if q.ref == ref and q.name == name)
    dst = group_of(st, p)
    others = [g_ for g_ in st['groups'] if g_ is not dst]
    if others:
        link(net, st, others, dst, p)


def route_net(net):
    """Alle Gruppen des Netzes zu einer verbinden, immer die naechste zuerst"""
    ps = pads[net]
    if len(ps) < 2:
        return
    st = net_state(net)
    cx = sum(p.x for p in ps) / len(ps)
    cy = sum(p.y for p in ps) / len(ps)
    first = min(ps, key=lambda p: math.hypot(p.x - cx, p.y - cy))
    main = group_of(st, first)
    skipped = []
    while True:
        rest = [p for g_ in st['groups'] if g_ is not main and g_ not in skipped for p in g_['pads']]
        if not rest:
            break
        p = min(rest, key=lambda p: min(math.hypot(p.x - q.x, p.y - q.y) for q in main['pads']))
        dst = group_of(st, p)
        if not link(net, st, [main], dst, p):
            skipped.append(dst)


HOLE_GAP = 0.55
drills = [(p.x, p.y, p.drill / 2) for ps in pads.values() for p in ps if p.tht]


def commit(net, t0, v0):
    """Neue Bahnen und Durchkontaktierungen des Netzes als Hindernisse eintragen"""
    for x1, y1, x2, y2, l, w, n in tracks[t0:]:
        obst.append((('s', x1, y1, x2, y2, w / 2), 1 << l, n))
    for x, y, n in vias[v0:]:
        obst.append((('c', x, y, VIA_R), 3, n))
        drills.append((x, y, VIA_DRILL / 2))


def net_len(net):
    ps = pads[net]
    return sum(min(math.hypot(p.x - q.x, p.y - q.y) for q in ps if q is not p) for p in ps)


def order():
    """Erst die isolierten Busseiten, dann die Netze, die unter dem S3 durch muessen, zuletzt Versorgung und GND"""
    names = sorted(n for n in pads if len(pads[n]) > 1)
    if SEED is not None:
        import random
        random.Random(SEED).shuffle(names)
    bus = sorted([n for n in names if cls(n)['name'] == 'Bus'], key=net_len)
    power = [n for n in ('+5V', '+3V3', 'GND') if n in names]
    first = [n for n in names if n.startswith(('SC_', 'CMT_', 'USB_D'))]
    rest = sorted([n for n in names if n not in bus and n not in power and n not in first], key=net_len)
    return bus + first + rest + power


# Bleibt ein Netz offen, kommt es beim naechsten Durchgang nach vorn. Bei GND und den beiden
# Versorgungen kommt nur das offene Pad nach vorn und wird vorab an sein naechstes Pad angeschlossen.
t_start = time.time()
base = list(obst)
base_drills = list(drills)
seq = order()
SPLIT = ('GND', '+5V', '+3V3')
best = None
for run in range(20):
    obst[:] = base
    drills[:] = base_drills
    state.clear()
    del tracks[:], vias[:], failed[:]
    for item in seq:
        t0, v0 = len(tracks), len(vias)
        if isinstance(item, tuple):
            route_pad(*item)
            commit(item[0], t0, v0)
        else:
            route_net(item)
            commit(item, t0, v0)
    bad = []
    for f_ in failed:
        key = (f_[0], f_[1], f_[2]) if f_[0] in SPLIT else f_[0]
        if key not in bad:
            bad.append(key)
    print(f'Durchgang {run + 1}: {len(failed)} offen {bad}, {len(vias)} Vias, {time.time() - t_start:.0f} s', flush=True)
    if best is None or len(failed) < len(best[2]):
        best = (list(tracks), list(vias), list(failed), list(seq))
    if not failed:
        break
    seq = bad + [n for n in seq if n not in bad]
tracks[:], vias[:], failed[:] = best[0], best[1], best[2]
print('Reihenfolge:', ' '.join(str(x) for x in best[3]))

# ---------------------------------------------------------------- in die Platine schreiben
LAYER = {0: pcbnew.F_Cu, 1: pcbnew.B_Cu}
for x1, y1, x2, y2, l, w, net in tracks:
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(VECTOR2I(FromMM(x1), FromMM(y1)))
    t.SetEnd(VECTOR2I(FromMM(x2), FromMM(y2)))
    t.SetWidth(FromMM(w))
    t.SetLayer(LAYER[l])
    t.SetNet(board.FindNet(net))
    board.Add(t)
for x, y, net in vias:
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(VECTOR2I(FromMM(x), FromMM(y)))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetWidth(FromMM(2 * VIA_R))
    v.SetDrill(FromMM(VIA_DRILL))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(board.FindNet(net))
    board.Add(v)

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
pcbnew.SaveBoard(PCB, board)
with open(PCB.replace('.kicad_pcb', '.kicad_pro'), 'w') as f:
    json.dump(pro, f, indent=2)

length = sum(math.hypot(t[2] - t[0], t[3] - t[1]) for t in tracks)
print(f'{len(tracks)} Stuecke, {len(vias)} Vias, {length:.0f} mm, {time.time() - t_start:.0f} s')
for f_ in failed:
    print('NICHT VERBUNDEN:', f_)
base = os.path.basename(PCB).replace('.kicad_pcb', '')
rpt = os.path.join(os.path.dirname(PCB), base + '_drc.txt')
pcbnew.WriteDRCReport(board, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
print('DRC-Bericht:', rpt)
