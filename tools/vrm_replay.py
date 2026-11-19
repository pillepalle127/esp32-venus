#!/usr/bin/env python3
"""Spielt einen Tag aus dem ioBroker-Verlauf an eine VRM-Test-Installation.

Ablauf und Zuordnung: docs/vrm.md, Abschnitt "Teststand".

Standard ist der Probelauf: Das Skript schreibt die Zeilen in eine Datei
(Token und Portal-ID ersetzt) und sendet nichts. Erst mit --senden geht es
an VRM, und nur an die Test-ID aus tools/vrm_test.json.

  python3 tools/vrm_replay.py docs/hist_20261118
  python3 tools/vrm_replay.py docs/hist_20261118 --id <MAC>    # legt tools/vrm_test.json an
  python3 tools/vrm_replay.py docs/hist_20261118 --nur-anmelden --senden   # 2 Zeilen, dann in VRM hinzufügen
  python3 tools/vrm_replay.py docs/hist_20261118 --senden
  python3 tools/vrm_replay.py docs/hist_20261118 --nur-kwh [--senden]       # kWh je 15 min

Jede Zeile hat die Form, die der vrmlogger in Venus OS schickt:
  d=2&IMEI=<id>&c=<befehl>&<code>[<instanz>]=<wert>...&t=300&TO=<alter>&VRMAUTHTOKEN=..&nonce=..
TO ist das Alter der Zeile in Sekunden. Gesendet wird gebündelt an
logbulk.php, zlib-gepackt, mit crc32 der ungepackten Zeilen.
"""

import argparse
import bisect
import datetime
import glob
import json
import os
import random
import re
import ssl
import string
import sys
import time
import urllib.parse
import urllib.request
import zlib
import zoneinfo

LOG_URL = "https://ccgxlogging.victronenergy.com/log/logbulk.php"
INTERVAL = 300                  # wie /Settings/Vrmlogger/LogInterval auf dem Pi
BUENDEL = 100                   # Zeilen je POST
ZONE = zoneinfo.ZoneInfo("Europe/Berlin")
SECRETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vrm_test.json")
# ccgxlogging hat ein Zertifikat der eigenen "CCGX Certificate Authority" von Victron,
# nicht einer öffentlichen CA. Venus hat sie in /etc/ssl/certs/ccgx-ca.pem (nicht im Repo).
CA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "ccgx-ca.pem")

ANNOUNCE, SENDDATA, CONFIGCHANGE, HOURLYDELTAS = 0, 1, 2, 3
KWH_FENSTER = 900               # kWh-Deltas je 15 min, wie der vrmlogger

# Geräte, wie Venus sie am 19.11.2026 meldet (dbus /ProductId).
# VRM bekommt die Produktkennung hexadezimal (conversions.productid).
GERAETE = [
    ("vvp", 288, 9763),     # MultiPlus-II 48/5000/70-50
    ("BM", 291, 49209),     # SmartShunt IP65 300A
    ("BM", 512, 45063),     # JK-BMS
    ("ScM", 288, 41056),    # SmartSolar MPPT 100/20 48V
    ("ScM", 291, 41056),
    ("ScM", 292, 41063),    # BlueSolar MPPT 100/20 48V
    ("ScM", 293, 41063),
    ("ScM", 294, 41063),
    ("ScM", 296, 41063),
    ("gP", 40, 41393),      # Energy Meter VM-3P75CT (Tasmota)
]

# ANNOUNCE: Version, Gateway-Produkt wie beim Pi, von den Fähigkeiten (cp, vrmlogger.py get_announce):
#   Bit 8:  "Needs active MQTT topic refreshing with keep-alives instead of using retained messages"
#   Bit 10: "Uses new vrm config data attributes, part of 2024 security project". Ohne dieses Bit
#           setzt VRM view_permissions.twoway nicht, trotz vrm=2; ohne twoway keine Live-Ansicht
#           (getestet 19.11.2026: cp 256 -> twoway False, 1280 -> True).
ANNOUNCE_DATEN = {"v": "v3.75", "mi": "C003", "cp": (1 << 8) | (1 << 10)}

