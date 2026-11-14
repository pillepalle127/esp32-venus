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
- Der CH348 läuft laut Datenblatt nur mit 3,3 V. Der CH348Q hat keinen eigenen I/O-Versorgungspin, alle UART-Pins arbeiten mit 3,3 V. TXD liefert high mindestens VCC − 0,4 V, RXD erkennt high ab 2,0 V und low bis 0,8 V. Die Isolatoren laufen auf der Seite zum CH348 deshalb mit 3,3 V.
- 5 V vertragen nur RXD0, RXD2, RXD4 und RXD7 (FT, bis 5,6 V). An RXD1, RXD3, RXD5 und RXD6 sind höchstens 4,0 V erlaubt. Die GPIOs des S3 vertragen gar keine 5 V.
- VE.Bus und CAN plane ich ebenfalls isoliert, passend zu den übrigen Kanälen.

## Belegung (Plan, ungetestet)

| Funktion | Signal | GPIO | Anschluss |
|---|---|---|---|
| VE.Bus, UART1, 256000 Baud | RX / TX | 5 / 4 | MAX13487, über ADuM1201 und B0505S isoliert |
| MT681 Haus, UART2, 9600 Baud | RX | 7 | Pfostenstecker MT681: GND, 3V3, Haus, WP |
| MT681 Wärmepumpe, RMT, 9600 Baud | RX | 6 | Pfostenstecker MT681 |
| Kellerlüftung, I²C | SDA / SCL | 16 / 15 | die beiden BME280 (0x76 außen, 0x77), je ein Pfostenstecker: GND, 3V3, SDA, SCL |
| Funk, ein CMT2300A für beide HMS | GPIO2, CS, SDIO, CLK, FCS, GPIO3 | 18, 8, 3, 46, 9, 10 | Modul oben am S3 angelötet, sechs Pins nebeneinander, dazu 3V3 und GND, kein Stecker |
| Kellerlüftung | Lüfter | 11 | Relais |
| Reserve | Relais 2 | 12 | Relais |
| JK-BMS, TWAI, 500 kbit/s | TX / RX | 14 / 13 | TJA1050 unten links, über ADuM1201 und B0505S isoliert |
| E-Paper, optional, SPI | SCK, MOSI, CS, DC, RST, BUSY | 39, 40, 41, 42, 2, 1 | Display |
| JK-BMS, UART0 im Betrieb, RS485 | RX / TX | 47 / 21 | MAX13487, über ADuM1201 und B0505S isoliert, nur für die Zellspannungen |
| RGB-LED des Boards | WS2812 | 48 | über die Lötbrücke „RGB“ auf dem Board, Farbverlauf durch das Spektrum, ein Umlauf in 15 s |
| Reserve | | 17, 38 | |
| Flashen, UART0 beim Start | TX / RX | 43 / 44 | COM-Buchse auf dem Board |
| CH348, USB-Host | D− / D+ | 19 / 20 | native USB-Buchse |
| nicht beschalten | Strapping | 0, 45 | |
| belegt | Octal-PSRAM | 33–37 | |

| CH348 | Gerät |
|---|---|
| Port 0–5 | die 6 MPPT, über die vorhandenen Isolatoren |
| Port 6 | SmartShunt, über den vorhandenen Isolator |
| Port 7 | frei, für einen weiteren MPPT |

Zum JK-BMS laufen zwei Wege mit getrennten Aufgaben:
- **CAN steuert:** Ladegrenzen, Alarme, SoC, beide Packs zusammengefasst. Das nutzt Venus heute schon. Ohne CAN keine DVCC-Vorgaben.
- **RS485 zeigt an:** einzelne Zellspannungen für Webseite und ioBroker. Fällt es aus, fehlt nur die Anzeige. Die Regelung hängt nicht daran.

Der Netzzähler bleibt am eigenen UART, er liefert das Regelsignal fürs ESS. Der zweite MT681 läuft per RMT als Empfangs-UART, am echten Zähler getestet (siehe Testfirmware). Die 3 UARTs des S3 sind damit belegt, ein Port des CH348 bleibt frei. UART0 gibt dafür im Betrieb die Konsole ab: Die Firmware legt ihn nach dem Start auf GPIO 47 und 21. Flashen über COM geht weiter, die Logs laufen über WLAN. Die beiden Zähler liegen nah beieinander, rund 2 m Leitung zum S3.

