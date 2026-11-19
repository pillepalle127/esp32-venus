# VRM

Die Verbindung zu VRM soll bleiben (Entscheidung vom 18.11.2026). ESP_venus meldet dann selbst an VRM, der Pi kann weg. Hier steht, wie Venus OS v3.75 heute mit VRM spricht. Quelle ist der Code von `vrmlogger` auf meinem Venus (`/opt/victronenergy/vrmlogger/`) und die Brücke in `/data/conf/flashmq.d/vrm_bridge.conf`. Beides liegt nur lokal, es ist Code und Konfiguration von Victron. Eine offizielle Beschreibung dieser Schnittstelle kenne ich nicht, Victron kann sie ändern.

Portal-ID, Token und Passwörter stehen nicht im Repo.

## Werte und Statistik (vrmlogger)

- Ziel: `https://ccgxlogging.victronenergy.com/log/logbulk.php?crc32=<CRC32 der ungepackten Daten>&id=<Portal-ID>`, POST. Header `User-Agent: VE/CCGX/2`.
- Inhalt: eine Zeile je Meldung, form-urlencoded, mit `\n` getrennt, alles mit zlib gepackt (Stufe 9).
- Eine Zeile: `d=2&IMEI=<Portal-ID>&c=<Befehl>&<Code>[<Instanz>]=<Wert>&…&t=<Intervall>`. Dazu `TO=<Alter in s>`, wenn die Meldung älter ist, `VRMAUTHTOKEN=<Token>` und `nonce=<4 Zeichen>:<Zähler>`. Aus `=0.0&` wird `=0&`.
- Antwort: HTTP 200 mit dem Text `vrm: OK`. Bei `Retry-After` wartet Venus höchstens 900 s.
- Intervall: `/Settings/Vrmlogger/LogInterval`, bei mir 300 s.
- Portal-ID: die MAC von eth0 des Pi, ohne Doppelpunkte, klein geschrieben.
- Token: aus dem EEPROM, sonst `/data/conf/vrm_auth_token.txt`. Auf dem Pi hat Venus es beim ersten Start zufällig erzeugt (8 Bytes, hex).

Befehle (`c=`):

| Wert | Name | Wann | Inhalt |
|---|---|---|---|
| 0 | ANNOUNCE | beim Start | Version, Build, Produkt (`mi`), Name, Seriennummer, Laufzeit, freier Speicher, Fähigkeiten (`cp`) |
| 1 | SENDDATA | im Intervall und bei Ereignissen | Messwerte, dazu `rtt`, `hp`, `zp` |
| 2 | CONFIGCHANGE | wenn sich eine Einstellung ändert, 5 s gesammelt | Einstellungen, IP-Adressen |
| 3 | HOURLYDELTAS | alle 15 min | Energie seit dem letzten Mal, daraus macht VRM die kWh-Statistik |
| 6 | TEST_POST | Verbindungstest | nichts, an `log.php` |
| 7 | EVENT | bei Ereignissen | Typ, Schwere, Programm, Text |
| 8 | PRIORITY_CONFIGCHANGE | sofort | Einstellungen für Online-Funktionen |

Welcher D-Bus-Pfad welchen Code bekommt und wann er gesendet wird (`whenToLog`), steht in `datalist.py`, getrennt nach Gerätetyp (`vebus`, `battery`, `solarcharger`, `grid`, `system` und weitere). Die Instanz in eckigen Klammern ist die VRM-Instanz des Geräts, bei mir zum Beispiel `solarcharger:288` bis `296`, `battery:291`, `grid:40`.

Die Energie für Befehl 3 rechnet `kwhdeltas.py` aus den Zählern der Geräte zusammen: Netz, PV an den MPPT, Wege im MultiPlus (Netz zu Batterie, Netz zu AC-out, Batterie zu AC-out und so weiter).

## Live-Ansicht und Fernzugriff (MQTT)

`flashmq` hält zwei Brücken, beide MQTT 5 über TLS auf Port 443:

| Ziel | Themen | Wozu |
|---|---|---|
| `mqtt54.victronenergy.com` | sendet `N/<ID>/#`, empfängt `R/<ID>/#` und `W/<ID>/#` | Live-Werte im Dashboard, Lesen und Schreiben aus VRM |
| `mqtt-rpc.victronenergy.com` | `P/<ID>/out/#` und `P/<ID>/in/#` | Fernaufrufe, zum Beispiel Firmware-Update |

Benutzer ist `ccgxapikey_<ID>`. Das Passwort erzeugt Venus einmal zufällig, speichert es in `/data/conf/mqtt_password.txt` und meldet es per `https://ccgxlogging.victronenergy.com/log/storemqttpassword.php` an (Felder `identifier` und `mqttPassword`). Danach schreibt Venus die Brücke. So steht es in `mosquitto_bridge_registrator.py`, `venus-platform` nutzt dieselbe Datei und dieselbe Adresse. Bei mir hat das Passwort 32 Zeichen aus Buchstaben und Ziffern.

Der Broker für die Live-Werte ergibt sich aus der Portal-ID: Summe der Zeichencodes der ID (klein geschrieben) modulo 128, also `mqtt<Zahl>.victronenergy.com`. Bei mir kommt 54 heraus, so steht es auch in der Brücke.

TLS prüft Venus gegen `/etc/ssl/certs/ccgx-ca.pem`, eine eigene CA von Victron. Den vrmlogger prüft es gegen die üblichen CAs (`ca-certificates.crt`).

So fordert VRM die Live-Werte an (aus `dbus_mqtt.py`, das neuere `flashmq` arbeitet laut Bit 8 der Fähigkeiten im ANNOUNCE ebenfalls mit Keepalive statt gehaltener Nachrichten):

- Nach dem Verbinden meldet Venus `N/<ID>/keepalive` = 1 und `N/<ID>/system/0/Serial` = Portal-ID und abonniert `R/<ID>/#` und `W/<ID>/#`.
- VRM schickt `R/<ID>/keepalive`. Ist der Inhalt leer, will VRM alle Werte. Sonst ist er eine JSON-Liste von Filtern wie `["system/#","vebus/+/Dc/0/Voltage"]`.
- Venus veröffentlicht dann die passenden Werte als `N/<ID>/<Gerätetyp>/<Instanz>/<D-Bus-Pfad>` mit Inhalt `{"value": …}`, bei jeder Änderung höchstens einmal je Sekunde.
- Ohne neues Keepalive läuft ein Filter nach 60 s ab. Venus sendet dann für die betroffenen Themen einen leeren Inhalt.
- `R/<ID>/<Gerätetyp>/<Instanz>/<Pfad>` liest einen Wert einmal, `W/<ID>/…` mit `{"value": …}` schreibt ihn.

Die Fernaufrufe über `mqtt-rpc` (Firmware-Update, VEConfigure aus der Ferne) braucht ESP_venus nicht. Im ANNOUNCE melde ich die zugehörigen Bits dann nicht.

ESP_venus nimmt Portal-ID, Token und Passwort des Pi. Sie stehen in `firmware/src/vrm_secrets.h`, die nicht ins Repo kommt.

## Festgelegt (18.11.2026)

- ESP_venus führt die bestehende Installation weiter, mit Portal-ID und Token des Pi. Verlauf und Statistik in VRM laufen weiter. Der vrmlogger auf dem Pi darf dann nicht mehr senden.
- Beide Wege: Werte und kWh-Statistik (vrmlogger) und die Live-Ansicht (MQTT-Brücke).

## Offen

- Umschalten: vrmlogger und Brücke auf dem Pi abschalten, bevor ESP_venus sendet. Die Brücke hängt an `flashmq`, und über den liest heute auch ioBroker. Das Umschalten für VRM gehört deshalb zum selben Schritt wie das für ioBroker.

## Plan