# Nach jedem ANNOUNCE schickt der vrmlogger PRIORITY_CONFIGCHANGE (c=8) und CONFIGCHANGE.
# VRM vergisst die Einstellungen sonst (gesehen 19.11.2026: vrm, sl, tz weg nach ANNOUNCE).
PRIORITY_KONFIG = {"vrm": 2}                     # VRM-Portal-Modus voll (Zwei-Wege)
SYSTEM_KONFIG = {"tz": "Europe/Berlin", "sl": 300, "mql": 1, "al": 2,
                 # Aufbau der Anlage wie beim Pi (VRM-API diagnostics, 19.11.2026)
                 "si1": 1, "si2": 0, "shao": 1, "shai": 1, "shd": 0,
                 "sb": "com.victronenergy.battery/291", "st": "ESS", "H4M": 1, "bol": 1, "rwgm": 0}
# Feste Werte des Systems, gehen mit jeder SENDDATA-Zeile mit (onIntervalAlwaysAndOnEvent)
SYSTEM_WERTE = {"abs": "com.victronenergy.battery/291", "AIS": 1, "AIT": "grid", "ini": 1,
                "ic0": 1, "id0": 40, "vi0": 40, "ist0": "grid", "is0": 1}

# Weitere Konfigurationswerte, wie der Pi sie meldet (VRM-API diagnostics, 19.11.2026)
KONFIG = {
    "vvv[288]": "508",          # VE.Bus-Firmware
    "vmc[288]": "VE.Bus",       # /Mgmt/Connection
    "vid[288]": 1,              # /ShortIds
    "PC[288]": 1,               # /Ac/NumberOfPhases
    "sa[288]": 1,               # /ModeIsAdjustable
    "pmk2[288]": "MK3",         # /Interfaces/Mk2/ProductName
    "vmk2[288]": "1170216",     # /Interfaces/Mk2/Version
    "BV[291]": "v4.19",
    "BV[512]": "v19.9",
    "Bm[512]": "JK-BMS",
    "gv[40]": "v1.04",
    **{f"ScVt[{n}]": "v1.74" for n in (288, 291, 292, 293, 294, 296)},
}

# Datei im Verlauf (ohne "history." und ".json") -> Code, Instanz, Nachkommastellen.
# "{V}" steht für "mqtt.1.N.<Portal-ID des Pi>", die ID liest das Skript aus den Dateinamen.
# Codes aus datalist.py. Quellen der Aliase (objects.jsonl, 19.11.2026): docs/vrm.md.
WERTE = [
    ("{V}.vebus.288.Ac.ActiveIn.L1.P", "IP1", 288, 0),
    ("{V}.vebus.288.Ac.Out.L1.P", "OP1", 288, 0),
    ("{V}.battery.291.Dc.0.Current", "I", 291, 2),
    ("{V}.battery.291.Dc.0.Voltage", "V", 291, 2),
    ("{V}.battery.291.Soc", "SOC", 291, 1),
    ("alias.0.VenusOS.DC_battery_I", "I", 512, 2),
    ("alias.0.VenusOS.DC_battery_T_BMS", "BT", 512, 0),
    ("alias.0.VenusOS.DC_battery_MinCellVoltage", "mcV", 512, 2),
    ("alias.0.VenusOS.DC_battery_MaxCellVoltage", "McV", 512, 2),
    ("alias.0.VenusOS.DC_battery_T_Cell_min", "mcT", 512, 0),
    ("alias.0.VenusOS.DC_battery_T_Cell_max", "McT", 512, 0),
    ("DC_MPPT_288_I", "ScI", 288, 1),
    ("DC_MPPT_288_P", "PVP", 288, 0),
    ("DC_MPPT_291_I", "ScI", 291, 1),
    ("DC_MPPT_291_P", "PVP", 291, 0),
    ("alias.0.VenusOS.DC_MPPT_292_I", "ScI", 292, 1),
    ("alias.0.VenusOS.DC_MPPT_292_P", "PVP", 292, 0),
    ("alias.0.VenusOS.DC_MPPT_293_I", "ScI", 293, 1),
    ("alias.0.VenusOS.DC_MPPT_293_P", "PVP", 293, 0),
    ("alias.0.VenusOS.DC_MPPT_294_I", "ScI", 294, 1),
    ("alias.0.VenusOS.DC_MPPT_294_P", "PVP", 294, 0),
    ("alias.0.VenusOS.DC_MPPT_296_I", "ScI", 296, 1),
    ("alias.0.VenusOS.DC_MPPT_296_P", "PVP", 296, 0),
    ("AC_powertotal_Haus", "g1p", 40, 0),
    ("{V}.system.0.Ac.Consumption.L1.Power", "a1", 0, 0),
    ("{V}.system.0.Ac.ConsumptionOnInput.L1.Power", "i1", 0, 0),
    ("alias.0.VenusOS.AC_MP2_PVonGrid_P", "Pi", 0, 0),
    ("alias.0.VenusOS.DC_PV_total_P", "Pdc", 0, 0),
    ("alias.0.VenusOS.MP2_system_state", "ss", 0, 0),
    ("AC_powertotal_Haus", "g1", 0, 0),
    # Batteriewerte im System kommen vom SmartShunt: /ActiveBatteryService = battery/291 (19.11.2026)
    ("{V}.battery.291.Soc", "bs", 0, 1),
    ("DC_battery_P", "bp", 0, 0),
]