Welcher MPPT an welchem Port hängt, soll keine Rolle spielen: Die Firmware erkennt jedes Gerät an seiner Seriennummer aus VE.Direct und gibt ihm die Instanz von heute, damit die Themen und die History in ioBroker gleich bleiben.

Auf einem Board mit DevKitC-1-Stiftleiste liegen VE.Bus, MT681, CAN, I²C, Relais und Funk auf einer Seite, E-Paper und RS485 zum JK-BMS auf der anderen. Die drei Pfostenstecker sitzen übereinander neben ihren GPIOs. Die sechs GPIOs des Funkmoduls liegen nebeneinander, von GPIO 18 bis GPIO 10. Die beiden Relais liegen nebeneinander auf 11 und 12, CAN ganz unten links auf 13 und 14. VE.Bus (GPIO 4, 5) habe ich im Timing-Test benutzt, die übrigen Pins sind noch nicht verdrahtet.

Summe 24 Pins plus USB, mit E-Paper, Kellerlüftung und Relais 2. Frei nutzbar sind nach meiner Zählung 27: GPIO 0–18, 21, 38–48, ohne die Strapping-Pins 0, 3, 45, 46. Dazu kommen GPIO 3 und 46 für Signale des CMT2300A, die beim Start niemand treibt:
- GPIO 46 muss beim Reset low sein, sonst kommt der S3 nicht in den Download-Modus. Der interne Pull-down hält ihn low, und CLK ist am CMT2300A ein Eingang. Was den Pin beim Start hochzieht, etwa RX vom Transceiver, sperrt den Download-Modus. CAN-TX wäre beim Reset low, also dominant, und würde den Bus bei jedem Neustart stören.
- GPIO 3 trägt SDIO. Sein Strapping wirkt nach meiner Erinnerung nur, wenn die passende eFuse gesetzt ist, und das habe ich auf dem Board nicht geprüft. Es betrifft dann nur die Wahl der JTAG-Quelle.

Das gilt für ein Modul mit Octal-PSRAM. Mit 4 × SC16IS752 kämen 8–11 Pins dazu (SPI, 4 CS, 1–4 IRQ), USB bliebe dann frei.

Ins Netz geht der S3 über sein eigenes WLAN, einen W5500 brauche ich nicht. Ein Ausfall des WLAN darf die Regelung nicht berühren, er trifft nur die Verbindung zu ioBroker.

- VE.Bus braucht einen UART am S3: Alle 20 ms kommt ein Sync-Frame, und gesendet wird nur in einem engen Fenster danach.
- Der native USB-Port ist mit dem CH348 belegt. Flashen und Logs laufen dann über UART0, Updates über WLAN.
- Beide HMS laufen über ein CMT2300A. OpenDTU kann laut Quellen bis zu 10 Wechselrichter. Der Standort läuft seit Jahren, eine Reichweitenfrage gibt es nicht.

## Trennung, Transceiver (Plan, ungetestet)

Lochraster-Plan mit allen Löchern und Drähten: [docs/layout_lochraster.drawio](docs/layout_lochraster.drawio). Belegung der Stiftleisten: [docs/layout_lochraster.md](docs/layout_lochraster.md).