| Schritt | Was | Der Pi |
|---|---|---|
| V1 Verbindung | ESP_venus schickt einen TEST_POST (`c=6`) an `log.php` und erwartet `vrm: OK`. Dazu baut er die TLS-Verbindung zu `mqtt54` auf, meldet sich an und abonniert `R/<ID>/#`, veröffentlicht aber nichts. | sendet weiter wie bisher |
| V2 Werte vorbereiten | Firmware sendet wie der vrmlogger und die Brücke, aber an die Test-Installation ESP_sim. Werte im Übergang vom Broker des Pi, nur lesend. Täglich über die API neben das legen, was der Pi sendet. | sendet weiter |
| V3 Umschalten | vrmlogger und Brücke auf dem Pi aus, ESP_venus sendet ANNOUNCE, Werte, kWh und Live-Werte. Zusammen mit dem Umschalten für ioBroker. | sendet nicht mehr |
- Welche Codes ESP_venus senden muss: Auszug aus `datalist.py` für MultiPlus, Batterie, MPPT, Netz und System.
- Die Live-Werte nutzen dieselben Themen `N/<ID>/…` wie der MQTT-Broker für ioBroker (README, Abschnitt MQTT). Beides sollte aus einer Quelle kommen.

## Teststand (ab 19.11.2026)

Eine eigene Test-Installation in VRM, getrennt von der echten. Sie bekommt die MAC von ESP_venus als Portal-ID und ein eigenes Token. Dorthin spiele ich den Verlauf eines vergangenen Tages ab, mit `TO=<Alter>` je Zeile.

Quelle ist der Verlauf in ioBroker (Kodi-Box, `/home/iobroker-history/<Datum>/`). Für den 18.11.2026 stehen dort diese Venus-Werte:

| Gerät | Werte | Abstand |
|---|---|---|
| MultiPlus (vebus 288) | `Ac/ActiveIn/L1/P`, `Ac/Out/L1/P` | 2,5 s |
| MultiPlus | Zustand, Modus, Einspeisung aus | nur bei Änderung |
| Batterie (291) | Strom, Spannung | 3 s |
| Batterie | SoC, Leistung, min./max. Zellspannung samt Zelle, Temperaturen | 10 s oder bei Änderung |
| MPPT 288, 291, 292, 293, 294, 296 | Strom, Leistung | 10 s |
| System | Verbrauch L1, Verbrauch am Eingang, Strom am Ausgang, PV gesamt | 10 bis 30 s |
| Netz | Leistung am Netzzähler, geglättet: `lowpass-filter.0.filtered_Values.sonoff_0_Tasm_Kellercenter2_MT681_Power_cur`, im Verlauf unter der Alias-ID `AC_powertotal_Haus` | höchstens alle 30 s (`blockTime`) |

Es fehlen:

- MPPT: PV-Spannung, Ertrag, Zustand
- MultiPlus: Spannungen und Ströme, Frequenz

ioBroker speichert viele Werte nur bei Änderung. Lücken bis zu mehreren Stunden heißen deshalb nicht, dass Werte fehlen.

Zuordnung zu den Codes in `datalist.py` (Zeile = `code[Instanz]=Wert`):

| Wert | Code |
|---|---|
| vebus `Ac/ActiveIn/L1/P`, `Ac/Out/L1/P`, `State`, `Mode` | `IP1`, `OP1`, `S`, `s` |
| battery 291 `Dc/0/Current`, `Dc/0/Voltage`, `Soc` | `I`, `V`, `SOC` |
| battery 512 `Dc/0/Current` | `I` |
| battery 512 `Dc/0/Temperature`, `System/Min-`/`MaxCellVoltage`, `System/Min-`/`MaxCellTemperature` | `BT`, `mcV`/`McV`, `mcT`/`McT` |
| solarcharger `Dc/0/Current`, `Yield/Power` | `ScI`, `PVP` |
| grid `Ac/L1/Power` | `g1p` |
| system `Ac/Consumption/L1/Power`, `Ac/ConsumptionOnInput/L1/Power`, `Ac/PvOnGrid/L1/Power`, `Dc/Pv/Power`, `Ac/Grid/L1/Power`, `SystemState/State` | `a1`, `i1`, `Pi`, `Pdc`, `g1`, `ss` |
| system `Dc/Battery/Soc`, `Dc/Battery/Power` aus battery 291 (`Soc`, `Dc/0/Power`) | `bs`, `bp` |