# ---------------------------------------------------------------- kWh (HOURLYDELTAS)
#
# Venus bildet die kWh aus Zählern der Geräte. Im Verlauf stehen davon nur die Zähler des
# Tasmota. Alles andere integriere ich aus den Leistungen (Wert gilt bis zum nächsten).
#   Netz Bezug       MT681_Total_in (Zählerstand)          -> dgb[40]
#   Netz Einspeisung VIC_Total_out (im Tasmota integriert)  -> dgs[40]
#   MPPT             Yield/Power integriert                 -> dYU[n]
#   Batterie         battery 291 Dc/0/Power, nach Vorzeichen -> dH22 (laden), dH21 (entladen)
#   PV am AC-Eingang system Ac/PvOnGrid/L1/Power integriert
#   MultiPlus        aus Ac/ActiveIn/L1/P und Ac/Out/L1/P die Wege AcIn1->AcOut, AcIn1->Inverter,
#                    Inverter->AcIn1, Inverter->AcOut, AcOut->AcIn1, AcOut->Inverter
# Aufteilung auf Gc, Gb, Pb, Pc, Pg, Bc, Bg wie kwhdeltas.getdeltas() im vrmlogger, mit den
# Einstellungen der echten Installation: kein DC-System (shd 0), AC-Eingang 1 = Netz.

KWH_ZAEHLER = {
    "dgb": "sonoff.0.Tasm_Kellercenter2.MT681_Total_in",
    "dgs": "sonoff.0.Tasm_Kellercenter2.VIC_Total_out",
}
KWH_LEISTUNG = {
    "pvdc": [(n, f"DC_MPPT_{n}_P" if n in (288, 291) else f"alias.0.VenusOS.DC_MPPT_{n}_P")
             for n in (288, 291, 292, 293, 294, 296)],
    "bat": "DC_battery_P",
    "pvac_grid": "alias.0.VenusOS.AC_MP2_PVonGrid_P",
    "p_in": "{V}.vebus.288.Ac.ActiveIn.L1.P",
    "p_out": "{V}.vebus.288.Ac.Out.L1.P",
}


def integral(reihe, a, b, f=lambda v: v):
    """Wh von a bis b, Wert gilt bis zum nächsten Eintrag, vor dem ersten 0."""
    if not reihe:
        return 0.0
    zeiten = [x[0] for x in reihe]
    i = max(bisect.bisect_right(zeiten, a) - 1, 0)
    wh = 0.0
    while i < len(reihe) and reihe[i][0] < b:
        t0 = max(reihe[i][0], a)
        t1 = min(reihe[i + 1][0], b) if i + 1 < len(reihe) else b
        if t1 > t0 and reihe[i][0] <= t1:
            wh += f(reihe[i][1]) * (t1 - t0) / 3600.0
        i += 1
    return wh


