# ESP32-S3 statt Venus OS

Ich will die Funktionen von Venus OS, die ich nutze, auf einem ESP32-S3 nachbauen. Eine Portierung geht nicht: Venus OS ist eine Linux-Distribution, und ein kompletter Build braucht private Repos von Victron.

Gesamtplan: [PLAN.md](PLAN.md). Testplan mit Ergebnissen: [docs/testplan.md](docs/testplan.md). Ausführliche Protokolle: [docs/tests.md](docs/tests.md).

## Ziel

1. Den Pi ersetzen (`raspberrypi2`, 192.168.1.10).
2. Den Netzzähler gleich mit übernehmen (MT681, heute `victron_meter.be` auf Tasm_Kellercenter2). Später ziehen auch Lüfter und BME280 um, dann entfällt der Tasmota ganz.
3. Zum Schluss zwei OpenDTU und damit zwei Wechselrichter ersetzen: die OpenDTU (HMS Garage) und die OpenDTU-OnBattery (DTUoB_WP).

## Anlage laut Venus (02.11.2026)

| Dienst | Instanz | Gerät | Anschluss |
|---|---|---|---|
| `vebus.ttyUSB0` | 288 | MultiPlus-II 48/5000/70-50 | VE.Bus |
| `solarcharger.ttyUSB1` | 293 | BlueSolar MPPT 100/20 48V | USB |
| `solarcharger.ttyUSB2` | 294 | BlueSolar MPPT 100/20 48V | USB |
| `solarcharger.ttyUSB4` | 296 | BlueSolar MPPT 100/20 48V | USB |
| `solarcharger.ttyUSB5` | 291 | SmartSolar MPPT 100/20 48V | USB |
| `solarcharger.ttyUSB6` | 288 | SmartSolar MPPT 100/20 48V | USB |
| `solarcharger.ttyUSB7` | 292 | BlueSolar MPPT 100/20 48V | USB |
| `battery.ttyUSB8` | 291 | SmartShunt IP65 300A | USB |
| `battery.socketcan_can1` | 512 | JK-BMS | CAN |
| `grid.ve_TM974E88` | 40 | Tasmota als VM-3P75CT | Modbus UDP 192.168.1.33 |
| `pvinverter.http_34` | 34 | `henne49_dbus-opendtu` | HTTP |
| `pvinverter.mqtt_pv_100` | 100 | Growatt MIC, über `dbus-mqtt-pv` | MQTT |

- DVCC ist aktiv.
- Die seriellen Anschlüsse laufen heute über 2 × FT4232 am Pi, alle Kanäle mit Isolatoren.
- Firmware der MPPT v1.74 (`372` = `0x174`), MultiPlus v508 (`1288` = `0x508`).
- `can1`: 500 kbit/s, MCP2515 am Pi. Das schafft der TWAI des S3 auch.
- Die MPPT laufen nachts aus der Batterie und lassen sich lesen.

## Entscheidungen

- Ein ESP32-S3 für alles. Durch die Streuung rechne ich nicht mit mehr Verfügbarkeit, wenn ich aufteile.
- Die FT4232 ersetze ich durch einen CH348. Der läuft allerdings nicht an Venus.
- Von OpenDTU-OnBattery nutze ich nur Überwachung und Steuerung des Hoymiles HMS zur Einspeisung. Es ist deshalb keine Basis für dieses Projekt.
- VRM fällt weg. Der VRM-Prognosevergleich endet dann vermutlich mit dem Umschalten.

## Serielle Anschlüsse

