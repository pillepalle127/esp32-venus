# Testplan

Ich prüfe jede Strecke einzeln, bevor der S3 an die Anlage kommt. Werkzeug ist die Testfirmware mit Webseite ([Tests und Messungen](tests.md#testfirmware-mit-webseite-14112026)): `http://<IP>/` oder `http://esp32-venus.local/`.

- **Teil A** läuft am Tisch, ohne Anlage. Nichts davon berührt VE.Bus, BMS oder Regelung.
- **Teil B** ist Phase 2 aus [PLAN.md](../PLAN.md): Der S3 hört an der Anlage mit und sendet nichts. Jeden Schritt in Teil B spreche ich vorher einzeln ab.
- Die Ergebnisse trage ich unten in die Tabelle ein.

## Teil A: am Tisch

### A1 Versorgung

- S3 so versorgen wie später, über die 5-V-Einspeisung.
- Mehrmals aus- und einschalten. Auf der Seite unter System den Neustartgrund ablesen.
- **In Ordnung:** immer „Einschalten“, nie „Unterspannung“.

### A2 Isolierte Busseiten

Für jeden Kanal mit B0505S (VE.Bus, CAN, später RS485 rechts):
- Durchgang zwischen GND des S3 und der Busmasse (G auf der Busseite des ADuM): **offen**.
- Isolationsmessung, nur wenn ein Messgerät dafür da ist: alle Pins einer Seite verbunden gegen alle Pins der anderen, **250 V DC**. **In Ordnung:** mehrere MΩ oder mehr, ein Wert im kΩ-Bereich heißt, irgendwo besteht eine Verbindung. Keine Prüfung mit kV, siehe README.
- +Vo gegen 0V am B0505S bei wenig Last: **4,75–5,25 V**. Den Bereich nennt das Datenblatt des MAX13487, beim TJA1050 gehe ich vom selben aus. Liegt die Spannung darüber: Lastwiderstand, siehe README.

### A3 TX-Pegel bei Reset und Flashen

Der ADuM1201 hat am Eingang keinen Pull-up oder Pull-down (Datenblatt: ±10 µA, typisch 0,01 µA). Ist der TX-Pin des S3 offen, ist auch der Ausgang des ADuM unbestimmt. Dann sendet der MAX13487 auf VE.Bus, oder der TJA1050 zieht den CAN-Bus dominant.

Messen gegen GND des S3:
- VE.Bus: ADuM-Pin **A1** (GPIO 4)
- CAN: ADuM-Pin **B1** (GPIO 14)

Jeweils in drei Zuständen (RST zieht EN des S3 auf low):
1. **RST gedrückt halten** und dabei messen.
2. **Download-Modus:** BOOT halten, RST kurz drücken, dann BOOT loslassen. Der S3 bleibt im Download-Modus, ohne WLAN und Seite, bis zum nächsten RST.
3. **Normaler Betrieb:** RST kurz drücken, nach dem Start messen.

**In Ordnung:** überall high, mindestens 2,3 V (0,7 × 3,3 V laut Datenblatt). Ist ein Pin low oder liegt er dazwischen, braucht dieser TX einen festen High-Pegel. B3 und alle späteren Phasen mit Senden warten so lange.

### A4 I²C und BME280

- Ersten Sensor an „BME280 1“ stecken, Seite I²C → **I²C scannen**: 0x76 oder 0x77.
- Zweiten Sensor dazu: zwei verschiedene Adressen, 0x76 und 0x77. Haben beide dieselbe, sehe ich nur einen.
- **In Ordnung:** Chip „BME280“, Werte passen zum Raum, Lesefehler bleiben 0.
- Kommen die Sensoren vom Tasmota, steht dort so lange die Lüftungsregelung.

### A5 MT681-Eingänge ohne Zähler

Der S3 schickt sich selbst ein SML-Telegramm, über UART1 an beide Zähler-Eingänge.

1. Drahtbrücke von GPIO 4 auf GPIO 6 und auf GPIO 7. Auf dem linken RS485 hängt dabei nichts.
2. Reiter VE.Bus / RS485: **RS485-Test**, **9600** Baud, übernehmen. Der S3 startet neu.
3. Dieses Telegramm als Hex senden (86 Bytes, CRC stimmt):

```
1B 1B 1B 1B 01 01 01 01 77 07 01 00 01 08 00 FF 65 00 1C 01 04 01 62 1E 52 FF 59 00 00 00 00 00 BC 61 4E 01 77 07 01 00 02 08 00 FF 01 01 62 1E 01 65 00 00 03 E8 01 77 07 01 00 10 07 00 FF 01 01 62 1B 52 00 55 FF FF FE 0C 01 00 00 00 1B 1B 1B 1B 1A 03 C2 11
```

4. **In Ordnung:** im Reiter Zähler bei Haus und WP je 1 Telegramm, CRC-Fehler 0, Bezug 1234,5678 kWh, Einspeisung 1,0000 kWh, Leistung −500 W. Bei WP zusätzlich Rahmenfehler 0.
5. Brücke abziehen, zurück auf **VE.Bus, nur hören**.

### A6 Linke RS485-Kette (GPIO 4/5)

Am Bus hängt nichts außer dem Prüfgerät, kein MultiPlus.
- **Echo:** RS485-Test, 115200 Baud, `55 AA 01 02` senden. Kommt dieselbe Folge als RX zurück, ist die Kette in beide Richtungen in Ordnung. Ob das Modul sein eigenes Senden zurückgibt, weiß ich nicht. Kommt nichts, ist das noch kein Fehler.
- **Mit USB-RS485-Adapter** an A, B und Busmasse, Terminal mit 115200 Baud: Was die Seite sendet, kommt am PC an. Was der PC sendet, zeigt die Seite als RX.
- **Ohne zweites Gerät:** Knopf **10 s Dauersenden 0x00**, dabei zwischen A und B messen. **In Ordnung:** in Ruhe etwa 0 V oder leicht positiv, beim Senden deutlich negativ.
- Danach zurück auf **VE.Bus, nur hören**.

### A7 CAN-Kette (TX GPIO 14, RX GPIO 13)

- Ohne Bus: Treiber „laeuft“, Fehlerzähler 0, keine Telegramme.
- Reiter CAN → **Selbsttest starten**. Der S3 schaltet kurz auf „ohne Quittung“, sendet 10 Telegramme und liest jedes über TJA1050 und ADuM zurück, danach hört er wieder nur zu. Das prüft die Kette in beide Richtungen, auch die Laufzeit durch den ADuM. Er sendet dabei auf CAN, also nur ohne BMS am Bus, mit dem 120-Ω-Widerstand des Moduls.
- **In Ordnung:** 10 gesendet, 10 richtig zurück, TX-Fehlerzähler 0.

### A6b Rechte RS485-Ketten (SC16IS752, Pack 1 und 2)

**Die Firmware dafür fehlt noch:** Treiber für den SC16IS752 und ein RS485-Test je Kanal auf der Seite.
- SPI: Die Firmware schreibt und liest das Scratchpad-Register des SC16IS752. In Ordnung, wenn der geschriebene Wert zurückkommt.
- Pack 1 hängt an UART B, Pack 2 an UART A.
- Je Kanal wie A6: Echo, Dauersenden mit Messung zwischen A und B oder ein USB-RS485-Adapter. Am Bus hängt dabei nichts.
- Dafür brauche ich die Frequenz des Quarzes auf dem CJMCU-752.

### A8 CH348

- CH348 an die native Buchse, 5 V über die USB-OTG-Brücke. COM und native Buchse nie gleichzeitig am PC.
- **In Ordnung:** Reiter VE.Direct zeigt „verbunden“, Version mit `8A`.

### A9 CMT2300A

- Nach dem Anlöten: Übersicht zeigt „gefunden“.
- Danach den Download-Modus prüfen, wegen CLK auf GPIO 46: BOOT halten, RST kurz drücken, flashen über COM. **In Ordnung:** esptool meldet den Chip und schreibt.

### A10 Relais-Pins

- Nach dem Start ist der Pin nicht getrieben. Die eigene Ansteuerung darf dann nicht schalten.
- Auf der Seite low und high schalten. **In Ordnung:** Pegel an GPIO 11 und 12 folgt, die Ansteuerung schaltet.

## Teil B: an der Anlage, nur hören

Voraussetzung: A1 bis A3 erledigt. Ablauf und Anschlussart stehen in [PLAN.md, Phase 2](../PLAN.md#phase-2-mithören). Hier stehen nur die Prüfpunkte.

| Schritt | Vorher | In Ordnung | Abbrechen, wenn |
|---|---|---|---|
| B1 MT681 Haus | parallel zum Tasmota | regelmäßig Telegramme, CRC-Fehler 0, Bezug wie beim Tasmota | Tasmota verliert Werte |
| B2 MT681 WP | parallel zu DTUoB_WP, berührt dessen Regelung | wie B1, dazu Rahmenfehler 0 | DTUoB_WP verliert Werte |
| B3 CAN am JK-BMS | A3 für GPIO 14 high, 120 Ω am Modul entfernt (der Bus hat schon zwei) | rund 27 Telegramme pro Sekunde, 21 IDs, Werte wie in Venus, Busfehler 0 | Fehlerzähler steigen, Venus meldet das BMS als weg |
| B4 VE.Bus am MultiPlus | TX-Leitung nicht angeschlossen, Bus über 100 kΩ gemessen, Lötbrücke 120_S offen, Aufdruck `MAX13487EESA` | Sync „empfangen“, Prüfsummenfehler bleiben bei 0, DC-Strom und Temperatur plausibel | Venus oder MultiPlus melden VE.Bus-Fehler |
| B5 VE.Direct | parallel an den Isolator-Ausgängen, FT4232 bleibt dran | Seriennummer erkannt, Instanz aus `devices.h`, Prüfsummen ohne Fehler, Werte wie in Venus | ein MPPT meldet Fehler #67 |

## Ergebnisse

| Schritt | Datum | Ergebnis | Bemerkung |
|---|---|---|---|
| A1 Versorgung | 14.11.2026 | offen | Über USB am Laptop: mit dem alten Kabel Unterspannung, mit einem anderen nicht. Im Betrieb wird die Versorgung anders gelöst. |
| A2 Isolierte Busseiten | 14.11.2026 | in Ordnung | Durchgang S3-Masse zu Busmasse offen. |
| A3 TX-Pegel | 14.11.2026 | in Ordnung | ADuM A1 (GPIO 4) und B1 (GPIO 14) bei gedrücktem RST, im Download-Modus und im Betrieb immer über 3 V. |
| A4 I²C und BME280 | 14.11.2026 | in Ordnung | SDA und SCL waren anders verdrahtet als geplant, übernommen: SDA 16, SCL 15. 0x76 außen, 0x77 der zweite Sensor, Lesefehler 0. |
| A5 MT681-Eingänge | 14.11.2026 | entfällt | Gleich am echten Zähler geprüft, siehe B1 und B2. |
| A6 Linke RS485-Kette | 14.11.2026 | teilweise | Pinprüfung in Ordnung: TX (GPIO 4) offen, RX (GPIO 5) vom ADuM high getrieben. Gesendet 15 Bytes, kein Echo. Ob das Modul sein Senden zurückgibt, ist offen. Sendeweg: A gegen B beim 10-s-Dauersenden messen. |
| A6b Rechte RS485-Ketten | | | |
| A7 CAN-Kette | 14.11.2026 | in Ordnung | 10 gesendet, 10 richtig zurück, TX-Fehlerzähler 0, Busfehler 0. Zuerst fehlgeschlagen: Die Pins waren noch nicht verdrahtet, danach TX und RX vertauscht. Übernommen wie verdrahtet: TX 14, RX 13. Der Selbsttest liest jetzt vorher beide Pins und sendet nur, wenn RX getrieben und TX offen ist. |
| A8 CH348 | 14.11.2026 | mit Einschränkung | Version `8A 88 80 00`, 8 Ports mit 19200 Baud. Nach einem Neustart des S3 meldet er sich nur, wenn er selbst kurz ohne Strom war. |
| A9 CMT2300A | | | |
| A10 Relais-Pins | | | |
| B1 MT681 Haus | 14.11.2026 | in Ordnung | GPIO 7 (getauscht wie verdrahtet), 124 Telegramme in 124 s, 0 CRC-Fehler. Ob parallel zum Tasmota, habe ich nicht notiert. |
| B2 MT681 WP | 14.11.2026 | in Ordnung | GPIO 6 per RMT, 34 Telegramme, 0 CRC-Fehler, 0 Rahmenfehler. |
| B3 CAN am JK-BMS | | | |
| B4 VE.Bus am MultiPlus | | | |
| B5 VE.Direct | 14.11.2026 | ein MPPT in Ordnung | Port 0, `HQ2503KHTNV` = solarcharger 294, 27 Blöcke ohne Prüfsummenfehler. RX und TX waren zuerst gekreuzt. |