Quellen der Aliase laut `objects.jsonl` (19.11.2026): `DC_MPPT_<n>_I` = `solarcharger/<n>/Dc/0/Current`, `DC_MPPT_<n>_P` = `solarcharger/<n>/Yield/Power`, `DC_battery_I`/`_U`/`_SOC`/`_T_BMS`/Zellwerte = `battery/512`, `DC_battery_P` und `DC_battery_SOC_SM` = `battery/291`, `DC_PV_total_P` = `system/0/Dc/Pv/Power`, `AC_MP2_PVonGrid_P` = `system/0/Ac/PvOnGrid/L1/Power`, `MP2_system_state` = `system/0/SystemState/State`. `MP2_mode` ist das W-Thema (`vebus/288/Mode`), kein Messwert, daher nicht übernommen. Spannung und SoC von battery 512 stehen im Verlauf nur unter den Namen `DC_battery_U` und `DC_battery_SOC`. Welches Objekt diese Alias-ID trägt, ist nicht geprüft, daher sind sie noch nicht übernommen.

Venus führt im System den SmartShunt: `/ActiveBatteryService` = `com.victronenergy.battery/291`, `/Dc/Battery/Soc` gleich dem SoC von 291 (19.11.2026: 22,5 %, BMS 31 %). `bs` und `bp` kommen deshalb von 291.

VRM erkennt ein Gerät an seinem Produktcode (`vvp`, `BM`, `ScM`, `gP` …, `whenToLog: configChange`). Diese Codes stehen nicht im Verlauf. Venus meldet (dbus, 19.11.2026):

| Dienst | Instanz | ProductId | Gerät | Firmware |
|---|---|---|---|---|
| vebus | 288 | 9763 | MultiPlus-II 48/5000/70-50 | 1288 |
| battery | 291 | 49209 | SmartShunt IP65 300A | 1049 |
| battery | 512 | 45063 | JK-BMS | 4873 |
| solarcharger | 288, 291 | 41056 | SmartSolar MPPT 100/20 48V | 372 |
| solarcharger | 292, 293, 294, 296 | 41063 | BlueSolar MPPT 100/20 48V | 372 |
| grid | 40 | 41393 | Energy Meter VM-3P75CT (Tasmota, `victron_meter.be`) | 66815 |

Die kWh-Statistik (`HOURLYDELTAS`, `c=3`) baue ich im zweiten Durchgang. Netz aus den Zählern, wie Venus sie vom Tasmota bekommt: Bezug = `MT681_Total_in` (echter Zählerstand), Einspeisung = `VIC_Total_out` (im Tasmota aus der Leistung integriert, der MT681 hat kein 2.8.0). Für die Leistungskurve taugt `MT681_Total_in` nicht: Es zählt nur Bezug, und bei 1 Wh Auflösung und 10 s Abstand ergibt der Unterschied Stufen von 360 W.

Skript: `tools/vrm_replay.py <Tagesordner>`. Standard ist der Probelauf, er schreibt die Zeilen lesbar nach `<Tagesordner>/vrm_replay_probe.txt` und sendet nichts. `--id <MAC>` legt `tools/vrm_test.json` an (Portal-ID und neues Token, nicht im Repo), `--senden` schickt an `logbulk.php`. Je Tag: 1 ANNOUNCE, 1 CONFIGCHANGE mit den Produktkennungen, 288 SENDDATA (alle 5 min der letzte Wert davor).

Probelauf 18.11.2026: 290 Zeilen. Um 00:05 fehlen Werte, die history nur bei Änderung speichert und die an dem Tag erst später kamen (z. B. SoC). In 4 von 192 Zeilen ist die kleinste Zellspannung größer als die größte, weil beide zu verschiedenen Zeiten gespeichert sind.

