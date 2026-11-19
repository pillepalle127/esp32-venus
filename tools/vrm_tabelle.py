#!/usr/bin/env python3
"""Erzeugt firmware/src/vrm_tabelle.h: welche Venus-Themen ESP_venus an VRM gibt.

Quellen (beide nur lokal, nicht im Repo):
  - datalist.py aus dem vrmlogger von Venus OS v3.75 (docs/vrmlogger/vrmlogger/):
    Venus-Pfad -> VRM-Code, wann er gesendet wird, Nachkommastellen, Umrechnung
  - Themenliste des Pi (TSV: Pfad, Anzahl, Retain, Inhalt), mitgeschnitten am 19.11.2026

In die Tabelle kommt jeder Pfad aus datalist.py, den der Pi für die Geräte aus
vrm_replay.GERAETE hat. Dazu die Zähler für die kWh (kwhdeltas.py) und die Themen
der Live-Ansicht (vrm_live.THEMEN).

  python3 tools/vrm_tabelle.py <pi_topics.tsv>
"""

import ast
import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
VRMLOGGER = os.path.join(HIER, "..", "docs", "vrmlogger", "vrmlogger")
AUS = os.path.join(HIER, "..", "firmware", "src", "vrm_tabelle.h")

sys.path.insert(0, HIER)
sys.path.insert(1, VRMLOGGER)
import conversions  # noqa: E402
import datalist  # noqa: E402
import vrm_replay as R  # noqa: E402

# Zähler, aus denen kwhdeltas.py die kWh bildet (nur die Dienste, die es hier gibt)
ZAEHLER = {
    "vebus": ["/Energy/AcIn1ToInverter", "/Energy/AcIn2ToInverter", "/Energy/AcIn1ToAcOut",
              "/Energy/AcIn2ToAcOut", "/Energy/InverterToAcIn1", "/Energy/InverterToAcIn2",
              "/Energy/AcOutToAcIn1", "/Energy/AcOutToAcIn2", "/Energy/InverterToAcOut",
              "/Energy/OutToInverter"],
    "battery": ["/History/DischargedEnergy", "/History/ChargedEnergy"],
    "solarcharger": ["/Yield/User"],
    "grid": ["/Ac/Energy/Forward", "/Ac/Energy/Reverse"],
    "pvinverter": ["/Ac/Energy/Forward", "/Position"],
    "system": ["/Timers/TimeOff", "/Timers/TimeOnGenerator", "/Timers/TimeOnGrid", "/Timers/TimeOnInverter"],
}
# Live-Ansicht: was vrm_live.py veröffentlicht hat (dort aus dem Verlauf, hier vom Pi)
LIVE = [
    "vebus/288/Ac/ActiveIn/L1/P", "vebus/288/Ac/Out/L1/P", "battery/291/Dc/0/Current",
    "battery/291/Dc/0/Voltage", "battery/291/Soc", "battery/291/Dc/0/Power", "battery/512/Dc/0/Current",
    "battery/512/Dc/0/Temperature", "battery/512/System/MinCellVoltage", "battery/512/System/MaxCellVoltage",
    *[f"solarcharger/{n}/{p}" for n in (288, 291, 292, 293, 294, 296) for p in ("Yield/Power", "Dc/0/Current")],
    "grid/40/Ac/L1/Power", "grid/40/Ac/Power", "system/0/Ac/Grid/L1/Power", "system/0/Ac/Consumption/L1/Power",
    "system/0/Ac/ConsumptionOnInput/L1/Power", "system/0/Ac/PvOnGrid/L1/Power", "system/0/Dc/Pv/Power",
    "system/0/Dc/Battery/Soc", "system/0/Dc/Battery/Power", "system/0/Dc/Battery/Current",
    "system/0/Dc/Battery/Voltage", "system/0/SystemState/State",
]
# Nie vom Pi übernehmen: enthält die Portal-ID des Pi, ESP_venus meldet seine eigene
NIE = {"system/0/Serial"}

