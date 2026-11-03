# ESP32-S3 statt Venus OS

Ich will die Funktionen von Venus OS, die ich nutze, auf einem ESP32-S3 nachbauen. Eine Portierung geht nicht: Venus OS ist eine Linux-Distribution, und ein kompletter Build braucht private Repos von Victron.

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
| `pvinverter.mqtt_pv_100` | 100 | MQTT PV | MQTT |

- DVCC ist aktiv.
- Die seriellen Anschlüsse laufen heute über 2 × FT4232 am Pi, alle Kanäle mit Isolatoren.
- Firmware der MPPT: `372` = `0x174`, also v1.74. MultiPlus: `1288` = `0x508`, also v508.
- `can1`: 500 kbit/s, MCP2515 am Pi. Das schafft der TWAI des S3 auch.
- Die MPPT laufen nachts aus der Batterie: Am 02.11. gegen 22:30 Uhr haben alle sechs geantwortet.

## Entscheidungen

- Ein ESP32-S3 für alles. Durch die Streuung rechne ich nicht mit mehr Verfügbarkeit, wenn ich aufteile.
- Die FT4232 ersetze ich gern durch einen CH348. Der läuft allerdings nicht an Venus.
- Von OpenDTU-OnBattery nutze ich nur Überwachung und Steuerung des Hoymiles HMS zur Einspeisung. Es ist deshalb keine Basis für dieses Projekt.
- VRM fällt weg. Der VRM-Prognosevergleich endet dann vermutlich mit dem Umschalten.

## Serielle Anschlüsse am S3

Der USB-Host des S3 kann nur Full Speed und hat 8 Kanäle. Jeder Endpunkt, auch EP0, belegt einen Kanal. Reicht das nicht, lässt sich das Gerät nicht öffnen.

- **2 × FT4232 geht nicht.** Schon einer braucht nach meiner Rechnung 9 Kanäle (4 Ports × 2 Endpunkte + EP0).
- **CH348 läuft am USB-Host des S3:** Das Datenblatt nennt nur 480 Mbit/s, am S3 meldet er sich aber mit Full Speed. Alle 8 Ports laufen über 4 Bulk-Endpunkte mit je 64 Bytes, mit EP0 also 5 von 8 Kanälen. Der Schleifentest über alle 8 Ports läuft fehlerfrei.
- Für den CH348 gibt es kein öffentliches Protokoll. Die Befehle in meinem Treiber stammen aus `ch9344ser_linux` von WCH (GPL-2.0), verglichen mit dem ESPHome-PR #14586.
- **Ausweichlösung 4 × SC16IS752:** SPI statt USB, je 2 UARTs, 64 Byte FIFO, SPI bis 4 Mbit/s, Eingänge 5 V tolerant, offenes Datenblatt von NXP. Mouser und LCSC führten ihn am 02.11.2026 laut Suchtreffern als lieferbar, live geprüft habe ich das nicht. Ein ESP-IDF-Treiber für den S3 existiert (`esp-idf-sc16is750`).
- Der CH9434 (SPI, 4 UARTs) wäre technisch passend, ist aber sehr schlecht verfügbar.

Pegel:
- Alle MPPT senden laut Victron mit 5 V. Die Isolatoren bleiben, die Seite zu den MPPT ändert sich also nicht. DVCC zeigt, dass Senden über sie heute funktioniert.
- Die Seite zum neuen Baustein muss mit 3,3 V laufen. Das muss ich beim Umbau prüfen.
- Falls doch 5 V ankommen: Beim CH348 vertragen nur RXD0, RXD2, RXD4 und RXD7 die 5 V, die GPIOs des S3 gar nicht.
- VE.Bus und CAN plane ich ebenfalls isoliert, passend zu den übrigen Kanälen.

## Belegung (Plan, ungetestet)