Trennung:
- Die BMS schalten im Minus-Zweig. Hängt der S3 ohne Trennung an zwei Stellen, überbrückt seine Masse einen offenen BMS-Schalter. Deshalb höchstens eine Verbindung ohne Trennung. Ich trenne alle drei: VE.Bus, CAN und RS485.
- Getrennt wird an TX und RX, nicht auf CANH/CANL oder A/B.
- GND bleibt am Bus. Ohne Masse sucht sich der Ausgleichsstrom den Weg über die Signalleitungen.
- Jede Busseite bekommt einen eigenen B0505S-1W. Ein gemeinsamer Wandler würde die Busmassen verbinden.
- Die Trennung ist eine Funktionstrennung gegen die Batteriespannung, rund 60 V, kein Schutz vor Netzspannung. Eine Prüfung mit 5 kV ergibt deshalb keinen Sinn und schadet: Der ADuM1201 ist für 2500 V rms über 1 Minute ausgelegt (Datenblatt, Tabelle 10). Was der B0505S aushält, steht in seinem Datenblatt, das habe ich nicht. Abstände auf der Lochrasterplatine sind nicht für Hochspannung ausgelegt. Es reicht der Durchgangstest, wer ein Isolationsmessgerät hat, misst mit 250 V DC (Testplan A2).
- Der ADuM1201 hängt auf der S3-Seite an 3,3 V, auf der Busseite an den 5 V des B0505S. Die 5 V der Transceiver erreichen die GPIOs dadurch nicht.
- Der B0505S ist ungeregelt. Der MAX13487 braucht laut Datenblatt 4,75–5,25 V, der TJA1050 nach meiner Erinnerung ebenso. Die Spannung bei wenig Last messe ich. Ist sie zu hoch, hilft ein Lastwiderstand: 250 Ω sind 10 % Last.
- Der ADuM1201AR reicht auch für CAN: rund 0,8 µs Laufzeit im schlechtesten Fall, der S3 tastet nach 1,6 µs ab. Das ist gerechnet, das BMS ist darin nicht enthalten.

Transceiver:
- Ich nehme fertige Module mit MAX13487, ADuM1201 und TJA1050.
- **MAX13487 (VE.Bus, RS485):** Er schaltet die Richtung selbst, DE entfällt. `ve.bus-library` ist laut Doku damit am MultiPlus getestet. Der Aufdruck muss `MAX13487EESA` sein, sonst ist es ein Nachbau.
- **ADuM1201 (CAN, RS485):** S3-Seite U1 an 3,3 V, A0 zum RX, B1 vom TX, G1 an GND. Busseite U2 an 5 V, A1 vom RXD, B0 zum TXD, G2 an die Busmasse.
- **ADuM1201 (VE.Bus):** Er sitzt oben neben den S3-Pins, die Seiten sind getauscht: U2 an 3,3 V, A1 von GPIO 4 (TX), B0 zu GPIO 5 (RX), G2 an GND. U1 an 5 V, A0 zum TXD, B1 vom RXD, G1 an die Busmasse. So kreuzen sich die vier Drähte zum MAX13487 nicht, sie laufen unter dem Modul. Das Datenblatt nennt 5 V/3 V und 3 V/5 V als Betriebsarten.
- **Richtung im ADuM1201:** Kanal A läuft von Seite 2 nach Seite 1, Kanal B umgekehrt (Datenblatt, Tabelle 16). Auf dem Modul heißt die Ziffer 1 Eingang und 0 Ausgang: A1 ist IC-Pin 7 (VIA), B1 Pin 3 (VIB), A0 Pin 2 (VOA), B0 Pin 6 (VOB). TX vom S3 geht deshalb immer auf A1 oder B1, RX kommt von A0 oder B0. Beide Einbaulagen funktionieren, solange alle vier Pins einer Seite auf derselben Seite der Trennung hängen. Die Netzliste aus dem Plan habe ich für alle drei Module so geprüft. Die Beschriftung habe ich nur am Foto des Händlers mit dem Datenblatt verglichen. Das Foto des Händlers zeigt nicht eindeutig, ob TXD zum IC-Pin DI geht, das messe ich am Modul. Details: [docs/layout_lochraster.md](docs/layout_lochraster.md).
- **TJA1050 (CAN):** ohne Quarz. Den braucht nur der MCP2515, und den brauche ich nicht, der S3 hat TWAI. Auf dem Bus sind nur zwei 120-Ω-Abschlüsse erlaubt, das Modul bringt einen mit.
- **Fertige isolierte Module** wie das XY-S485: nicht an VE.Bus. Am JK-BMS möglich, wenn GND der TTL-Seite gegen GND an den Klemmen offen ist und das Modul die Baudrate schafft.
- Vor dem Anschließen an VE.Bus messe ich A gegen B: kein 120-Ω-Abschluss.

Die Bestückung an den Relais-Pins (GPIO 11 und 12) mache ich anders, sie steht nicht im Lochraster-Plan.

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