- Der USB-Host des S3 kann nur Full Speed und hat 8 Kanäle, jeder Endpunkt belegt einen. 2 × FT4232 geht deshalb nicht, schon einer braucht 9.
- Der CH348 läuft am S3 mit Full Speed über 4 Bulk-Endpunkte, mit EP0 sind das 5 von 8 Kanälen. Sein Protokoll ist nicht offen. Die Befehle in meinem Treiber stammen aus `ch9344ser_linux` von WCH (GPL-2.0), verglichen mit dem ESPHome-PR #14586.
- Für die beiden RS485 zu den Packs nehme ich einen SC16IS752 (CJMCU-752): SPI, 2 UARTs, 64 Byte FIFO, offenes Datenblatt von NXP, ESP-IDF-Treiber vorhanden. Der CH9434 wäre passend, ist aber sehr schlecht verfügbar.
- Pegel: Die MPPT senden mit 5 V, die Isolatoren bleiben. Der CH348 läuft laut Datenblatt nur mit 3,3 V (TXD high mindestens VCC − 0,4 V, RXD high ab 2,0 V), die Isolatoren laufen auf seiner Seite deshalb mit 3,3 V. 5 V vertragen nur RXD0, RXD2, RXD4 und RXD7. Die GPIOs des S3 vertragen gar keine 5 V.

## Belegung (wie verdrahtet, 14.11.2026)

| Funktion | Signal | GPIO | Anschluss |
|---|---|---|---|
| VE.Bus, UART1, 256000 Baud | RX / TX | 5 / 4 | MAX13487, über ADuM1201 und B0505S isoliert |
| MT681 Haus, UART2, 9600 Baud | RX | 7 | Pfostenstecker MT681: GND, 3V3, Haus, WP |
| MT681 Wärmepumpe, RMT, 9600 Baud | RX | 6 | Pfostenstecker MT681 |
| Kellerlüftung, I²C | SDA / SCL | 16 / 15 | die beiden BME280 (0x76 außen, 0x77), je ein Pfostenstecker: GND, 3V3, SDA, SCL |
| Funk, ein CMT2300A für beide HMS | CSB, SDIO, SCLK, FCSB | 8, 3, 46, 9 | Modul per Pigtail oben am S3 angelötet, vier Pins nebeneinander, dazu VCC an 3V3 und GND. GPIO1 bis GPIO3 des Moduls bleiben frei |
| Kellerlüftung | Lüfter | 11 | Relais |
| Reserve | Relais 2 | 12 | Relais |
| JK-BMS, TWAI, 500 kbit/s | TX / RX | 14 / 13 | TJA1050, über ADuM1201 und B0505S isoliert |
| JK-BMS, RS485 je Pack, SC16IS752 per SPI | CS, SI, SO, SCLK | 2, 42, 41, 38 | CJMCU-752 mit Quarz 1,8432 MHz (höchstens 115200 Baud), UART A und B je über ADuM1201, B0505S und MAX13487 isoliert, nur für die Zellspannungen |
| RGB-LED des Boards | WS2812 | 48 | über die Lötbrücke „RGB“, Farbverlauf in 15 s |
| Reserve | | 1, 10, 17, 18, 21, 39, 40, 47 | Das E-Paper entfällt vorerst, für eins würden die Pins reichen. |
| Flashen, UART0 beim Start | TX / RX | 43 / 44 | COM-Buchse auf dem Board |
| CH348, USB-Host | D− / D+ | 19 / 20 | native USB-Buchse, 5 V über die Brücke `USB-OTG` |
| nicht beschalten | Strapping | 0, 45 | |
| belegt | Octal-PSRAM | 33–37 | |

| CH348 | Gerät |
|---|---|
| Port 0–5 | die 6 MPPT, über die vorhandenen Isolatoren |
| Port 6 | SmartShunt, über den vorhandenen Isolator |
| Port 7 | frei, für einen weiteren MPPT |