| Funktion | Signal | GPIO | Anschluss |
|---|---|---|---|
| VE.Bus, UART1, 256000 Baud | RX / TX / DE | 4 / 5 / 6 | RS485-Transceiver, isoliert |
| MT681 Netz, UART2, 9600 Baud | RX | 7 | Lesekopf, nur Empfang |
| MT681 Wärmepumpe, RMT, 9600 Baud | RX | 21 | Lesekopf, nur Empfang |
| JK-BMS, TWAI, 500 kbit/s | TX / RX | 15 / 16 | CAN-Transceiver für 3,3 V, isoliert |
| Funk, ein CMT2300A für beide HMS | CS, SDIO, CLK, FCS, GPIO2, GPIO3 | 10, 11, 12, 13, 9, 8 | Modul mit Antenne |
| Kellerlüftung, I²C | SDA / SCL | 17 / 18 | die beiden BME280 (0x76, 0x77) |
| Kellerlüftung | Lüfter | 14 | Relais |
| E-Paper, optional, SPI | SCK, MOSI, CS, DC, RST, BUSY | 39, 40, 41, 42, 2, 1 | Display |
| Konsole, Flashen, UART0 | TX / RX | 43 / 44 | COM-Buchse auf dem Board |
| CH348, USB-Host | D− / D+ | 19 / 20 | native USB-Buchse |
| Reserve | | 38, 47, 48 | Auf einem davon sitzt oft die RGB-LED des Boards |
| nicht beschalten | Strapping | 0, 3, 45, 46 | |
| belegt | Octal-PSRAM | 33–37 | |

| CH348 | Gerät |
|---|---|
| Port 0–5 | die 6 MPPT, über die vorhandenen Isolatoren |
| Port 6 | SmartShunt, über den vorhandenen Isolator |
| Port 7 | JK-BMS über RS485, nur für die Zellspannungen |

Zum JK-BMS laufen zwei Wege mit getrennten Aufgaben:
- **CAN steuert:** Ladegrenzen, Alarme, SoC, beide Packs zusammengefasst. Das nutzt Venus heute schon. Ohne CAN keine DVCC-Vorgaben.
- **RS485 zeigt an:** einzelne Zellspannungen für Webseite und ioBroker. Fällt es aus, fehlt nur die Anzeige. Die Regelung hängt nicht daran.

Der Netzzähler bleibt am eigenen UART, er liefert das Regelsignal fürs ESS. Der zweite MT681 läuft per RMT als Empfangs-UART, das ist noch ungetestet. Die 3 UARTs des S3 und die 8 Ports des CH348 sind damit belegt. Die beiden Zähler liegen nah beieinander, rund 2 m Leitung zum S3.

Welcher MPPT an welchem Port hängt, soll keine Rolle spielen: Die Firmware erkennt jedes Gerät an seiner Seriennummer aus VE.Direct und gibt ihm die Instanz von heute, damit die Themen und die History in ioBroker gleich bleiben.

Auf einem Board mit DevKitC-1-Stiftleiste liegen VE.Bus, MT681, CAN und Funk auf einer Seite, das E-Paper auf der anderen. VE.Bus (GPIO 4, 5) habe ich im Timing-Test benutzt, die übrigen Pins sind noch nicht verdrahtet.

Summe 24 Pins plus USB, mit E-Paper und Kellerlüftung. Frei nutzbar sind nach meiner Zählung 27: GPIO 0–18, 21, 38–48, ohne die Strapping-Pins 0, 3, 45, 46. Das gilt für ein Modul mit Octal-PSRAM. Mit 4 × SC16IS752 kämen 8–11 Pins dazu (SPI, 4 CS, 1–4 IRQ), USB bliebe dann frei.

Ins Netz geht der S3 über sein eigenes WLAN, einen W5500 brauche ich nicht. Ein Ausfall des WLAN darf die Regelung nicht berühren, er trifft nur die Verbindung zu ioBroker.

- VE.Bus braucht einen UART am S3: Alle 20 ms kommt ein Sync-Frame, und gesendet wird nur in einem engen Fenster danach.
- Der native USB-Port ist mit dem CH348 belegt. Flashen und Logs laufen dann über UART0, Updates über WLAN.
- Beide HMS laufen über ein CMT2300A. OpenDTU kann laut Quellen bis zu 10 Wechselrichter. Der Standort läuft seit Jahren, eine Reichweitenfrage gibt es nicht.