Am 04.11.2026 gesehen ([Mitschnitt](docs/venus_mppt_0v_2026-11-04.txt)): Ohne Netzzähler und mit `RunWithoutGridMeter` = 0 meldet der MP2 `/Hub/ChargeVoltage` 0.0. DVCC gibt den MPPT dann 0 V, und sie laden nicht. `Mode` = 3 allein half nicht, `RunWithoutGridMeter` = 1 schon. Der S3 schickt den MPPT nie 0 V.

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

Ein zweiter S3 spielt den MultiPlus (`firmware/sim/`): Er sendet alle 20 ms einen Sync-Frame im Format aus der esp32ess-Doku und misst von außen, wann die Antwort beginnt, bezogen auf den Beginn des Sync-Frames. Der Prüfling ist die Gesamtfirmware (Env `s3_timing`), in der VE.Bus sendet. Verbunden über Kreuz: GPIO 4 auf 5, 5 auf 4, GND, damals mit RX 4 und TX 5 am Prüfling. Ohne Transceiver.

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

## Testfirmware mit Webseite (14.11.2026)

Damit ich die Anschlüsse vorab prüfen kann, zeigt die Firmware (Env `s3`) auf einer Webseite, was ankommt: `http://<IP>/` oder `http://esp32-venus.local/`. Ablauf der Prüfungen: [docs/testplan.md](docs/testplan.md). Die alte Gesamtfirmware mit CH348-Schleifentest liegt jetzt in `firmware/timing/` und baut weiter als Env `s3_timing`.

| Reiter | Inhalt |
|---|---|
| Übersicht | alle Werte in einer Tabelle, Relais-Pins schalten |
| I²C | Scan, beide BME280 mit Temperatur, Feuchte, Druck, Taupunkt, absoluter Feuchte |
| Zähler | MT681 Haus (UART2) und WP (RMT): Leistung, 1.8.0, 2.8.0, Telegramme, CRC- und Rahmenfehler |
| CAN | JK-BMS nach Victron-Format gelesen, alle IDs mit Rohdaten, Fehlerzähler |
| VE.Bus / RS485 | linker Kanal: VE.Bus nur hören oder RS485-Test |
| VE.Direct | 8 Ports am CH348: Gerät, Spannung, Strom, PV, Zustand, Prüfsummen |
| Konsole | Protokoll der Firmware, auch Meldungen der IDF-Treiber |
| System | Laufzeit, Neustartgrund, WLAN, Speicher, Firmware-Update über WLAN |

Gesendet wird nur auf Knopfdruck im RS485-Test:
- VE.Bus hört nur zu, das Senden ist ausgeschaltet.
- CAN läuft listen-only.
- Der CH348 empfängt nur. Der alte Schleifentest hat auf allen 8 Ports gesendet, das darf an den MPPT nicht passieren.
- Die Relais-Pins sind beim Start nicht getrieben, erst ein Knopf macht sie zum Ausgang.

Der linke Kanal (GPIO 4/5) hat dieselbe Hardware wie der rechte: MAX13487, ADuM1201, B0505S. Im RS485-Test laufen Baudrate, Empfang als Hex und Text und Senden über UART1, VE.Bus bleibt dann aus. Die Wahl steht im NVS, nach dem Umschalten startet der S3 neu. Senden nur, wenn kein MultiPlus am Kanal hängt. Damit kann ich die Kette mit einem USB-RS485-Adapter prüfen oder am JK-BMS die Baudrate herausfinden.

Die Seite hat keine Anmeldung. Wer im WLAN ist, kann Relais schalten, im RS485-Test senden und Firmware laden.

Das WLAN steht in `firmware/src/wifi_secrets.h` (`WIFI_SSID`, `WIFI_PASS`, Vorlage `wifi_secrets.h.example`). Die Datei ist nicht im Repo. Nach einer Änderung bauen und flashen.

