#!/usr/bin/env python3
"""Live-Ansicht in VRM für die Test-Installation, wie die MQTT-Brücke in Venus OS.

Ablauf und Hintergrund: docs/vrm.md, Abschnitt "Live-Ansicht und Fernzugriff".

  python3 tools/vrm_live.py docs/hist_20261118                    # Probelauf: zeigt die Themen
  python3 tools/vrm_live.py docs/hist_20261118 --senden [--dauer 600]

Mit --senden:
 1. Beim ersten Mal ein MQTT-Passwort erzeugen (32 Hex-Zeichen wie in Venus), in
    tools/vrm_test.json ablegen und bei VRM anmelden (/log/storemqttpassword.php).
 2. Mit mqtt<n>.victronenergy.com:443 verbinden (TLS, Victron-CA), Benutzer ccgxapikey_<id>.
 3. R/<id>/# und W/<id>/# abonnieren und nur ins Log schreiben. Schreibwünsche (W) werden
    nie ausgeführt.
 4. Alle 5 s die Werte des Verlaufs zur selben Uhrzeit als N/<id>/<dienst>/<instanz>/<pfad>
    = {"value": x} veröffentlichen, wie dbus-mqtt in Venus.

Braucht paho-mqtt (2.x).
"""

import argparse
import datetime
import json
import os
import re
import ssl
import sys
import time
import urllib.parse
import urllib.request

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vrm_replay as R  # noqa: E402

REG_URL = "https://ccgxlogging.victronenergy.com/log/storemqttpassword.php"
TAKT = 3                        # wie heartbeat des Pi (9 in 25 s)

# Datei im Verlauf -> Thema unter N/<id>/ (Pfade wie auf dem D-Bus von Venus)
THEMEN = [
    ("{V}.vebus.288.Ac.ActiveIn.L1.P", "vebus/288/Ac/ActiveIn/L1/P"),
    ("{V}.vebus.288.Ac.Out.L1.P", "vebus/288/Ac/Out/L1/P"),
    ("{V}.battery.291.Dc.0.Current", "battery/291/Dc/0/Current"),
    ("{V}.battery.291.Dc.0.Voltage", "battery/291/Dc/0/Voltage"),
    ("{V}.battery.291.Soc", "battery/291/Soc"),
    ("DC_battery_P", "battery/291/Dc/0/Power"),
    ("alias.0.VenusOS.DC_battery_I", "battery/512/Dc/0/Current"),
    ("alias.0.VenusOS.DC_battery_T_BMS", "battery/512/Dc/0/Temperature"),
    ("alias.0.VenusOS.DC_battery_MinCellVoltage", "battery/512/System/MinCellVoltage"),
    ("alias.0.VenusOS.DC_battery_MaxCellVoltage", "battery/512/System/MaxCellVoltage"),
    *[(f"DC_MPPT_{n}_P" if n in (288, 291) else f"alias.0.VenusOS.DC_MPPT_{n}_P", f"solarcharger/{n}/Yield/Power")
      for n in (288, 291, 292, 293, 294, 296)],
    *[(f"DC_MPPT_{n}_I" if n in (288, 291) else f"alias.0.VenusOS.DC_MPPT_{n}_I", f"solarcharger/{n}/Dc/0/Current")
      for n in (288, 291, 292, 293, 294, 296)],
    ("AC_powertotal_Haus", "grid/40/Ac/L1/Power"),
    ("AC_powertotal_Haus", "grid/40/Ac/Power"),
    ("AC_powertotal_Haus", "system/0/Ac/Grid/L1/Power"),
    ("{V}.system.0.Ac.Consumption.L1.Power", "system/0/Ac/Consumption/L1/Power"),
    ("{V}.system.0.Ac.ConsumptionOnInput.L1.Power", "system/0/Ac/ConsumptionOnInput/L1/Power"),
    ("alias.0.VenusOS.AC_MP2_PVonGrid_P", "system/0/Ac/PvOnGrid/L1/Power"),
    ("alias.0.VenusOS.DC_PV_total_P", "system/0/Dc/Pv/Power"),
    ("{V}.battery.291.Soc", "system/0/Dc/Battery/Soc"),
    ("DC_battery_P", "system/0/Dc/Battery/Power"),
    ("{V}.battery.291.Dc.0.Current", "system/0/Dc/Battery/Current"),
    ("{V}.battery.291.Dc.0.Voltage", "system/0/Dc/Battery/Voltage"),
    ("alias.0.VenusOS.MP2_system_state", "system/0/SystemState/State"),
]