## DVCC

DVCC ist aktiv, der S3 muss es also ersetzen: die Ladegrenzen vom JK-BMS lesen und an alle sechs MPPT und den MultiPlus weitergeben.

Laut MPPT-HEX-Protokoll von Victron:
- **External Control (ESS, DVCC):** `0x200E` = `b0000101`, Vorgaben in `0x2001` und `0x2015`. Ohne Daten fällt der MPPT nach 1 Minute auf Stand-alone zurück.
- **BMS-Modus:** `0x200E` = `b0001001`. Der MPPT setzt dabei `BMS present` (`0xEDE8`) und speichert das im Flash. Danach läuft er nicht mehr alleine: Ohne Daten hört er auf zu laden und zeigt Fehler #67. Morgens lädt er 45 s in Float, damit das BMS starten kann.
- Ab Firmware 1.53 unterdrücken HEX-Anfragen den Text-Modus nicht mehr. Meine MPPT haben v1.74.
- Textblöcke kommen im Abstand von 1 s.

Bei mir steht auf allen sechs MPPT `NetworkMode` = 13 (`b0001101`: Networked, External Control, BMS) und `BmsPresent` = 1. Sie sind also im BMS-Modus. Das heißt für den Umbau:
- Ein umgesteckter MPPT hört tagsüber auf zu laden, bis der S3 ihm Vorgaben schickt. Nachts lädt er ohnehin nicht, läuft aber weiter und lässt sich lesen.
- Beim Umschalten gilt das für alle sechs.
- Bleiben die Vorgaben vom S3 aus, stoppen alle sechs mit Fehler #67.

## Software (Plan)

- Firmware in `firmware/`: PlatformIO mit Arduino 3.3.12 auf ESP-IDF 5.5.5 (`pioarduino`). Der USB-Treiber braucht ein aktuelles IDF, und beide Bibliotheken bauen auf Arduino auf.
- VE.Bus: `ve.bus-library` (MIT). Getestet auf LilyGo T-CAN485, der S3 ist nicht bestätigt.
- Hoymiles: `lib/Hoymiles` aus OpenDTU, laut Dateiköpfen GPL-2.0-or-later.
- Woher die fremden Bibliotheken stammen, steht in `firmware/lib/README.md`.
- Der CH348-Treiber liegt in `firmware/lib/ch348/`.
- Neu: VE.Direct, SML, JK-BMS über CAN, DVCC, ESS-Regelung, Stützung der Wärmepumpe, MQTT, Webseite.
- Aus `victron_meter.be` übernehmen: Der MT681 hat keinen Einspeisezähler (2.8.0), die Einspeisung wird integriert und alle 10 min gesichert. Der Zähler ist einphasig auf L1 abgebildet.

ioBroker soll nichts merken: gleiche Adresse 192.168.1.10:1883, Benutzer `remoteconsole`, Portal-ID `b827ebc35935`, gleiche Themen, Werte als `{"value": x}`. Dazu braucht der S3 einen eigenen MQTT-Broker.

- Lesen: 38 N-Themen, die Bereiche stehen in `~/projects/Kellerlüftung/mqtt1_abo.txt`.
- Schreiben: `AcPowerSetPoint`, `vebus.288.Mode`, `BatteryLife/State`.
- Dazu die Werte, die heute der Tasmota an ioBroker schickt (`MT681_Power_cur`, `MT681_Total_in`, `VIC`).
- `mqtt_pv_100` bekommt seine Werte heute per MQTT. Das muss weiter ankommen.

## VE.Bus laut Quellen (nicht von mir gemessen)