- Zum JK-BMS laufen zwei Wege. **CAN steuert:** Ladegrenzen, Alarme, SoC, beide Packs zusammengefasst, ohne CAN keine DVCC-Vorgaben. **RS485 zeigt an:** einzelne Zellspannungen, die Regelung hängt nicht daran.
- Der Netzzähler bleibt am eigenen UART, er liefert das Regelsignal fürs ESS. Der zweite MT681 läuft per RMT als Empfangs-UART. Die Zellspannungen beider Packs laufen über den SC16IS752, UART0 bleibt Konsole.
- Welcher MPPT an welchem Port hängt, spielt keine Rolle: Die Firmware erkennt jedes Gerät an seiner Seriennummer und gibt ihm die Instanz von heute.
- GPIO 3 und 46 sind Strapping-Pins und tragen nur Signale des CMT2300A, die beim Start niemand treibt. GPIO 46 muss beim Reset low sein, sonst gibt es keinen Download-Modus. Der interne Pull-down hält ihn low, SCLK ist am CMT2300A ein Eingang. GPIO 3 wirkt nach meiner Erinnerung nur mit gesetzter eFuse und dann nur auf die Wahl der JTAG-Quelle.
- Ins Netz geht der S3 über sein WLAN. Ein Ausfall trifft nur die Verbindung zu ioBroker, nicht die Regelung.
- VE.Bus braucht einen UART am S3: Alle 20 ms kommt ein Sync-Frame, gesendet wird nur in einem engen Fenster danach.
- Beide HMS laufen über ein CMT2300A. Der Standort läuft seit Jahren, eine Reichweitenfrage gibt es nicht.

## Trennung und Transceiver

Lochraster-Plan: [docs/layout_lochraster.drawio](docs/layout_lochraster.drawio), Belegung der Stiftleisten: [docs/layout_lochraster.md](docs/layout_lochraster.md).