Erster Start auf dem Board der Lochrasterplatine (14.11.2026, MAC `7C:E8:B1:A4:AF:E8`), noch ohne angeschlossene Geräte:
- Der USB-Host fand keinen freien Level-1-Interrupt mehr, weil RMT, I²C, CAN und UART ihre schon belegt hatten. Der S3 startete deshalb in einer Schleife neu. USB startet jetzt zuerst und darf Level 1–3 nehmen.
- RMT mit DMA: `mem_block_symbols` legt den DMA-Bereich fest und muss mindestens so groß sein wie der Empfangspuffer.
- Danach startet alles ohne Fehler. BME280, CMT2300A und CH348 fehlen, weil nichts angeschlossen ist.
- Beide MT681 am echten Zähler (14.11.2026), 124 s nach dem Start: Haus an UART2 (GPIO 7) 124 Telegramme zu 216 Bytes, also eins pro Sekunde. WP per RMT (GPIO 6) 34 Telegramme zu 324 Bytes, etwa alle 3,6 s. Bei beiden 0 CRC-Fehler, beim RMT-Empfang 0 Rahmenfehler. Werte: Haus 720–1025 W, 1.8.0 bei 27505,0 kWh. WP 46–50 W, 1.8.0 bei 27195,6 kWh. 2.8.0 senden beide nicht.
- CH348 an der nativen Buchse (14.11.): erkannt, Version `8A 88 80 00`, alle 8 Ports mit 19200 Baud offen, keine ungültigen Blöcke. An den Ports hing noch nichts.
- Erster MPPT am CH348 (14.11.2026, abends): Port 0, `HQ2503KHTNV`, von der Firmware als `solarcharger 294` erkannt. 27 Blöcke ohne Prüfsummenfehler, alle Felder lesbar (V 49,96 V, VPV 3,15 V, CS 0). Zuerst kam kein Byte: RX und TX waren zwischen Isolator und CH348-Modul gekreuzt.
- Nach einem Neustart des S3 meldete sich der CH348 nicht wieder, wenn er selbst Strom behielt: nach Software-Neustart, nach Reset über EN und auch, wenn die Firmware den USB-Port ab- und wieder einschaltet (16 Versuche). Nach einmal Aus und Ein am CH348 war er sofort da. Vorher leuchtete am Modul nur die rote LED, danach rot und grün. Der CH348 hat einen Ausgang ACT, der nach dem Einrichten durch den USB-Host auf low geht (Datenblatt). Hängt die grüne LED daran, wurde der CH348 nach dem Neustart des S3 nie eingerichtet. Den Schaltplan des Moduls habe ich nicht. Die Firmware kann seine 5 V nicht schalten, sie hängen über die USB-OTG-Brücke fest an der 5-V-Schiene.
- Versorgt über die COM-Buchse am Laptop: Nach jedem Einschalten oder Reset über EN löst beim ersten WLAN-Start die Unterspannungserkennung aus (`E BOD: Brownout detector was triggered`, Neustartgrund „Unterspannung“). Nach dem Neustart verbindet sich der S3 und läuft stabil, in drei Versuchen jedes Mal so. Vorher schlug zweimal der Interrupt-Watchdog zu, ebenfalls beim WLAN-Start und ohne Panik-Ausgabe. Dass auch das an der Versorgung lag, ist naheliegend, aber nicht belegt. Danach einmal an die native Buchse und zurück an COM umgesteckt. Seitdem in 5 Starts kein Brownout mehr: Einstecken an beiden Buchsen, Software-Neustart, EN-Reset mit offenem und mit sofort geschlossenem Port. Die Buchse ist damit als Ursache unwahrscheinlich. Was das Umstecken geändert hat, ist offen.
- Status, Konsole und I²C-Scan habe ich vom Container aus geprüft: gültiges JSON, Werte wie erwartet.
- Update über WLAN (OTA) brach fast immer ab, und große Pakete kamen beim S3 nicht an (Ping mit 1472 Bytes bis 100 % Verlust). Ursache: Das Fritz-Mesh hat die SSID auf drei Zugangspunkten. Arduino sucht schnell und nimmt den ersten Treffer, das war Kanal 1 mit −75 dBm. Der nächste Zugangspunkt hatte −48 dBm auf Kanal 11. Jetzt sucht der S3 auf allen Kanälen und nimmt den stärksten (`WIFI_ALL_CHANNEL_SCAN`, `WIFI_CONNECT_AP_BY_SIGNAL`): −44 dBm, Ping mit 1472 Bytes ohne Verlust, OTA dreimal hintereinander in 12–14 s (rund 95 KB/s).
- Vorher falsch vermutet: 40 MHz Kanalbreite, das Löschen von 64-KB-Blöcken beim Update, Antenne oder Board. Ein WLAN-Scan auf der Seite (`/api/scan`) zeigt jetzt alle Zugangspunkte mit Kanal und Pegel.
- Ohne Stromsparmodus antwortet der S3 auf Ping im Mittel nach 14 ms, mit Stromsparmodus nach 100 ms, in der Spitze 500 ms. Er bleibt aus.
- Nach einem Fehlschlag versucht Arduino nur einmal neu und nach `AUTH_FAIL` gar nicht mehr. Die Firmware startet nach 20 s ohne Verbindung selbst einen neuen Versuch.
- Das Update nutzt die IDF-Funktionen mit sequentiellem Schreiben und 15 s Zeitgrenze für den Upload.
- Mit dem CH348 an der nativen Buchse, alles über die COM-Buchse des Laptops versorgt: Nach zwei von drei Updates meldete der S3 „Unterspannung“, einmal brach ein Update mittendrin mit Neustart wegen Unterspannung ab. Mit einem anderen USB-Kabel danach drei Updates hintereinander, jedes Mal Neustartgrund „Software“, keine Unterspannung. Es lag also am Kabel. Im Betrieb wird die Versorgung ohnehin anders gelöst.