- 256000 Baud, 8N1. Sync-Frame alle 20 ms.
- `ve.bus-library` sendet nur 8,0–9,5 ms nach dem Sync und lässt sonst einen Sync aus.
- Der ESS-Sollwert muss spätestens alle 60 s kommen, sonst geht der MultiPlus in Passthrough.
- Vor OTA oder Neustart den Bus stumm schalten (`setTxEnabled(false)`).
- esp32ess läuft auf einem MultiPlus-II 48/5000 mit Firmware v502 (meiner: v508) und kann nur ein Gerät (einphasig). Es meldet 1–2 % fehlgeschlagene Befehle, die wiederholt werden.
- Der Bus muss hochohmig bleiben, über 100 kΩ, sonst schaltet sich das Victron-Gerät ab. Beim MAX485 also den 120-Ω-Widerstand und die Bias-Widerstände entfernen.
- USB des ESP32 und MultiPlus nie gleichzeitig anschließen (README von PepeTheFroggie).

## Plan

Der Gesamtplan steht in [PLAN.md](PLAN.md).

## Test CH348 (03.11.2026)

Aufbau:
- ESP32-S3 mit 16 MB Flash und 8 MB PSRAM, MAC `7C:4F:AD:1E:04:5C`.
- CH348 am nativen USB-Port, PC am COM-Port (UART0).
- 5 V für den CH348 über die geschlossene Brücke `USB-OTG`. Der native Port und COM dürfen dann nicht gleichzeitig am PC stecken.
- Mit dem ersten Kabel versorgte die COM-Buchse das Board nur in einer Steckrichtung. Ob das am Kabel oder an der Buchse lag, habe ich nicht geprüft.

Erkennung ([ganze Ausgabe](docs/test_ch348_2026-11-03.log)):

```
ERGEBNIS: 1a86:55d9 erkannt, Full Speed, 1 Interfaces belegt, 0 fehlgeschlagen, 5 von 8 Host-Kanaelen (Endpunkte + EP0)
```

Schleifentest mit dem Treiber aus `firmware/lib/ch348/`, an jedem Port TX auf RX ([Ausgabe](docs/test_ch348_schleife_2026-11-03.log)):

```
Version: 8a 88 80 00
Port 0: gesendet 75050, empfangen 75050, Fehler 0, fehlt 0
...
Port 7: gesendet 75050, empfangen 75050, Fehler 0, fehlt 0
ERGEBNIS: Minute 1, gesendet 600400, empfangen 600400, Fehler 0, ungueltige Bloecke 0, Status-Meldungen 12017
```

- Jeder Port sendet ein eigenes Muster mit 19200 Baud, 8 × 50 Bytes alle 40 ms. Das sind rund 65 % der Leitung. Jede Minute pausiert das Senden kurz, dann muss jeder Port genau so viel empfangen haben, wie er gesendet hat.
- Alle 8 Ports laufen also gleichzeitig über die 4 Endpunkte.
- Laut Versionsabfrage ist es ein CH348Q in Revision `0x8A`. Der Q hat RTS/CTS nur auf den ersten vier Ports. VE.Direct braucht beides nicht.
- Die Status-Meldungen kommen ungefähr so oft, wie ich Blöcke sende (12008 in der ersten Minute). Ausgewertet habe ich sie noch nicht.
- Endstand nach 92 Minuten: je Port 6.904.600 Bytes gesendet und empfangen, zusammen 55,2 MB, 0 Fehler, kein Neustart.

## Gesamtfirmware auf dem Board (03.11.2026)

`firmware/` läuft auf dem Testboard ([Ausgabe](docs/gesamt_2026-11-03.log)):

```
t=90s | CH348 an tx=894000 rx=893600 fehler=0 bloecke=0 | VE.Bus sync=nein | MT681 0 Bytes | CAN 0 | WLAN 192.168.1.173
  Heap intern frei 223532 (min 217096), PSRAM frei 8367272, Stack-Reserve loop=2968 net=6388 can=2140
```

- Belegt sind 14,9 % RAM statisch und 16,1 % der App-Partition. Im Betrieb bleiben über 217 KB interner Speicher und 8,3 MB PSRAM frei.
- Der Unterschied von 400 Bytes zwischen `tx` und `rx` ist ein Sendezyklus, der noch unterwegs ist.
- CMT2300A und Transceiver waren nicht angeschlossen. VE.Bus, CAN und MT681 hatten also nichts zu tun, auch der Funk lief nicht. Das sagt noch nichts über das Timing.