def vebus_wege(p_in, p_out, a, b):
    """kWh der Wege im MultiPlus aus Eingangs- und Ausgangsleistung (Inverter = in - out)."""
    zeit = sorted({t for t, _ in p_in + p_out if a <= t < b} | {a})
    zin, zout = [x[0] for x in p_in], [x[0] for x in p_out]
    w = dict.fromkeys(("in_out", "in_inv", "inv_in", "inv_out", "out_in", "out_inv"), 0.0)
    for k, t in enumerate(zeit):
        t1 = zeit[k + 1] if k + 1 < len(zeit) else b
        vin, vout = wert_bei(p_in, zin, t), wert_bei(p_out, zout, t)
        if vin is None or vout is None:
            continue
        h = (t1 - t) / 3600.0 / 1000.0
        if vin >= 0 and vout >= 0:
            w["in_out"] += min(vin, vout) * h
            w["in_inv"] += max(vin - vout, 0) * h
            w["inv_out"] += max(vout - vin, 0) * h
        elif vin < 0 and vout >= 0:
            w["inv_in"] += -vin * h
            w["inv_out"] += vout * h
        elif vin >= 0 and vout < 0:
            w["out_inv"] += (vin - vout) * h if vin == 0 else -vout * h
            w["in_inv"] += vin * h
        else:
            w["out_in"] += min(-vin, -vout) * h
            w["out_inv"] += max(vin - vout, 0) * h
            w["inv_in"] += max(vout - vin, 0) * h
    return w


def verteile(quelle, ziel):
    """_distribute() aus kwhdeltas.py"""
    v = min(quelle, ziel)
    return quelle - v, ziel - v, v


def kwh_aufteilen(grid_in, grid_out, pvac_grid, pvdc, w):
    """kwhdeltas.getdeltas() ohne Genset, Inverter, Multi-RS, Lichtmaschine, ohne DC-System."""
    acgrid_to_dc, acgrid_to_acout = w["in_inv"], w["in_out"]
    acout_to_dc, dc_to_acgrid, dc_to_acout = w["out_inv"], w["inv_in"], w["inv_out"]
    grid_to_consumption = pv_to_consumption = bat_to_consumption = pv_to_bat = pv_to_grid = 0.0

    dc_to_ac = dc_to_acgrid + dc_to_acout
    pvdc_to_bat = max(0.0, pvdc - dc_to_ac)
    bat_in = max(0.0, dc_to_ac - pvdc)
    bat_out = acgrid_to_dc + acout_to_dc
    pvdc -= pvdc_to_bat

    pvac_grid, acgrid_to_dc, pvac_in_to_dc = verteile(pvac_grid, acgrid_to_dc)
    pvac_grid, grid_out, pvac_in_to_grid = verteile(pvac_grid, grid_out)
    pvac_grid, acgrid_to_acout, pvac_in_to_acout = verteile(pvac_grid, acgrid_to_acout)
    pv_to_consumption += pvac_grid + pvac_in_to_acout
    pv_to_grid += pvac_in_to_grid

    grid_in, acgrid_to_dc, grid_to_dc = verteile(grid_in, acgrid_to_dc)
    grid_in, acgrid_to_acout, grid_to_acout = verteile(grid_in, acgrid_to_acout)
    grid_to_consumption += grid_in + grid_to_acout

    grid_to_dc, bat_out, grid_to_bat = verteile(grid_to_dc, bat_out)
    grid_to_consumption += grid_to_dc
    pvac_in_to_dc, bat_out, pvac_in_to_bat = verteile(pvac_in_to_dc, bat_out)
    pv_to_consumption += pvac_in_to_dc
    pv_to_bat += pvac_in_to_bat

    bat_in, dc_to_acout, bat_to_acout = verteile(bat_in, dc_to_acout)
    bat_in, dc_to_acgrid, bat_to_acin = verteile(bat_in, dc_to_acgrid)
    bat_to_consumption += bat_to_acout

    pvdc, dc_to_acout, pvdc_to_acout = verteile(pvdc, dc_to_acout)
    pvdc, dc_to_acgrid, pvdc_to_acin = verteile(pvdc, dc_to_acgrid)
    pv_to_consumption += pvdc_to_acout
    pv_to_bat += pvdc_to_bat

    pvdc_to_acin, grid_out, pvdc_to_grid = verteile(pvdc_to_acin, grid_out)
    pv_to_grid += pvdc_to_grid
    pv_to_consumption += pvdc_to_acin
    bat_to_acin, grid_out, bat_to_grid = verteile(bat_to_acin, grid_out)
    bat_to_consumption += bat_to_acin

    return {"Gc": grid_to_consumption, "Gb": grid_to_bat, "gc": 0.0, "gb": 0.0, "Pb": pv_to_bat,
            "Pc": pv_to_consumption, "Pg": pv_to_grid, "Bc": bat_to_consumption, "Bg": bat_to_grid}