Zertifikat: `ccgxlogging.victronenergy.com` hat ein Zertifikat der „CCGX Certificate Authority“ von Victron, keiner öffentlichen CA. Prüfen geht nur mit `ccgx-ca.pem` (dieselbe CA wie bei MQTT). Gemerkt beim ersten Senden am 19.11.2026: Mit den System-CAs scheitert der Handshake.

19.11.2026, kurz vor 10:34: `--nur-anmelden --senden` an die Test-ID, Antwort `HTTP 200, "vrm: OK"`.
19.11.2026, 10:38: Installation in VRM hinzugefügt (Nutzer), dann 18.11. gesendet: 290 Zeilen in 3 POSTs, alle `HTTP 200, "vrm: OK"`.

VRM danach (Nutzer, 19.11.2026): Geräteliste zeigt Gateway, Battery Monitor (einmal) und Grid meter, je „zuletzt gesehen vor 11 Stunden“. Das passt zur letzten Zeile (18.11. 24:00), VRM hat `TO` also ausgewertet. MultiPlus, die 6 MPPT und die zweite Batterie fehlen. „Erweitert“ zeigt keine Daten. Ursache offen.

VRM-Ansicht (Nutzer, 19.11.2026): Name der Test-Installation „ESP_sim“. Battery Monitor = SmartShunt IP65 300A, VRM-Instanz 291, letzte Verbindung 2026-11-19 00:00:08. „Erweitert“ hat noch keine Widgets ausgewählt; leer heißt dort also nicht, dass Daten fehlen. Ortszeit der Installation 09:40 bei 10:4x in Berlin: Zeitzone vermutlich nicht gesetzt (wir senden kein `tz`).

Vergleich über die VRM-API (`tools/vrm_diag.py`, Endpunkt `diagnostics`, 19.11.2026): Bei ESP_sim sind alle gesendeten Codes angekommen, je Gerät unter der richtigen Instanz (vebus 288, battery 291 und 512, bms 512, solarcharger 288–296, grid 40, system 0). Die MPPT erscheinen unter den Widgets (Nutzer). Die Geräteliste zeigt trotzdem nur Gateway, SmartShunt und Netzzähler; warum, ist offen. Die echte Installation hat zusätzlich unter anderem Firmware (`vvv`, `ScVt`, `BV`), Zustände (`S`, `ScS`, `vcs`), Spannungen/Ströme am MultiPlus (`IV1`, `II1`, `OV1`, `OI1`, `CV`, `CI`), PV-Spannung (`PVV`), Tageserträge (`YT`, `YY`) und Zeitzone (`tz`).

kWh-Codes der echten Installation (Vorlage für den zweiten Durchgang): Netz `dgb`/`dgs`, Batterie `dH21`/`dH22`, MPPT `dYU`, MultiPlus `t1`–`t10`, Systemübersicht `Pb`, `Pc`, `Pg`, `Gb`, `Gc`, `Bc`, `Bg`.

### kWh (zweiter Durchgang, 19.11.2026)

`tools/vrm_replay.py <Tagesordner> --nur-kwh` schickt je 15 min eine Zeile `c=3` (HOURLYDELTAS) mit `Et` (Beginn des Fensters, UTC) und den Deltas. Quellen: Netz aus den Tasmota-Zählern `MT681_Total_in` (`dgb`) und `VIC_Total_out` (`dgs`), alles andere aus den Leistungen integriert (MPPT `dYU`, SmartShunt `dH21`/`dH22`, PV am AC-Eingang, die sechs Wege im MultiPlus aus Eingangs- und Ausgangsleistung). Die Aufteilung auf `Gc`, `Gb`, `Pb`, `Pc`, `Pg`, `Bc`, `Bg` folgt `kwhdeltas.getdeltas()`, mit den Einstellungen der echten Installation (kein DC-System, AC-Eingang 1 = Netz). Dazu geht `tz=Europe/Berlin` mit der Geräteliste.

Gesendet 19.11.2026 10:49, 97 Zeilen, `vrm: OK`. Tagessummen 18.11. laut VRM-API (`stats`, kWh):