Am PC getestet, bevor die Firmware aufs Board ging:
- SML: ein Telegramm mit 1.8.0, 2.8.0 und 16.7.0, Mehrbyte-Längenfeld, Müll davor. Werte richtig, verfälschte CRC wird verworfen.
- RMT-Empfang: Der Decoder tastet wie ein UART in Bitmitte ab, synchronisiert auf jede Startflanke. Fehlerfrei, solange Baudabweichung über ein Zeichen, Flankenverzögerung und Zittern zusammen unter einer halben Bitzeit bleiben (52 µs). Bei exakter Baudrate sind das zum Beispiel ±10 µs Zittern plus 20 µs Verzögerung der steigenden Flanke. Wie gut der Lesekopf das einhält, zeigen die Rahmenfehler am echten Zähler.
- BME280: Temperatur und Druck mit dem Beispiel aus dem BMP280-Datenblatt (25,08 °C), Feuchte gegen die Gleitkomma-Formel von Bosch, Abweichung unter 0,01 %.
- VE.Direct: Blöcke mit Prüfsumme, eingeschobene Hex-Meldung, verfälschter Block wird verworfen.

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
- Zellspannungen beider Packs über RS485: DTUoB_WP hängt heute per RS485 an einem JK-BMS, vermutlich dem zweiten. Laut esphome-jk-bms ist je Modbus-Bus nur ein Master erlaubt, und ein BMS mit Adresse 0 ist selbst Master. Ob ein Anschluss für beide Packs reicht, klärt sich, wenn der S3 in Phase 5 die Leitung von DTUoB_WP übernimmt. Braucht jedes Pack einen eigenen Anschluss, fehlt ein UART. Dann kommt ein SC16IS752 auf die Pins des E-Paper, das Display entfällt dafür.
- Baudrate am RS485 des JK-BMS.
- GND am CAN- und RS485-Port des JK-BMS gegen B− messen, dazu VE.Bus Pin 3 gegen DC− des MP2. Ist ein Port schon getrennt, entfällt dort die Trennung.

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
- [MAX13487E Datenblatt](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf)
- [ADuM1200/ADuM1201 Datenblatt](https://www.mouser.com/datasheet/2/609/ADuM1200_1201-1503479.pdf)
- [ADuM1200/ADuM1201 Datenblatt](https://www.analog.com/media/en/technical-documentation/data-sheets/adum1200_1201.pdf)
- [MAX13487E/MAX13488E Datenblatt](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf)
- [2N7000 Datenblatt](https://www.onsemi.com/pdf/datasheet/nds7002a-d.pdf)

## Lizenz

GPL-2.0-or-later, siehe [LICENSE](LICENSE). Die fremden Bibliotheken in `firmware/lib/` stehen unter ihren eigenen Lizenzen, siehe [firmware/lib/README.md](firmware/lib/README.md).