def kwh_zeilen(verlauf, venus, beginn, ende):
    """Je 15 min ein Satz kWh-Deltas: (Zeitpunkt der Zeile, Daten)."""
    def reihe(name):
        return lese_reihe(os.path.join(verlauf, "history." + name.replace("{V}", venus) + ".json"))

    zaehler = {c: reihe(n) for c, n in KWH_ZAEHLER.items()}
    mppt = [(n, reihe(name)) for n, name in KWH_LEISTUNG["pvdc"]]
    bat, pvac = reihe(KWH_LEISTUNG["bat"]), reihe(KWH_LEISTUNG["pvac_grid"])
    p_in, p_out = reihe(KWH_LEISTUNG["p_in"]), reihe(KWH_LEISTUNG["p_out"])

    def stand(r, t):
        return wert_bei(r, [x[0] for x in r], t)

    ergebnis, summe = [], {}
    a = beginn
    while a < ende:
        b = a + KWH_FENSTER
        d = {}
        za = {c: (stand(r, a), stand(r, b)) for c, r in zaehler.items()}
        grid_in = za["dgb"][1] - za["dgb"][0] if None not in za["dgb"] else 0.0
        grid_out = za["dgs"][1] - za["dgs"][0] if None not in za["dgs"] else 0.0
        pvdc = 0.0
        for n, r in mppt:
            e = integral(r, a, b) / 1000.0
            d[f"dYU[{n}]"] = e
            pvdc += e
        pvac_grid = integral(pvac, a, b) / 1000.0
        d["dH22[291]"] = integral(bat, a, b, lambda v: max(v, 0.0)) / 1000.0
        d["dH21[291]"] = integral(bat, a, b, lambda v: max(-v, 0.0)) / 1000.0
        d["dgb[40]"], d["dgs[40]"] = grid_in, grid_out
        d.update(kwh_aufteilen(grid_in, grid_out, pvac_grid, pvdc, vebus_wege(p_in, p_out, a, b)))
        for k, v in d.items():
            summe[k] = summe.get(k, 0.0) + v
        d = {k: round(v, 4) for k, v in d.items()}
        # Et: Beginn des Fensters in UTC, auf 15 min gerundet (kwhdeltas.py)
        d["Et"] = datetime.datetime.fromtimestamp(a, datetime.timezone.utc).strftime("%Y%m%d%H%M")
        ergebnis.append((b, d))
        a = b
    return ergebnis, summe


def lese_reihe(pfad):
    """Liste (ts in s, Wert) aufsteigend. Venus-Werte aus mqtt.1 sind Text {"value": x}."""
    reihe = []
    for e in json.load(open(pfad)):
        v = e.get("val")
        if isinstance(v, str):
            try:
                v = json.loads(v)
            except ValueError:
                continue
            if isinstance(v, dict):
                v = v.get("value")
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            continue
        reihe.append((e["ts"] / 1000.0, float(v)))
    reihe.sort()
    return reihe


def wert_bei(reihe, zeiten, t):
    """Letzter Wert bis einschließlich t. history speichert oft nur Änderungen."""
    i = bisect.bisect_right(zeiten, t)
    return reihe[i - 1][1] if i else None


def runde(v, stellen):
    return int(round(v)) if stellen == 0 else round(v, stellen)


def nonce_quelle():
    zeichen = string.ascii_letters + string.digits
    vorsatz = "".join(random.SystemRandom().choice(zeichen) for _ in range(4))
    n = 0
    while True:
        n += 1
        yield f"{vorsatz}:{n}"