# Einstellungen, nach denen das VRM-Dashboard fragt. Werte: Vorgaben von Venus (localsettings),
# nicht vom Pi abgelesen.
EINSTELLUNGEN = {
    "settings/0/Settings/System/Units/Temperature": "celsius",
    "settings/0/Settings/System/VolumeUnit": 0,
    "settings/0/Settings/Gps/SpeedUnit": "km/h",
    # Das Dashboard fragt laufend nach system/0/Connected; der Pi meldet 1, ebenso je Gerät
    **{f"{d}/Connected": 1 for d in ("system/0", "vebus/288", "battery/291", "battery/512", "grid/40",
                                       *[f"solarcharger/{n}" for n in (288, 291, 292, 293, 294, 296)])},
}


def log(s):
    t = datetime.datetime.now(R.ZONE).strftime("%H:%M:%S")
    print(f"{t} {s}", flush=True)


def broker(portal_id):
    # Wie _get_vrm_broker_url() in mosquitto_bridge_registrator.py
    return "mqtt%d.victronenergy.com" % (sum(ord(c) for c in portal_id.lower()) % 128)


def passwort(sec):
    """Passwort einmal erzeugen und vor dem Anmelden speichern (wie Venus), dann anmelden."""
    if "mqtt_password" not in sec:
        sec["mqtt_password"] = os.urandom(16).hex()
        tmp = R.SECRETS + ".tmp"
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            json.dump(sec, f, indent=1)
            f.write("\n")
        os.replace(tmp, R.SECRETS)
        log("MQTT-Passwort erzeugt und in vrm_test.json abgelegt")
    body = urllib.parse.urlencode({"identifier": "ccgxapikey_" + sec["portal_id"],
                                   "mqttPassword": sec["mqtt_password"]}).encode()
    req = urllib.request.Request(REG_URL, data=body, headers={
        "Content-Type": "application/x-www-form-urlencoded", "User-Agent": "dbus-mqtt"})
    ctx = ssl.create_default_context(cafile=R.CA)
    with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
        log(f"Passwort bei VRM angemeldet: HTTP {r.status}")
        if r.status != 200:
            sys.exit("Anmeldung abgelehnt")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("verlauf", help="entpackter Tagesordner, z. B. docs/hist_20261118")
    ap.add_argument("--senden", action="store_true", help="wirklich anmelden und veröffentlichen")
    ap.add_argument("--spiegel", help="Test: Themenliste des Pi (TSV: Pfad, Anzahl, Retain, Inhalt) "
                                      "zusätzlich alle 30 s veröffentlichen")
    ap.add_argument("--mqtt311", action="store_true",
                    help="MQTT 3.1.1 statt 5 (Test, ob esp-mqtt in der Firmware reicht)")
    ap.add_argument("--dauer", type=int, default=600, help="Sekunden, dann trennen (Standard 600)")
    args = ap.parse_args()

    m = re.search(r"(\d{8})/?$", args.verlauf)
    if not m:
        sys.exit("Ordnername muss mit dem Datum JJJJMMTT enden")
    tag = datetime.datetime.strptime(m.group(1), "%Y%m%d").replace(tzinfo=R.ZONE)
    venus = None
    for f in os.listdir(args.verlauf):
        mm = re.match(r"history\.(mqtt\.1\.N\.[0-9a-f]+)\.vebus\.288\.Ac\.ActiveIn\.L1\.P\.json$", f)
        if mm:
            venus = mm.group(1)
    if not venus:
        sys.exit("Venus-Dateien nicht gefunden")

    reihen = {}
    for name, _ in THEMEN:
        if name not in reihen:
            r = R.lese_reihe(os.path.join(args.verlauf, "history." + name.replace("{V}", venus) + ".json"))
            reihen[name] = (r, [x[0] for x in r])

    def werte_jetzt():
        # dieselbe Uhrzeit am Tag des Verlaufs
        jetzt = datetime.datetime.now(R.ZONE)
        t = tag.replace(hour=jetzt.hour, minute=jetzt.minute, second=jetzt.second).timestamp()
        out = {}
        for name, thema in THEMEN:
            r, z = reihen[name]
            v = R.wert_bei(r, z, t)
            if v is not None:
                out[thema] = round(v, 3)
        return out

    if not args.senden:
        for k, v in werte_jetzt().items():
            print(f"N/<ID>/{k} {{\"value\": {v}}}")
        print("Probelauf, nichts gesendet.")
        return

    sec = json.load(open(R.SECRETS))
    pid = sec["portal_id"]

    spiegel = {}
    if args.spiegel:
        import ast
        for zeile in open(args.spiegel):
            pfad, _, _, inhalt = zeile.rstrip("\n").split("\t", 3)
            b = ast.literal_eval(inhalt)
            if len(b) >= 80:            # im Mitschnitt gekürzt, nicht verwenden
                continue
            spiegel[pfad] = b.decode(errors="replace")
        # Portal-ID des Pi im Inhalt (z. B. Serial) durch die Test-ID ersetzen
        alt = json.loads(spiegel.get("system/0/Serial", '{"value": ""}')).get("value")
        if alt:
            spiegel = {k: v.replace(alt, pid) for k, v in spiegel.items()}
        log(f"Spiegel: {len(spiegel)} Themen des Pi")
    passwort(sec)

    host = broker(pid)
    # Wie die Brücke von flashmq in Venus v3.75 (data/conf/flashmq.d/vrm_bridge.conf):
    # MQTT 5, Bridge-Bit, Client-ID mit Präfix GXdbus
    cid = "GXdbus_" + os.urandom(6).hex()
    proto = mqtt.MQTTv311 if args.mqtt311 else mqtt.MQTTv5
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=cid, protocol=proto,
                    **({"clean_session": True} if args.mqtt311 else {}))
    c.enable_bridge_mode()      # bridge_protocol_bit true wie in vrm_bridge.conf
    c.username_pw_set("ccgxapikey_" + pid, sec["mqtt_password"])
    c.tls_set(ca_certs=R.CA)
    zustand = {"verbunden": False, "keepalive": 0, "gesendet": 0, "echo": 0}

    def alles_senden(cl):
        for k, v in {**werte_jetzt(), **EINSTELLUNGEN}.items():
            cl.publish(f"N/{pid}/{k}", json.dumps({"value": v}))
        cl.publish(f"N/{pid}/system/0/Serial", json.dumps({"value": pid}), retain=True)

    def on_connect(cl, ud, flags, rc, props=None):
        log(f"verbunden mit {host}:443 als {cid}, MQTT {'3.1.1' if args.mqtt311 else '5'}, Ergebnis {rc}")
        if rc == 0:
            zustand["verbunden"] = True
            # N mit abonnieren: Kommt das Eigene zurück, nimmt der Broker die Themen an
            cl.subscribe([(f"R/{pid}/#", 0), (f"W/{pid}/#", 0), (f"N/{pid}/system/0/Dc/Battery/Soc", 0)])
            cl.publish(f"N/{pid}/system/0/Serial", json.dumps({"value": pid}), retain=True)

    def on_disconnect(cl, ud, flags, rc, props=None):
        zustand["verbunden"] = False
        log(f"getrennt, Ergebnis {rc}")

    def on_message(cl, ud, msg):
        thema = msg.topic.replace(pid, "<ID>")
        inhalt = msg.payload[:120].decode(errors="replace")
        if msg.topic.startswith("W/"):
            log(f"Schreibwunsch, NICHT ausgeführt: {thema} {inhalt}")
            return
        if msg.topic.startswith("N/"):
            zustand["echo"] += 1
            return
        if msg.topic == f"R/{pid}/keepalive":
            # wie dbus-flashmq: ohne suppress-republish alles senden, dann full_publish_completed
            zustand["keepalive"] += 1
            optionen, echo = [], None
            try:
                j = json.loads(msg.payload) if msg.payload.strip() else {}
                optionen = j.get("keepalive-options", [])
                for o in optionen:
                    if isinstance(o, dict) and "full-publish-completed-echo" in o:
                        echo = o["full-publish-completed-echo"]
            except ValueError:
                log(f"keepalive-Optionen nicht lesbar: {inhalt}")
            if "suppress-republish" not in optionen:
                alles_senden(cl)
                fertig = {"value": int(time.time())}
                if echo is not None:
                    fertig["full-publish-completed-echo"] = echo
                cl.publish(f"N/{pid}/full_publish_completed", json.dumps(fertig))
            log(f"keepalive {inhalt or '(leer)'} -> " +
                ("nur Zeitgeber" if "suppress-republish" in optionen else "alles gesendet"))
            return
        log(f"empfangen: {thema} {inhalt or '(leer)'}")
        # Leseanfrage beantworten: N mit dem Wert, wenn bekannt (wie dbus-mqtt)
        pfad = msg.topic.split("/", 2)[2]
        w = {**werte_jetzt(), **EINSTELLUNGEN}
        if pfad == "system/0/Serial":
            cl.publish(f"N/{pid}/system/0/Serial", json.dumps({"value": pid}))
        elif pfad in w:
            cl.publish(f"N/{pid}/{pfad}", json.dumps({"value": w[pfad]}))
            log(f"  beantwortet: N/<ID>/{pfad} {w[pfad]}")
        elif pfad in spiegel:
            cl.publish(f"N/{pid}/{pfad}", spiegel[pfad])
            log(f"  beantwortet aus Spiegel: N/<ID>/{pfad}")
        else:
            log("  unbekannt, keine Antwort")

    c.on_connect, c.on_disconnect, c.on_message = on_connect, on_disconnect, on_message
    c.reconnect_delay_set(min_delay=5, max_delay=60)
    if args.mqtt311:
        c.connect(host, 443, keepalive=60)
    else:
        c.connect(host, 443, keepalive=60, clean_start=True)
    c.loop_start()
    # Werte für SENDDATA (c=1) zur selben Uhrzeit, wie in vrm_replay.py
    w_reihen = {}
    for name, _, _, _ in R.WERTE:
        if name not in w_reihen:
            r = R.lese_reihe(os.path.join(args.verlauf, "history." + name.replace("{V}", venus) + ".json"))
            w_reihen[name] = (r, [x[0] for x in r])
    nonce = R.nonce_quelle()

    def senddata():
        jetzt = datetime.datetime.now(R.ZONE)
        t = tag.replace(hour=jetzt.hour, minute=jetzt.minute, second=jetzt.second).timestamp()
        daten = {}
        for name, code, inst, stellen in R.WERTE:
            r, z = w_reihen[name]
            v = R.wert_bei(r, z, t)
            if v is not None:
                daten[f"{code}[{inst}]"] = R.runde(v, stellen)
        daten.update(R.SYSTEM_WERTE)
        daten["t"] = R.INTERVAL
        now = time.time()
        R.sende([R.zeile(R.SENDDATA, daten, now, now, pid, sec["token"], next(nonce))], pid)
        log(f"SENDDATA geschickt ({len(daten) - 1} Werte)")

    # ANNOUNCE mit cp Bit 8, damit VRM das Keepalive-Verfahren nimmt
    now = time.time()
    konf = {f"{cc}[{i}]": "%x" % p for cc, i, p in R.GERAETE}
    konf.update(R.SYSTEM_KONFIG)
    konf.update(R.KONFIG)
    R.sende([R.zeile(R.ANNOUNCE, R.ANNOUNCE_DATEN, now, now, pid, sec["token"], next(nonce)),
             R.zeile(8, R.PRIORITY_KONFIG, now, now, pid, sec["token"], next(nonce)),
             R.zeile(R.CONFIGCHANGE, konf, now, now, pid, sec["token"], next(nonce))], pid)
    log(f"ANNOUNCE geschickt: {R.ANNOUNCE_DATEN}")

    ende = time.time() + args.dauer
    naechste_meldung = 0
    naechste_senddata = 0
    try:
        while time.time() < ende:
            if time.time() >= naechste_senddata:
                senddata()
                naechste_senddata = time.time() + R.INTERVAL
            if zustand["verbunden"] and spiegel and time.time() >= zustand.get("spiegel_bis", 0):
                eigene = set(werte_jetzt()) | set(EINSTELLUNGEN)
                for k, v in spiegel.items():
                    if k not in eigene and k not in ("heartbeat", "full_publish_completed", "system/0/Serial"):
                        c.publish(f"N/{pid}/{k}", v)
                zustand["spiegel_bis"] = time.time() + 30
            if zustand["verbunden"]:
                w = werte_jetzt()
                for k, v in {**w, **EINSTELLUNGEN}.items():
                    c.publish(f"N/{pid}/{k}", json.dumps({"value": v}))
                c.publish(f"N/{pid}/system/0/Serial", json.dumps({"value": pid}))
                # Der Pi schickt alle paar Sekunden N/<id>/heartbeat mit der Unixzeit
                c.publish(f"N/{pid}/heartbeat", json.dumps({"value": int(time.time())}))
                zustand["gesendet"] += 1
                if time.time() >= naechste_meldung:
                    log(f"Runde {zustand['gesendet']}: {len(w)} Themen, z. B. SoC "
                        f"{w.get('system/0/Dc/Battery/Soc')} %, PV {w.get('system/0/Dc/Pv/Power')} W, "
                        f"keepalive bisher {zustand['keepalive']}, eigene N zurück {zustand['echo']}")
                    naechste_meldung = time.time() + 60
            time.sleep(TAKT)
    finally:
        c.loop_stop()
        c.disconnect()
        log(f"Ende: {zustand['gesendet']} Runden, {zustand['keepalive']} keepalive von VRM")


if __name__ == "__main__":
    main()