| | Gc | Gb | Pb | Pc | Pg | Bc | Bg | gesamt |
|---|---|---|---|---|---|---|---|---|
| echte Installation (Pi) | 3,38 | 0,30 | 11,52 | 1,28 | 0,06 | 6,36 | 0,06 | 22,96 |
| ESP_sim (aus dem Verlauf) | 3,28 | 0,29 | 11,83 | 1,00 | 0,06 | 6,32 | 0,04 | 22,83 |

Größte Abweichung: PV in den Verbrauch (`Pc`) 0,28 kWh weniger, PV in die Batterie (`Pb`) 0,31 kWh mehr. Die Wege im MultiPlus rechnet der Pi aus den Zählern des MultiPlus, ich aus 2,5-s-Leistungswerten. Woher genau die Differenz kommt, ist nicht untersucht.

### Geräteliste (19.11.2026)

Geprüft über die VRM-API (`system-overview`), Schritt für Schritt an ESP_sim:

| Gesendet | Ergebnis |
|---|---|
| Produktkennung und Messwerte | Gateway, SmartShunt (291), Netzzähler |
| dazu Firmware (`ScVt`, `BV`, `gv`, `vvv`) und `Bm` | dazu die 6 MPPT |
| dazu VE.Bus-Konfiguration (`vmc`, `vid`, `PC`, `sa`, `pmk2`, `vmk2`) | keine Änderung |
| dazu einmal `S`, `CV`, `CI` vom MultiPlus und `V`, `SOC` vom JK-BMS (aktuelle Werte des Pi aus der API) | dazu MultiPlus (288) und JK-BMS (512) |
| `mi=C003` in ANNOUNCE | Gateway „Generic Venus Device (rpi or other)“ statt „Color Control GX“ |

Ein Gerät erscheint also erst mit Firmware-Code und bestimmten Messwerten. Welcher der Werte `S`, `CV`, `CI` beim MultiPlus und `V`, `SOC` beim BMS den Ausschlag gibt, habe ich nicht einzeln geprüft. Die Firmware sendet ohnehin alle.

### Live-Ansicht, erster Test (19.11.2026)

`tools/vrm_live.py` (paho-mqtt 2.1): MQTT-Passwort für ESP_sim erzeugt und über `storemqttpassword.php` angemeldet (HTTP 200), Verbindung zu `mqtt67.victronenergy.com:443` steht. Der Broker nimmt `N/<ID>/…` an (PUBACK `Success`, ein anderes Thema `Not authorized`). VRM schickt bei offenem Dashboard alle 30 s `R/<ID>/system/0/Serial` mit `{"keepalive-options":["suppress-republish"]}`, beim Öffnen auch Leseanfragen für `battery/291/…` und drei Einheiten-Einstellungen.

Ergebnis: Das Dashboard zeigt nur die Werte der letzten SENDDATA-Zeile (alle 5 min), nicht die MQTT-Werte. Probiert ohne Unterschied: MQTT 3.1.1 und 5, Client-ID `ccgx_…` und `GXdbus_…`, Bridge-Bit, Antworten auf die Leseanfragen. Solange die letzte SENDDATA-Zeile 12 h alt war, galt ESP_sim als offline. Ursache offen.

### Live-Ansicht, Ergebnis (19.11.2026)

Das Dashboard von ESP_sim zeigt „Echtzeit“ und die MQTT-Werte. Nötig waren, alles am Code der VRM-Seite (`app.js`) und am Mitschnitt des Pi geprüft:

1. **`view_permissions.twoway`** der Installation (VRM-API `installations?extended=1`). Ohne diese Freigabe verbindet sich die Seite gar nicht für Live-Werte. VRM setzt sie erst, wenn ANNOUNCE Bit 10 in `cp` meldet („Uses new vrm config data attributes, part of 2024 security project“) und der Portal-Modus `vrm=2` gemeldet ist. Gegenprobe: `cp=256` → false, `cp=1280` → true. ESP_venus meldet `cp = 1280` (Bit 8 und 10).
2. **Nach jedem ANNOUNCE** gleich PRIORITY_CONFIGCHANGE (`vrm=2`) und CONFIGCHANGE schicken, wie der vrmlogger. Sonst vergisst VRM `vrm`, `sl`, `tz` usw.
3. **MQTT-Brücke** wie flashmq in Venus v3.75: Client-ID `GXdbus_…`, Benutzer `ccgxapikey_<id>`, Passwort über `storemqttpassword.php`. MQTT 5 und Bridge-Bit sind nicht nötig (siehe unten).
4. **Was die Seite erwartet:** Sie schickt alle 30 s `R/<id>/system/0/Serial` mit `suppress-republish` als Keepalive und fragt `R/<id>/system/0/Connected`. „Echtzeit“ zeigt sie, solange nicht gespeicherte (retain 0) `N/<id>/…`-Nachrichten kommen. ESP_venus schickt `system/0/Connected = 1`, `heartbeat` (Unixzeit) alle 3 s und `system/0/Serial` mit Retain, wie der Pi.

Bis 12:00 offen: Das Dashboard blieb bis auf Verbrauch, Solar und Batterie leer; beim Pi zeigt es die Topologie mit allen Einzelwerten. Auch 4896 gespiegelte Venus-Themen des Pi und die Aufbau-Codes (`si1`, `shao`, `shai`, `st`, `AIS`, `vi0` …) haben daran nichts geändert. VRM führt ESP_sim noch mit `hasMains = 0` (Pi: 1).

19.11.2026, gegen 12:01: Das Dashboard von ESP_sim zeigt die Topologie mit den Einzelwerten (Nutzer), VRM führt ESP_sim jetzt mit `hasMains = 1`. Davor: um 11:58 die Aufbau-Codes (`si1`, `si2`, `shao`, `shai`, `shd`, `sb`, `st`, `H4M`, `bol`, `rwgm` als CONFIGCHANGE, `abs`, `AIS`, `AIT`, `ini`, `ic0`, `id0`, `vi0`, `ist0`, `is0` mit SENDDATA), um 12:01 ein Neustart des Live-Skripts mit ANNOUNCE, Konfiguration und SENDDATA samt dieser Codes. Welcher Teil den Ausschlag gab und ob VRM nur Zeit brauchte, ist nicht getrennt geprüft. ESP_venus schickt alles davon.

19.11.2026, 12:05: Live-Ansicht mit MQTT 3.1.1 und Bridge-Bit (`tools/vrm_live.py --mqtt311`): „Echtzeit“ und Topologie wie mit MQTT 5 (Nutzer). esp-mqtt in der Firmware (nur 3.1.1) reicht also.

19.11.2026, ab 12:09: MQTT 3.1.1 ohne Bridge-Bit (`--mqtt311 --ohne-bridge`): ebenfalls „Echtzeit“ (Nutzer). Das Bridge-Bit ist also nicht nötig, esp-mqtt bleibt unverändert.

## Firmware V2 (19.11.2026)

Ziel ist ESP_sim: `firmware/src/vrm_secrets.h` hat die Zugangsdaten aus `tools/vrm_test.json`, die des Pi liegen in `vrm_secrets_pi.h` (beide nicht im Repo).