- Die BMS schalten im Minus-Zweig. Hängt der S3 ohne Trennung an zwei Stellen, überbrückt seine Masse einen offenen BMS-Schalter. Ich trenne deshalb VE.Bus, CAN und RS485, jeweils an TX und RX, mit ADuM1201 und einem eigenen B0505S-1W je Busseite. GND bleibt am Bus.
- Das ist eine Funktionstrennung gegen rund 60 V, kein Schutz vor Netzspannung. Prüfungen mit kV schaden: Der ADuM1201 ist für 2500 V rms über 1 Minute ausgelegt (Datenblatt, Tabelle 10). Es reicht der Durchgangstest.
- Der ADuM1201 hängt auf der S3-Seite an 3,3 V, auf der Busseite an den 5 V des B0505S. Kanal A läuft von Seite 2 nach Seite 1, Kanal B umgekehrt. Auf dem Modul heißt die Ziffer 1 Eingang und 0 Ausgang: TX vom S3 geht auf A1 oder B1, RX kommt von A0 oder B0. Bei CAN zeigt Seite 1 zum S3, bei VE.Bus und den beiden RS485 zu den Packs Seite 2, damit die Drähte zum MAX13487 kreuzungsfrei laufen. Die ADuM1201 der beiden Packs hängen unter der Platine, mit den Bauteilen zur Platine. Von oben gesehen liegt Seite 1 zum MAX13487 als U1, A0, B1, G1: VCC, TXD, RXD und GND gehen gerade hinüber. Pack 1 hängt an UART B, Pack 2 an UART A: So liegen die Wege ineinander, und es kreuzen sich nur TX und RX je Pack einmal. Jedes Pack hat seine eigene Trennung, eine gemeinsame würde ihre Massen über den SC16IS752 verbinden.
- Der B0505S ist ungeregelt. Der MAX13487 braucht laut Datenblatt 4,75–5,25 V, der TJA1050 nach meiner Erinnerung ebenso. Ist die Spannung bei wenig Last zu hoch, hilft ein Lastwiderstand: 250 Ω sind 10 % Last.
- Der ADuM1201AR reicht für CAN: rund 0,8 µs Laufzeit im schlechtesten Fall, der S3 tastet nach 1,6 µs ab. Der Selbsttest am Tisch lief fehlerfrei.
- **MAX13487 (VE.Bus, RS485):** schaltet die Richtung selbst, DE entfällt. `ve.bus-library` ist laut Doku damit am MultiPlus getestet. Der Aufdruck muss `MAX13487EESA` sein. Vor dem Anschluss an VE.Bus: A gegen B ohne 120-Ω-Abschluss, Lötbrücke 120_S offen.
- **TJA1050 (CAN):** ohne Quarz, der S3 hat TWAI. Das Modul bringt einen 120-Ω-Abschluss mit, auf dem Bus sind nur zwei erlaubt.
- Fertige isolierte Module wie das XY-S485 nicht an VE.Bus. Am JK-BMS möglich, wenn GND der TTL-Seite gegen GND an den Klemmen offen ist.
- Die Relaistreiber stehen nicht im Lochraster-Plan: Die echte Platine ist kleiner als der Plan, trotz doppelstöckiger Bestückung ist dort kein Platz. Schaltung unter [Relaistreiber](#relaistreiber).

## Relaistreiber

Zwei 5-V-Relais von Finder, je mit einem AO3402, der die Spule nach GND schaltet. Festgelegt am 04.11.2026. GPIO high schaltet das Relais ein.

| Relais | GPIO | In der Firmware |
|---|---|---|
| Lüfter, Kellerlüftung | 11 | `FAN_RELAY` |
| Relais 2, Reserve | 12 | `RELAY2` |

```
                 +5V
                  │
          ┌───────┤
          │       │
   Diode  ▲     Spule
 Kathode  │       │
 an +5V   │       │
          └───────┤
                  │
                  D
GPIO ──┬── 100 Ω ─ G   AO3402
       │          S
     10 kΩ        │
       │          │
      GND        GND
```

| Teil | Wert | Wozu |
|---|---|---|
| Schalter | AO3402 von AOS, SOT-23 | Laut Datenblatt höchstens 85 mΩ bei 2,5 V Gate-Spannung, Schwelle höchstens 1,5 V. Der GPIO liefert 3,3 V. |
| Gate-Widerstand | 100 Ω, GPIO zum Gate | begrenzt den Umladestrom des Gates |
| Pull-down | 10 kΩ, Gate nach GND | hält das Relais aus, solange der GPIO beim Start nicht getrieben ist |
| Freilaufdiode | 1N4007 oder 1N4148 parallel zur Spule, Kathode an +5 V | nimmt die Abschaltspitze der Spule auf |
| Spule | an der 5-V-Schiene | ihr Strom kommt zur Last der 5-V-Einspeisung dazu |

- Bei 100 mA Spulenstrom fallen am AO3402 rund 9 mV ab, die Wärme bleibt unter 1 mW. Er braucht keine Kühlfläche.
- Andere Hersteller und eine ältere AOS-Fassung nennen bis zu 110 mΩ. Bei diesem Strom spielt das keine Rolle.
- Hat der Finder-Sockel schon eine Schutzdiode, entfällt die externe. Dann + und − an der Spule richtig anschließen.
- SOT-23 sitzt auf einem Adapter auf 2,54 mm. Belegung von oben laut Datenblatt: D allein oben, G links unten, S rechts unten.
- Nicht genommen: 2N7000, MTD3055VL, BUK9225-55A und BUK9226-75A nennen ihren Widerstand erst ab 4,5–5 V Gate-Spannung, an 3,3 V ist er nicht garantiert. Der VN610SP ist ein High-Side-Schalter und braucht mindestens 5,5 V. Das Modul HW-532 (PC817 und MOSFET) gibt dem Gate nur die halbe Lastspannung, an 5 V also rund 2,5 V.
- Firmware: Nach dem Start bleiben GPIO 11 und 12 Eingang, erst ein Knopf auf der Seite macht sie zum Ausgang. Test A10 im [Testplan](docs/testplan.md).

## DVCC

DVCC ist aktiv, der S3 muss es also ersetzen: die Ladegrenzen vom JK-BMS lesen und an alle sechs MPPT und den MultiPlus weitergeben.

Laut MPPT-HEX-Protokoll von Victron:
- **External Control (ESS, DVCC):** `0x200E` = `b0000101`, Vorgaben in `0x2001` und `0x2015`. Ohne Daten fällt der MPPT nach 1 Minute auf Stand-alone zurück.
- **BMS-Modus:** `0x200E` = `b0001001`. Der MPPT setzt dabei `BMS present` (`0xEDE8`) und speichert das im Flash. Danach läuft er nicht mehr alleine: Ohne Daten hört er auf zu laden und zeigt Fehler #67. Morgens lädt er 45 s in Float, damit das BMS starten kann.
- Ab Firmware 1.53 unterdrücken HEX-Anfragen den Text-Modus nicht mehr. Textblöcke kommen im Abstand von 1 s.

Auf allen sechs MPPT steht `NetworkMode` = 13 (Networked, External Control, BMS) und `BmsPresent` = 1. Ein umgesteckter MPPT hört deshalb tagsüber auf zu laden, bis der S3 ihm Vorgaben schickt. Bleiben die Vorgaben vom S3 aus, stoppen alle sechs mit Fehler #67.

Am 04.11.2026 gesehen ([Mitschnitt](docs/venus_mppt_0v_2026-11-04.txt)): Ohne Netzzähler und mit `RunWithoutGridMeter` = 0 meldet der MP2 `/Hub/ChargeVoltage` 0.0. DVCC gibt den MPPT dann 0 V, und sie laden nicht. `Mode` = 3 allein half nicht, `RunWithoutGridMeter` = 1 schon. Der S3 schickt den MPPT nie 0 V.

## Software

- Firmware in `firmware/`: PlatformIO mit Arduino 3.3.12 auf ESP-IDF 5.5.5 (`pioarduino`). Der USB-Treiber braucht ein aktuelles IDF, und beide Bibliotheken bauen auf Arduino auf.
- VE.Bus: `ve.bus-library` (MIT). Getestet auf LilyGo T-CAN485, der S3 ist nicht bestätigt.
- Hoymiles: `lib/Hoymiles` aus OpenDTU, laut Dateiköpfen GPL-2.0-or-later. Herkunft der fremden Bibliotheken: `firmware/lib/README.md`. Der CH348-Treiber liegt in `firmware/lib/ch348/`.
- Neu: VE.Direct, SML, JK-BMS über CAN, DVCC, ESS-Regelung, Stützung der Wärmepumpe, MQTT, Webseite.
- Aus `victron_meter.be` übernehmen: Der MT681 hat keinen Einspeisezähler (2.8.0), die Einspeisung wird integriert und alle 10 min gesichert. Der Zähler ist einphasig auf L1 abgebildet.

ioBroker soll nichts merken: gleiche Adresse 192.168.1.10:1883, Benutzer `remoteconsole`, Portal-ID `b827ebc35935`, gleiche Themen, Werte als `{"value": x}`. Dazu braucht der S3 einen eigenen MQTT-Broker.
- Lesen: 38 N-Themen, die Bereiche stehen in `~/projects/Kellerlüftung/mqtt1_abo.txt`.
- Schreiben: `AcPowerSetPoint`, `vebus.288.Mode`, `BatteryLife/State`.
- Dazu die Werte, die heute der Tasmota an ioBroker schickt (`MT681_Power_cur`, `MT681_Total_in`, `VIC`).
- `mqtt_pv_100` ist der Growatt MIC. Venus bindet ihn mit [venus-os_dbus-mqtt-pv](https://github.com/mr-manuel/venus-os_dbus-mqtt-pv) ein: höchstens 1600 W, AC-Eingang 1, Topic `Growatt_fake/power` am Broker 192.168.1.4. Kommt 60 s nichts, beendet sich der Treiber. Das Topic schreibt das ioBroker-Skript `script.js.common.Photovoltaik` (Adapter `javascript.0`), mit `power`, `current` und `energy_forward`. Das muss weiter ankommen.

## VE.Bus laut Quellen (nicht von mir gemessen)

- 256000 Baud, 8N1. Sync-Frame alle 20 ms.
- `ve.bus-library` sendet nur 8,0–9,5 ms nach dem Sync und lässt sonst einen Sync aus.
- Der ESS-Sollwert muss spätestens alle 60 s kommen, sonst geht der MultiPlus in Passthrough.
- Vor OTA oder Neustart den Bus stumm schalten (`setTxEnabled(false)`).
- esp32ess läuft auf einem MultiPlus-II 48/5000 mit Firmware v502 (meiner: v508) und kann nur ein Gerät (einphasig). Es meldet 1–2 % fehlgeschlagene Befehle, die wiederholt werden.
- Der Bus muss hochohmig bleiben, über 100 kΩ, sonst schaltet sich das Victron-Gerät ab.
- USB des ESP32 und MultiPlus nie gleichzeitig anschließen (README von PepeTheFroggie).

## Stand

- **CH348 (03.11.2026):** am S3 erkannt, Schleifentest über alle 8 Ports 92 min ohne Fehler, 55,2 MB.
- **VE.Bus-Timing (03.11.2026):** Ein zweiter S3 spielt den MultiPlus. 9000 von 9000 Antworten, Beginn 8,95–10,53 ms nach dem Sync. Unter künstlicher Last Core 1 bei 5 %, Core 0 bei rund 50 %. Ob ein echter MultiPlus diese Zeitpunkte annimmt, zeigt erst Phase 2.
- **Testfirmware (Env `s3`, 14.11.2026):** Eine Webseite zeigt, was an den Anschlüssen ankommt: `http://<IP>/` oder `http://esp32-venus.local/`. Gesendet wird nur auf Knopfdruck (RS485-Test, CAN-Selbsttest), sonst hört der S3 nur zu. Die Seite hat keine Anmeldung. Das WLAN steht in `firmware/src/wifi_secrets.h`, nicht im Repo. Die alte Gesamtfirmware mit CH348-Schleifentest baut als Env `s3_timing` aus `firmware/timing/`.
- **Am Tisch geprüft (14.11.2026):** beide BME280, beide MT681 am echten Zähler (RMT ohne Rahmenfehler), CAN-Kette per Selbsttest, CH348 mit einem MPPT, Isolation, TX-Pegel bei Reset. Die linke RS485-Kette ist noch offen.
- **Plan (15.11.2026):** Rechts sitzen jetzt der SC16IS752 und darunter die beiden RS485 zu den Packs, die E-Paper-Stiftleiste entfällt. Draht- und Netzliste geprüft.
- **Bekannt:** Nach einem Neustart des S3 meldet sich der CH348 nur, wenn er selbst kurz ohne Strom war. Im Fritz-Mesh muss der S3 den stärksten Zugangspunkt wählen, das tut die Firmware jetzt.

## Kabelwege und Platz des S3

| Verbindung | Art | Länge |
|---|---|---|
| VE.Bus | RS485, RJ45-Kabel direkt vom S3 zum MultiPlus | unkritisch |
| JK-BMS | CAN, differentiell | unkritisch |
| CH348 | USB, 5 V und Daten | freifliegend am USB-Kabel, bis etwa 3 m |
| VE.Direct | 19200 Baud über die Isolatoren | wie heute am FT4232, wenn der CH348 dort sitzt |
| MT681 | 9600 Baud, 3,3 V | 2 m sind in Ordnung |
| Lüfter | Relais | unkritisch |
| BME280 | I²C | empfindlichste Leitung, nicht länger als heute am Tasmota |
| Funk | SPI, direkt am S3 angelötet | der Platz des S3 ist auch der Platz des Funks |

Die Längenangaben sind Erfahrungswerte, gemessen habe ich sie nicht. Der S3 gehört deshalb eher zu BME280, Zählern und dem heutigen Platz der DTUs als zum MultiPlus.

## JK-BMS auf CAN (Mitschnitt 03.11.2026)

Rohdaten: [docs/venus_can_seriennummern_2026-11-03.txt](docs/venus_can_seriennummern_2026-11-03.txt). 21 IDs, zusammen rund 27 Telegramme pro Sekunde. Gelesen nach dem üblichen Victron-Format, an der Anzeige von Venus nicht geprüft.

| ID | Inhalt | Wert im Mitschnitt |
|---|---|---|
| `351` | Ladespannung, Ladestrom, Entladestrom, Entladespannung | 51,8 V, 161,5 A, 289,7 A, 40,5 V |
| `355` | SoC, SoH, SoC fein | 26 %, 100 %, 25,60 % |
| `356` | Spannung, Strom, Temperatur | 48,60 V, −10,7 A, 15,9 °C |
| `35A` | Warnungen und Alarme | `A8A80200A8A80202` |
| `35E`, `370` | Name | `JK-BMS` |
| `372` | Module online | 2 |
| `373` | Zelle min, max, Temperatur min, max | 3233 mV, 3245 mV, 14 °C, 17 °C |
| `374`–`377` | Wo min und max liegen | `01::02`, `00::05`, `00::02`, `01::03` |
| `378` | Energie geladen, entladen | 34432, 31595, vermutlich je 100 Wh |
| `379` | Kapazität | 440 Ah |
| `380`, `381` | Seriennummer | `506036N49005514` |
| `305`, `307` | 1-mal pro Sekunde, Inhalt von `307` endet auf `VIC` | vermutlich von Venus gesendet |

- 15 Zellen in Reihe je Pack.
- Die wirksame Ladespannung ist heute 51,8 V vom BMS, unter der Grenze von 52,0 V aus Venus. Beim Ladestrom gilt die Grenze aus Venus: 120 A statt 161,5 A vom BMS.
- `305` und `307` sehen nach dem Lebenszeichen des Wechselrichters ans BMS aus. Fallen sie weg, wenn der Pi aus ist, muss sie der S3 senden. Dann kann er auf dem CAN-Bus nicht bei Listen-only bleiben.
- Einzelne Zellspannungen stehen nicht auf dem CAN-Bus, nur Minimum und Maximum.

## Geräte an VE.Direct

Die Firmware erkennt die Geräte an der Seriennummer (`firmware/src/devices.h`).

| Seriennummer | Gerät | Instanz | heute an |
|---|---|---|---|
| `HQ2440T6X3F` | SmartSolar 100/20 | 288 | `ttyUSB6` |
| `HQ24444FA7P` | SmartSolar 100/20 | 291 | `ttyUSB5` |
| `HQ2444X92KD` | BlueSolar 100/20 | 292 | `ttyUSB7` |
| `HQ250332J3K` | BlueSolar 100/20 | 293 | `ttyUSB1` |
| `HQ2503KHTNV` | BlueSolar 100/20 | 294 | `ttyUSB2` |
| `HQ2503PZF6F` | BlueSolar 100/20 | 296 | `ttyUSB4` |
| `HQ2512RP9C2` | SmartShunt 300 A | 291 | `ttyUSB8` |

## Offen

- Die DVCC- und ESS-Einstellungen, die der S3 übernehmen muss.
- Einzelzellen: DTUoB_WP (192.168.1.30, `OpenDTU-WP-0DB714`) liest per RS485 nur ein Pack, laut ioBroker am 14.11.2026 Adresse 1, 15 Zellen, 220 Ah. CAN meldet 440 Ah und 2 Module. Deshalb bekommt jedes Pack einen eigenen RS485 am SC16IS752. Laut esphome-jk-bms ist je Modbus-Bus nur ein Master erlaubt, und ein BMS mit Adresse 0 ist selbst Master.
- Baudrate am RS485 des JK-BMS.
- GND am CAN- und RS485-Port des JK-BMS gegen B− messen, dazu VE.Bus Pin 3 gegen DC− des MP2. Ist ein Port schon getrennt, entfällt dort die Trennung.
- Der CH348 nach einem Neustart des S3: weiter beobachten.

## Quellen

- [Venus OS Build-Repo](https://github.com/victronenergy/venus)
- [mkrasselt1/ve.bus-library](https://github.com/mkrasselt1/ve.bus-library)
- [pv-baxi/esp32ess, Doku](https://github.com/pv-baxi/esp32ess/blob/main/docs/README.md)
- [PepeTheFroggie/Victron-VE.Bus---esp32](https://github.com/PepeTheFroggie/Victron-VE.Bus---esp32)
- [Victron: ESS mode 2 and 3](https://www.victronenergy.com/live/ess:ess_mode_2_and_3?do=export_pdf)
- [Victron: VE.Direct-Protokoll](https://www.victronenergy.com/upload/documents/VE.Direct-Protocol-3.34.pdf)
- [Victron: VE.Direct FAQ](https://www.victronenergy.com/live/vedirect_protocol:faq?do=export_pdf)
- [MPPT HEX-Protokoll](https://www.mikrocontroller.net/attachment/522263/MPPT-HEX-Protocol.pdf)
- [CH348 Datenblatt](https://files.waveshare.com/wiki/USB-TO-8CH-TTL/CH348DS1.PDF)
- [CH348 Linux-Patch v8](https://ratatoskr.run/lkml/2025/02/2911285/t)
- [WCH: ch9344ser_linux](https://github.com/WCHSoftGroup/ch9344ser_linux)
- [ESPHome-PR #14586: CH9344 und CH348](https://github.com/esphome/esphome/pull/14586)
- [Waveshare: USB TO 8CH TTL](https://www.waveshare.com/wiki/USB_TO_8CH_TTL)
- [esphome-jk-bms #706: Master und Slave](https://github.com/syssi/esphome-jk-bms/issues/706)
- [Multiple JK BMS by Modbus RS485](https://github.com/phinix-org/Multiple-JK-BMS-by-Modbus-RS485)
- [SC16IS752 Datenblatt](https://www.nxp.com/docs/en/data-sheet/SC16IS752_SC16IS762.pdf)
- [nopnop2002/esp-idf-sc16is750](https://github.com/nopnop2002/esp-idf-sc16is750)
- [dbus-systemcalc-py](https://github.com/victronenergy/dbus-systemcalc-py)
- [dbus-flashmq](https://github.com/victronenergy/dbus-flashmq/blob/master/README.md)
- [mlesniew/PicoMQTT](https://github.com/mlesniew/PicoMQTT)
- [ESP-IDF: USB Host, Hinweise zum DWC_OTG](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/peripherals/usb_host/usb_host_notes_dwc_otg.html)
- [ESP-IDF: USB Host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)
- [ESPHome: USB Host](https://esphome.io/components/usb_host/)
- [tbnobody/OpenDTU](https://github.com/tbnobody/OpenDTU)
- [ESP32-S3 Datenblatt](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)
- [MAX13487E/MAX13488E Datenblatt](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf)
- [ADuM1200/ADuM1201 Datenblatt](https://www.analog.com/media/en/technical-documentation/data-sheets/adum1200_1201.pdf)
- [AO3402 Datenblatt (AOS)](https://www.aosmd.com/sites/default/files/res/datasheets/AO3402.pdf)

## Lizenz

GPL-2.0-or-later, siehe [LICENSE](LICENSE). Die fremden Bibliotheken in `firmware/lib/` stehen unter ihren eigenen Lizenzen, siehe [firmware/lib/README.md](firmware/lib/README.md).