def zeile(befehl, daten, ts, jetzt, portal_id, token, nonce):
    """Wie vrm_encode() + vrm_encode_with_token() + remove_extraneous_characters()."""
    s = urllib.parse.urlencode({"d": 2, "IMEI": portal_id, "c": befehl})
    if daten:
        s += "&" + urllib.parse.urlencode(daten)
    alter = int(jetzt) - int(ts)
    if alter > 0:
        s += f"&TO={alter}"
    s += f"&VRMAUTHTOKEN={token}&nonce={nonce}"
    return s.replace("=0.0&", "=0&")


def lade_secrets(neue_id):
    if os.path.exists(SECRETS):
        sec = json.load(open(SECRETS))
        if neue_id and neue_id.lower() != sec["portal_id"]:
            sys.exit(f"{SECRETS} hat schon eine andere ID. Datei von Hand löschen, wenn sie ersetzt werden soll.")
        return sec
    if not neue_id:
        return None
    if not re.fullmatch(r"[0-9a-f]{12}", neue_id.lower()):
        sys.exit("--id: 12 Hex-Zeichen erwartet (MAC ohne Trennzeichen)")
    # Token wie im vrmlogger: 8 Zufallsbytes als Hex
    sec = {"portal_id": neue_id.lower(), "token": os.urandom(8).hex()}
    fd = os.open(SECRETS, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(sec, f, indent=1)
        f.write("\n")
    print(f"{SECRETS} angelegt (Portal-ID und neues Token, nicht im Repo)")
    return sec


def sende(zeilen, portal_id):
    if not os.path.exists(CA):
        sys.exit(f"fehlt: {CA} (von Venus: /etc/ssl/certs/ccgx-ca.pem)")
    ctx = ssl.create_default_context(cafile=CA)
    for a in range(0, len(zeilen), BUENDEL):
        teil = zeilen[a:a + BUENDEL]
        roh = b"".join(z.encode() + b"\n" for z in teil)
        crc = zlib.crc32(roh) & 0xFFFFFFFF
        url = f"{LOG_URL}?crc32={crc}&id={portal_id}"
        req = urllib.request.Request(url, data=zlib.compress(roh, 9), headers={"User-Agent": "VE/CCGX/2"})
        try:
            with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
                code, text = r.status, r.read(200).decode(errors="replace").strip()
                retry = r.headers.get("Retry-After")
        except urllib.error.HTTPError as e:
            code, text, retry = e.code, e.read(200).decode(errors="replace").strip(), None
        ok = code == 200 and text == "vrm: OK"
        print(f"Zeilen {a + 1}–{a + len(teil)}: HTTP {code}, \"{text}\"" + (f", Retry-After {retry}" if retry else ""))
        if not ok or retry:
            sys.exit("abgebrochen")
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("verlauf", help="entpackter Tagesordner, z. B. docs/hist_20261118")
    ap.add_argument("--id", help="Portal-ID der Test-Installation (MAC von ESP_venus), nur beim ersten Mal")
    ap.add_argument("--senden", action="store_true", help="wirklich an VRM senden")
    ap.add_argument("--nur-anmelden", action="store_true",
                    help="nur ANNOUNCE und Geräte, ohne Werte (damit die ID in VRM hinzugefügt werden kann)")
    ap.add_argument("--nur-kwh", action="store_true",
                    help="nur Geräte und kWh-Deltas (HOURLYDELTAS) je 15 min, ohne Messwerte")
    ap.add_argument("--nur-konfig", action="store_true",
                    help="nur eine CONFIGCHANGE-Zeile mit Geräten und KONFIG, Zeitpunkt jetzt")
    ap.add_argument("--aus", help="Datei für den Probelauf (Standard: <verlauf>/vrm_replay_probe.txt)")
    args = ap.parse_args()

    m = re.search(r"(\d{8})/?$", args.verlauf)
    if not m:
        sys.exit("Ordnername muss mit dem Datum JJJJMMTT enden")
    tag = datetime.datetime.strptime(m.group(1), "%Y%m%d").replace(tzinfo=ZONE)
    beginn = tag.timestamp()
    ende = (tag + datetime.timedelta(days=1)).timestamp()

    treffer = glob.glob(os.path.join(args.verlauf, "history.mqtt.1.N.*.vebus.288.Ac.ActiveIn.L1.P.json"))
    if len(treffer) != 1:
        sys.exit("Venus-Dateien (mqtt.1.N.<ID>…) nicht eindeutig gefunden")
    venus = re.search(r"history\.(mqtt\.1\.N\.[0-9a-f]+)\.", os.path.basename(treffer[0])).group(1)

    reihen = {}
    for name, code, inst, _ in WERTE:
        datei = os.path.join(args.verlauf, "history." + name.replace("{V}", venus) + ".json")
        if not os.path.exists(datei):
            sys.exit(f"fehlt: {os.path.basename(datei)}")
        if name not in reihen:
            r = lese_reihe(datei)
            reihen[name] = (r, [x[0] for x in r])

    sec = lade_secrets(args.id)
    if args.senden and not sec:
        sys.exit(f"{SECRETS} fehlt. Erst einmal mit --id <MAC> aufrufen.")
    portal_id = sec["portal_id"] if sec and args.senden else "<ID>"
    token = sec["token"] if sec and args.senden else "<TOKEN>"

    jetzt = time.time()
    nonce = nonce_quelle()
    zeilen = []

    # Venus schickt beim Start ANNOUNCE mit der eigenen Version; hier nur die Version
    # Beim reinen Anmelden mit der aktuellen Zeit, sonst mit dem Beginn des Tages
    t0 = jetzt if args.nur_anmelden else beginn
    if not args.nur_kwh and not args.nur_konfig:
        zeilen.append(zeile(ANNOUNCE, ANNOUNCE_DATEN, t0, jetzt, portal_id, token, next(nonce)))
    # Geräte anmelden, dazu die Zeitzone der echten Installation
    geraete = {f"{c}[{i}]": "%x" % pid for c, i, pid in GERAETE}
    geraete.update(SYSTEM_KONFIG)
    geraete.update(KONFIG)
    if not args.nur_kwh and not args.nur_konfig:
        zeilen.append(zeile(8, PRIORITY_KONFIG, t0, jetzt, portal_id, token, next(nonce)))
    if args.nur_konfig:
        t0 = jetzt
    zeilen.append(zeile(CONFIGCHANGE, geraete, t0, jetzt, portal_id, token, next(nonce)))

    summe = {}
    if args.nur_kwh:
        kwh, summe = kwh_zeilen(args.verlauf, venus, beginn, ende)
        for t, daten in kwh:
            zeilen.append(zeile(HOURLYDELTAS, daten, t, jetzt, portal_id, token, next(nonce)))

    leer = 0
    t = beginn + INTERVAL
    while t <= ende and not args.nur_anmelden and not args.nur_kwh and not args.nur_konfig:
        daten = {}
        for name, code, inst, stellen in WERTE:
            r, z = reihen[name]
            v = wert_bei(r, z, t)
            if v is not None:
                daten[f"{code}[{inst}]"] = runde(v, stellen)
        if daten:
            daten.update(SYSTEM_WERTE)
            daten["t"] = INTERVAL
            zeilen.append(zeile(SENDDATA, daten, t, jetzt, portal_id, token, next(nonce)))
        else:
            leer += 1
        t += INTERVAL

    aus = args.aus or os.path.join(args.verlauf, "vrm_replay_probe.txt")
    if args.nur_kwh:
        print(f"Tag {m.group(1)}: {len(zeilen)} Zeilen (1 CONFIGCHANGE, {len(zeilen) - 1} HOURLYDELTAS)")
        print("Tagessummen kWh: " + ", ".join(f"{k} {v:.2f}" for k, v in sorted(summe.items())))
    else:
        print(f"Tag {m.group(1)}: {len(zeilen)} Zeilen (1 ANNOUNCE, 1 CONFIGCHANGE, {len(zeilen) - 2} SENDDATA), "
              f"{leer} Zeitpunkte ohne Werte")

    if not args.senden:
        with open(aus, "w") as f:
            for z in zeilen:
                f.write(urllib.parse.unquote(z) + "\n")
        print(f"Probelauf, nichts gesendet. Zeilen (lesbar, ID und Token ersetzt): {aus}")
        return

    print(f"Sende an {LOG_URL}, Test-Installation aus {os.path.basename(SECRETS)}")
    sende(zeilen, portal_id)
    print("fertig")


if __name__ == "__main__":
    main()