Zwei Dinge, die ich dabei gelernt habe:

- Arduino baut den USB-Host mit Enumerationsfilter. Ohne eigene Filterfunktion bricht ESP-IDF die Erkennung jedes Geräts ab, und die einzige Warnung darüber ist in Arduino abgeschaltet. `usb_host_lib_info()` zählt das Gerät trotzdem mit.
- Ein Treiber legt seinen Interrupt auf den Kern, der ihn startet, und `setup()` läuft auf Core 1. Außer VE.Bus startet deshalb alles aus einem Task auf Core 0.

## Timing-Test (03.11.2026)

Ein zweiter S3 spielt den MultiPlus (`firmware/sim/`): Er sendet alle 20 ms einen Sync-Frame im Format aus der esp32ess-Doku und misst von außen, wann die Antwort beginnt, bezogen auf den Beginn des Sync-Frames. Der Prüfling ist die Gesamtfirmware (Env `s3_timing`), in der VE.Bus sendet. Verbunden über Kreuz: GPIO 4 auf 5, 5 auf 4, GND. Ohne Transceiver.

Last auf Core 0 während der Messung: CH348 mit 8 Ports im Schleifentest, WLAN mit rund 95 UDP-Paketen pro Sekunde, MQTT mit 99 Nachrichten pro Sekunde an einen Abonnenten, alle 10 s ein Schreibzugriff auf den Flash. Das Funkmodul war nicht angeschlossen.

Ergebnis über 6 Blöcke zu je 30 s ([Simulation](docs/timing_simulation_2026-11-03.log), [Testboard](docs/timing_testboard_2026-11-03.log)):

- 9000 Sync-Frames, 9000 Antworten, keine Prüfsummenfehler, keine falsche Frame-Nummer.
- Beginn der Antwort 8,95 bis 10,53 ms nach dem Beginn des Sync-Frames, im Mittel 10,0 ms.

Rechenlast dabei, je Task in Prozent eines Kerns ([Ausgabe](docs/auslastung_2026-11-03.log)):

| Kern | Task | Last |
|---|---|---|
| Core 1 | VE.Bus | 5,0 |
| Core 1 | frei | 95,0 |
| Core 0 | `net` (MQTT und UDP der Testlast, Funk-Abfrage) | 27,1 |
| Core 0 | `wifi` | 13,9 |
| Core 0 | `tiT` (TCP/IP) | 3,5 |
| Core 0 | `usb_client` | 3,2 |
| Core 0 | CH348-Schleife | 0,9 |
| Core 0 | Sollwert | 0,8 |
| Core 0 | frei | 50,5 |

Der Netzverkehr im Test ist künstlich hoch: rund 195 Pakete pro Sekunde, im Betrieb sind es nach meinen Notizen etwa 13 MQTT-Nachrichten pro Sekunde. Auch der CH348 läuft mit 10 KB/s, 7 VE.Direct-Geräte liefern deutlich weniger.

Dieselbe Firmware ohne Testlast, aber mit CH348-Schleife: Core 1 ist zu 97,0 % frei (VE.Bus 3,0), Core 0 zu 91,3 % (`net` 4,9, `usb_client` 2,5, CH348-Schleife 0,9, `wifi` 0,4).

Schätzung für den Endausbau, nicht gemessen: Core 0 bei 15–20 %. 13 MQTT-Nachrichten pro Sekunde kosten anteilig etwa 3 Prozentpunkte. VE.Direct, SML, CAN, Funk, DVCC und ESS sind wenige Rechenschritte oder wenige Pakete pro Sekunde.

Was das nicht zeigt: ob ein echter MultiPlus diese Zeitpunkte annimmt. Die 8,0–9,5 ms der Bibliothek zählen ab dem Moment, in dem ihr Task den Sync liest. Von außen gemessen liegt die Antwort deshalb später. Das Band von gut 1,5 ms kommt vor allem daher, dass der Task zwischendurch einen Tick schläft. Den echten Zeitpunkt zeigt erst der Mitschnitt am Bus in Phase 2.