KIND = {"onIntervalAlways": "VI", "onIntervalAlwaysAndOnEvent": "VI",
        "onIntervalOnlyWhenChanged": "VC", "configChange": "VK"}
CONV = {None: "UN", conversions.productid: "UP", conversions.firmware: "UT",
        conversions.seconds_to_days: "UD", conversions.seconds_to_hours: "UH",
        conversions.assistant_code: "UA"}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pi = {}
    for zeile in open(sys.argv[1]):
        teile = zeile.rstrip("\n").split("\t")
        pi[teile[0]] = teile[-1]

    instanzen = {}
    for code, inst, _ in R.GERAETE:
        dienst = {"vvp": "vebus", "BM": "battery", "ScM": "solarcharger", "gP": "grid"}[code]
        instanzen.setdefault(dienst, []).append(inst)
    instanzen["system"] = [0]
    for n in range(0, 300):
        if f"pvinverter/{n}/Position" in pi:
            instanzen.setdefault("pvinverter", []).append(n)

    eintraege = {}      # Pfad -> (Code, Art, Umrechnung, Stellen, Text)
    ohne = []
    for dienst, liste in instanzen.items():
        baum = datalist.vrmtree.get("com.victronenergy." + dienst, {})
        for inst in liste:
            if dienst != "pvinverter":
                for pfad, a in baum.items():
                    thema = f"{dienst}/{inst}{pfad}"
                    art = KIND.get(a.get("whenToLog"))
                    if not art or thema not in pi or thema in NIE:
                        continue
                    conv = a.get("convert")
                    if conv not in CONV:
                        ohne.append(f"{thema} ({conv.__name__})")
                        continue
                    code = f"{a['code']}[{inst}]"
                    text = None
                    if conv is conversions.firmware:
                        # Den Text der Firmware-Version bildet jeder Treiber selbst, über MQTT kommt
                        # nur die Zahl. Text wie VRM ihn vom Pi hat (vrm_replay.KONFIG).
                        text = R.KONFIG.get(code)
                        if text is None:
                            ohne.append(f"{thema} (Firmware-Text unbekannt)")
                            continue
                    eintraege[thema] = (code, art, CONV[conv], a.get("precision", -1), text)
            for pfad in ZAEHLER.get(dienst, []):
                thema = f"{dienst}/{inst}{pfad}"
                if thema in pi and thema not in eintraege:
                    eintraege[thema] = (None, "VN", "UN", -1, None)
    for thema in LIVE:
        if thema not in eintraege:
            if thema not in pi:
                ohne.append(f"{thema} (nicht beim Pi)")
            eintraege[thema] = (None, "VN", "UN", -1, None)

    def c(s):
        return "NULL" if s is None else '"' + s.replace('"', '\\"') + '"'

    zeilen = []
    for thema in sorted(eintraege):
        code, art, conv, stellen, text = eintraege[thema]
        zeilen.append(f"    {{{c(thema)}, {c(code)}, {art}, {conv}, {stellen}, {c(text)}}},")

    with open(AUS, "w") as f:
        f.write("// Erzeugt von tools/vrm_tabelle.py, nicht von Hand ändern.\n")
        f.write("// Venus-Thema -> VRM-Code wie datalist.py im vrmlogger von Venus OS v3.75,\n")
        f.write("// nur Pfade, die der Pi am 19.11.2026 hatte. Ablauf: docs/vrm.md.\n")
        f.write("#pragma once\n\n#include \"vrm_tab_typ.h\"\n\n")
        f.write(f"static const VrmEintrag VRM_TABELLE[] = {{\n")
        f.write("\n".join(zeilen))
        f.write("\n};\n")
    arten = {}
    for e in eintraege.values():
        arten[e[1]] = arten.get(e[1], 0) + 1
    print(f"{AUS}: {len(eintraege)} Themen, " + ", ".join(f"{k} {v}" for k, v in sorted(arten.items())))
    for o in ohne:
        print("  ausgelassen:", o)


if __name__ == "__main__":
    main()