- **Tabelle** `firmware/src/vrm_tabelle.h`, erzeugt von `tools/vrm_tabelle.py` aus `datalist.py` und der Themenliste des Pi vom 19.11.2026: jeder Code, den der vrmlogger für die zehn Geräte und das System sendet und den der Pi hat. 471 Themen: 375 mit jeder SENDDATA-Zeile, 22 nur nach Änderung, 64 als Konfiguration, 10 nur für kWh und Live-Ansicht. Ausgelassen: `…/Devices/0/VregLink` (8×), dafür fragt Venus den D-Bus ab. Firmware-Versionen gehen als fester Text, über MQTT kommt nur die Zahl, den Text bildet jeder Treiber selbst (`v4.19`, `v19.9`, `508`).
- **Quelle** (`vrm_quelle.cpp`): abonniert die 471 Themen unter `N/<ID des Pi>/` auf `192.168.1.10:1883`. Sendet dort nichts, auch kein Keepalive. Anmeldung als `remoteconsole` wie ioBroker (`mqtt.1`); ohne Benutzer antwortet der Broker „Not authorized“ (19.11.2026). Laut meinen Notizen zur Kellerlüftung (26.10.2026) schickt Venus laufend alle Werte, auch unveränderte, höchstens 13 s alt; Mithören reicht also.
- **Logger** (`vrm.cpp`): 60 s nach dem Verbinden mit der Quelle ANNOUNCE, `vrm=2`, CONFIGCHANGE, erste SENDDATA. Danach alle 300 s SENDDATA mit den laufenden kWh, zu jeder Viertelstunde HOURLYDELTAS. Geänderte Konfiguration nach 5 s als CONFIGCHANGE. Ist die Quelle getrennt, fällt die SENDDATA-Zeile aus. Gesendet wird gebündelt an `logbulk.php`, zlib mit ungepackten Blöcken (vom Server angenommen, Probe 19.11.2026). Was nicht ankommt, bleibt in der Warteschlange, bis 1,5 MB im PSRAM, ohne Speichern über einen Neustart.
- **Rundung** wie `monitor.py`: nur bei 1 oder mehr Nachkommastellen, Werte mit 0 Stellen gehen ungerundet raus.
- **kWh** wie `kwhdeltas.py`, aus den Zählern: `vebus/288/Energy/*`, `solarcharger/*/Yield/User`, `grid/40/Ac/Energy/*`, `pvinverter/*/Ac/Energy/Forward`, Zähler der aktiven Batterie, `system/0/Timers/*`. Ohne DC-System, beide AC-Eingänge zählen als Netz (`si2 = 0`).
- **Brücke**: MQTT 3.1.1 zu `mqtt<n>.victronenergy.com:443`, Client-ID `GXdbus_…`, Passwort beim Start über `storemqttpassword.php` angemeldet. `heartbeat` alle 3 s; geänderte Werte nur, solange die VRM-Seite fragt (letzte Anfrage unter 70 s); nach `R/<ID>/keepalive` ohne `suppress-republish` alles und `full_publish_completed`. Leseanfragen beantwortet sie aus der Tabelle, Schreibwünsche schreibt sie nur ins Log.
- **Webseite**, Reiter VRM: Zustand, Quelle, Warteschlange, letzte Antwort, Brücke, „senden an/aus“ (bis zum Neustart), Verbindungstest. `/vrm.txt` zeigt die letzte SENDDATA-Zeile, ein Wert je Zeile, ohne Token.

Anders als Venus: Venus schickt bei Änderung eines Werts mit `onIntervalAlwaysAndOnEvent` sofort eine weitere SENDDATA-Zeile. ESP_venus schickt nur alle 300 s.

### Erster Lauf auf ESP_venus (19.11.2026)

- 14:22: Quelle verbunden (Anmeldung als `remoteconsole`), 367 von 471 Themen mit Wert.
- Zuerst scheiterten Logger und Brücke mit `mbedtls_ssl_setup returned -0x7F00` (`MBEDTLS_ERR_SSL_ALLOC_FAILED`): Arduino stellt mbedtls auf internen RAM (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`), frei waren rund 50 kB. Seit `mbedtls_platform_set_calloc_free` größere Stücke aus dem PSRAM holt, geht es; interner RAM danach mindestens 29 kB frei.
- 14:31: ANNOUNCE, Konfiguration und erste SENDDATA (299 Werte) angenommen („vrm: OK“), Passwort angemeldet (HTTP 200), Brücke verbunden.
- Dashboard von ESP_sim: „Echtzeit“, Werte bewegen sich (Nutzer).
- Offen: Vergleich ESP_sim mit dem Pi über die API nach einem vollen Tag (Schritt 6); warum 104 Themen ohne Wert bleiben.