Gefunden dabei:

- Eine einzelne fehlende Antwort je 30 s kam von meinem Test: Die Statusausgabe hielt den Task auf, der die Sollwerte liefert. Seit die Sollwerte einen eigenen Task haben, ist sie weg.
- Das Testboard hatte unter dieser Last einen Brownout (`E BOD: Brownout detector was triggered`) und ist neu gestartet. Der nächste Brownout kam beim Start der normalen Firmware, also ohne Testlast. Versorgt wurde es dabei über die COM-Buchse, und daran hängt auch der CH348. Mit einem anderen Kabel lief es danach einen Start und 95 s ohne Brownout, allerdings ohne Testlast. Die endgültige Versorgung muss das sicher tragen.

## Kabelwege und Platz des S3

| Verbindung | Art | Länge |
|---|---|---|
| VE.Bus | RS485, RJ45-Kabel direkt vom S3 zum MultiPlus | unkritisch |
| JK-BMS | CAN, differentiell | unkritisch |
| CH348 | USB, 5 V und Daten | bis etwa 3 m, der CH348 kann woanders sitzen als der S3 |
| VE.Direct | 19200 Baud über die Isolatoren | wie heute am FT4232, wenn der CH348 dort sitzt |
| MT681 | 9600 Baud, 3,3 V | 2 m sind in Ordnung |
| Lüfter | Relais | unkritisch |
| BME280 | I²C | empfindlichste Leitung, nicht länger als heute am Tasmota |
| Funk | SPI, nur wenige Zentimeter | Modul sitzt direkt am S3, der Platz des S3 ist auch der Platz des Funks |

Die Längenangaben sind Erfahrungswerte, gemessen habe ich sie nicht. Der S3 gehört deshalb eher zu BME280, Zählern und dem heutigen Platz der DTUs als zum MultiPlus.

## JK-BMS auf CAN (Mitschnitt 03.11.2026)

Rohdaten: [docs/venus_can_seriennummern_2026-11-03.txt](docs/venus_can_seriennummern_2026-11-03.txt). 21 IDs, zusammen rund 27 Telegramme pro Sekunde, jede etwa 1,3-mal pro Sekunde. Die Bedeutung habe ich nach dem üblichen Victron-Format gelesen. Die Werte passen zueinander, an der Anzeige von Venus habe ich sie nicht geprüft.

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

- 48,60 V bei rund 3,24 V je Zelle ergibt 15 Zellen in Reihe.
- Die wirksame Ladespannung ist heute 51,8 V vom BMS, unter der Grenze von 52,0 V aus Venus. Beim Ladestrom gilt die Grenze aus Venus: 120 A statt 161,5 A vom BMS.
- `305` und `307` sehen nach dem Lebenszeichen des Wechselrichters ans BMS aus. Fallen sie weg, wenn der Pi aus ist, muss sie der S3 senden. Er kann auf dem CAN-Bus dann nicht bei Listen-only bleiben.
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
- Wer `mqtt_pv_100` speist. In `README.md` von pv-prognose heißt es, Venus erfasse Growatt und HMS als PV-Wechselrichter. Die Zuordnung habe ich nicht geprüft.
- Zellspannungen beider Packs über RS485: DTUoB_WP hängt heute per RS485 an einem JK-BMS, vermutlich dem zweiten. Laut esphome-jk-bms ist je Modbus-Bus nur ein Master erlaubt, und ein BMS mit Adresse 0 ist selbst Master. Ob ein Anschluss für beide Packs reicht, klärt sich, wenn der S3 in Phase 5 die Leitung von DTUoB_WP übernimmt. Braucht jedes Pack einen eigenen, gibt UART0 im Betrieb die Konsole ab und läuft auf GPIO 38, 47, 48. Flashen über COM geht dann weiter, die Logs laufen über WLAN.
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
