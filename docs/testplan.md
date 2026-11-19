# Testplan

Ich prüfe jede Strecke einzeln, bevor der S3 an die Anlage kommt. Werkzeug ist die Testfirmware mit Webseite ([Tests und Messungen](tests.md#testfirmware-mit-webseite-14112026)): `http://<IP>/` oder `http://esp32-venus.local/`.

- **Teil A** läuft am Tisch, ohne Anlage. Nichts davon berührt VE.Bus, BMS oder Regelung.
- **Teil B** ist Phase 2 aus [PLAN.md](../PLAN.md): Der S3 hört an der Anlage mit und sendet nichts. Jeden Schritt in Teil B spreche ich vorher einzeln ab.
- Die Ergebnisse trage ich unten in die Tabelle ein.

## Anstehend (Stand 18.11.2026)

Erledigt seit 15.11.: A6c, B4, B3 und der erste Test mit Senden am MultiPlus (C1, siehe Ergebnisse). Nach Wichtigkeit für das Ziel, Venus zu ersetzen:

| Nr. | Test | Wo | Braucht vorher |
|---|---|---|---|
| 1 | C1 Vorzeichen AC-out (RAM 16): 10 s fest 0 W, während die Kühltruhe läuft | Anlage | Absprache, MK3 ab, ESP_venus an VE.Bus und CAN |
| 2 | C1 Schalter MultiPlus: nur Laden, nur Wechselrichter, ein (aus nur, wenn AC-out kurz stromlos sein darf) | Anlage | wie 1 |
| 3 | C1 Regelung bei Lastsprüngen mit der Begrenzung ±500 W um die gemessene Einspeisung: Einschwingzeit, Überschwinger. Danach Verzögerung des MP2 ausmessen, Verstärkung schrittweise erhöhen | Anlage | wie 1, Wasserkocher oder Herd |
| 4 | C1 Entladeende: bis Mindest-SoC (BMS) entladen, danach Batteriestrom ~0 A, Kühltruhe aus dem Netz | Anlage | 1 erledigt |
| 5 | C1 Ausfälle: MT681 ab (Batterie ruht), CAN ab (Grenzen 0), Neustart ESP_venus (Zeit bis Passthrough messen) | Anlage | wie 1 |
| 6 | C1 Dauerlauf: gesendet/quittiert und Prüfsummenfehler über Stunden, einmal A–B beim Senden mit dem Oszilloskop | Anlage | wie 1 |
| 7 | CAN 0x305/0x307 ans BMS senden, wie bisher Venus. Vorher prüfen, ob das BMS ohne sie auf Dauer etwas meldet | Anlage | Firmware |
| 8 | A1 Versorgung: Diode am COM ersetzen oder andere Lösung | Tisch | – |
| 9 | B5 VE.Direct, die übrigen MPPT und der SmartShunt, danach DVCC | Anlage | 8 |
| 10 | Entscheidung SoC-Quelle für Regelung und Mindest-SoC: SmartShunt (wie heute Venus) oder BMS | – | 9 |
| 11 | Werte an ioBroker über MQTT, wie heute von Tasmota und Venus. Bisher veröffentlicht ESP_venus nichts | – | Firmware |
| 12 | V1 VRM-Verbindung: TEST_POST an `log.php`, Antwort `vrm: OK`. Anmeldung bei `mqtt54` mit Portal-ID und Passwort des Pi, `R/<ID>/#` abonnieren, nichts senden. Nur kurz, dabei auf dem Pi `/Mqtt/Bridges/GXdbus/Connected` prüfen. Ablauf in [vrm.md](vrm.md) | Tisch | Firmware |
| 13 | V2 VRM-Zeilen: Codes aus `datalist.py` den Werten von ESP_venus zuordnen, Zeilen nur ins Log schreiben und mit dem vergleichen, was der Pi sendet | – | 12 |
| 14 | V3 VRM umschalten: vrmlogger und Brücke auf dem Pi aus, ESP_venus sendet ANNOUNCE, Werte, kWh und Live-Werte | Anlage | 11, 13, Absprache |
| 15 | B6 JK-BMS über RS485, Zellspannungen beider Packs | Anlage | Firmware: JK-Protokoll |
| 16 | B7 Regelung WP-HMS, A9 Rest | Anlage | CMT2300A wird seit 16.11. nicht gefunden |
| 17 | A10 Relais-Pins | Tisch | Relaistreiber aufgebaut |

Zu lösen, bevor ESP_venus die MPPT übernimmt: Der CH348 meldet sich nach einem Neustart von ESP_venus erst nach Aus/Ein seiner eigenen 5 V wieder (A8).

## Teil A: am Tisch

### A1 Versorgung

- S3 so versorgen wie später, über die 5-V-Einspeisung.
- Mehrmals aus- und einschalten. Auf der Seite unter System den Neustartgrund ablesen.
- **In Ordnung:** immer „Einschalten“, nie „Unterspannung“.

### A2 Isolierte Busseiten

Für jeden Kanal mit B0505S (VE.Bus, CAN, später RS485 rechts):
- Durchgang zwischen GND des S3 und der Busmasse (G auf der Busseite des ADuM): **offen**.
- Isolationsmessung, nur wenn ein Messgerät dafür da ist: alle Pins einer Seite verbunden gegen alle Pins der anderen, **250 V DC**. **In Ordnung:** mehrere MΩ oder mehr, ein Wert im kΩ-Bereich heißt, irgendwo besteht eine Verbindung. Keine Prüfung mit kV, siehe README.
- +Vo gegen 0V am B0505S bei wenig Last: **4,75–5,25 V**. Der MS2548 verträgt laut Datenblatt 4,5–5,5 V, den engeren Bereich nehme ich für den TJA1050. Liegt die Spannung darüber: Lastwiderstand, siehe README.

### A3 TX-Pegel bei Reset und Flashen

Der ADuM1201 hat am Eingang keinen Pull-up oder Pull-down (Datenblatt: ±10 µA, typisch 0,01 µA). Ist der TX-Pin des S3 offen, ist auch der Ausgang des ADuM unbestimmt. Dann sendet das RS485-Modul auf VE.Bus, oder der TJA1050 zieht den CAN-Bus dominant.

Messen gegen GND des S3:
- VE.Bus: ADuM-Pin **B1** (GPIO 5, seit 15.11.2026 der TX)
- CAN: ADuM-Pin **B1** (GPIO 14)

Die beiden RS485 zu den Packs brauchen das nicht: Ihren TX treibt der SC16IS752, sein RESET hängt nicht am S3.

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

### A6b SC16IS752 am SPI (CS GPIO 2, SI 42, SO 41, SCLK 38)

**Die Firmware dafür fehlt noch:** Treiber für den SC16IS752 und ein Reiter für die beiden rechten Kanäle. Pack 1 hängt an UART B, Pack 2 an UART A.

1. **Quarz:** 1,8432 MHz (Aufdruck, 15.11.2026). Die Baudrate ist Quarz / (16 × Teiler), höchstens also 115200 Baud mit Teiler 1. 9600 Baud gehen mit Teiler 12, beide ohne Abweichung.
2. **SPI prüfen** (Knopf im neuen Reiter): Die Firmware schreibt in das Scratchpad-Register beider Kanäle nacheinander `55`, `AA`, `00`, `FF` und liest jeden Wert zurück. Dazu liest sie das Line-Status-Register, nach dem Start `60` (Sendepuffer leer).
3. **In Ordnung:** alle 8 Werte richtig zurück, bei beiden Kanälen `60`. Kommt immer `00` oder `FF`, prüfen: CS, SO und den Pin I2C/SPI (für SPI auf low).

### A6c RS485 über Kreuz (alle drei Ketten)

Zwei Kanäle direkt verbinden: A an A, B an B, Busmasse an Busmasse. Dann sendet ein Kanal und der andere empfängt. So prüfe ich jede Kette in beide Richtungen, ohne USB-Adapter und ohne Anlage. Die Masseverbindung überbrückt die Trennung der beiden Busseiten nur für diesen Test.

1. Linker Kanal: Reiter VE.Bus / RS485 auf **RS485-Test**, **115200** Baud. Der S3 startet neu.
2. Beide rechten Kanäle im Reiter RS485 Packs auf **115200** Baud.
3. Drei Paare nacheinander verbinden: links mit Pack 1, links mit Pack 2, Pack 1 mit Pack 2.
4. Je Paar im Reiter RS485 Packs den Knopf **Kreuztest** drücken. Jeder Kanal sendet `55 AA 3x C3` (x: 0 links, 1 Pack 1, 2 Pack 2), die Tabelle zeigt, was jeder andere Kanal empfangen hat. Beim Sender selbst steht, ob das RS485-Modul das eigene Senden zurückgibt.
5. Dasselbe mit **9600** Baud.
6. **In Ordnung:** In allen 6 Richtungen und bei beiden Baudraten kommt die Folge unverändert an, ohne zusätzliche Bytes.
7. Danach die Verbindungen lösen und den linken Kanal zurück auf **VE.Bus, nur hören** stellen.

Der linke Kanal läuft über den UART des S3, seine Baudrate stimmt. Kommen bei links ↔ Pack falsche Bytes, aber bei Pack 1 ↔ Pack 2 richtige, ist die Quarzfrequenz falsch eingetragen. Beide rechten Kanäle laufen dann gleich falsch und verstehen sich trotzdem. Der Test ersetzt auch den offenen Teil von A6.

### A8 CH348

- CH348 an die native Buchse, 5 V über die USB-OTG-Brücke. COM und native Buchse nie gleichzeitig am PC.
- **In Ordnung:** Reiter VE.Direct zeigt „verbunden“, Version mit `8A`.

### A9 CMT2300A

- Nach dem Anlöten: Übersicht zeigt „gefunden“.
- Danach den Download-Modus prüfen, wegen SCLK auf GPIO 46: BOOT halten, RST kurz drücken, flashen über COM. **In Ordnung:** esptool meldet den Chip und schreibt.
- „gefunden“ beweist nur SPI. Den Empfang zeigt erst ein HMS im Reiter DTU: Abfrage einschalten. **In Ordnung:** erreichbar, Antworten ohne Ausfälle, Werte plausibel. Erledigt am 15.11.2026, siehe [tests.md](tests.md).

### A10 Relais-Pins

- Nach dem Start ist der Pin nicht getrieben. Die eigene Ansteuerung darf dann nicht schalten.
- Auf der Seite low und high schalten. **In Ordnung:** Pegel an GPIO 11 und 12 folgt, die Ansteuerung schaltet.

## Teil B: an der Anlage, nur hören

Voraussetzung: A1 bis A3 erledigt. Ablauf und Anschlussart stehen in [PLAN.md, Phase 2](../PLAN.md#phase-2-mithören). Hier stehen nur die Prüfpunkte.

| Schritt | Vorher | In Ordnung | Abbrechen, wenn |
|---|---|---|---|
| B1 MT681 Haus | parallel zum Tasmota | regelmäßig Telegramme, CRC-Fehler 0, Bezug wie beim Tasmota | Tasmota verliert Werte |
| B2 MT681 WP | parallel zu DTUoB_WP, berührt dessen Regelung | wie B1, dazu Rahmenfehler 0 | DTUoB_WP verliert Werte |
| B3 CAN am JK-BMS | A3 für GPIO 14 high. Parallel zu Venus: 120 Ω am Modul nur, wenn der Bus sonst nicht an beiden Enden abgeschlossen ist (nicht gemessen). Direkt umgeklemmt statt Venus: Abschluss am Modul dran, ESP_venus muss quittieren. | rund 27 Telegramme pro Sekunde, 21 IDs, Werte wie in Venus, Busfehler 0 | Fehlerzähler steigen, Venus meldet das BMS als weg |
| B4 VE.Bus am MultiPlus | A3 für GPIO 5 (ADuM B1) high, Bus über 100 kΩ gemessen, Lötbrücke 120_S offen. Das Modul hat einen MS2548 statt MAX13487: Umschaltung bei 256000 Baud und Last von Pull-up und Pull-down am Bus vorher klären. | Sync „empfangen“, Prüfsummenfehler bleiben bei 0, DC-Strom und Temperatur plausibel | Venus oder MultiPlus melden VE.Bus-Fehler |
| B5 VE.Direct | parallel an den Isolator-Ausgängen, FT4232 bleibt dran | Seriennummer erkannt, Instanz aus `devices.h`, Prüfsummen ohne Fehler, Werte wie in Venus | ein MPPT meldet Fehler #67 |
| B7 Regelung WP-HMS | Absprache. OpenDTU WP aus, sonst regeln zwei. MT681 WP probehalber an ESP_venus (GPIO 6). Regelung im Reiter DTU einschalten. | WP-Zähler bleibt um 25 W Bezug, neues Limit nur bei mehr als 3 W Abweichung, unter 66 W Bedarf schaltet der HMS ab und danach wieder ein | der Zähler zeigt dauerhaft Einspeisung, der HMS folgt den Limits nicht, die Batterie fällt unter die Stoppschwelle, ohne dass er abschaltet |
| B6 JK-BMS über RS485, Pack 1 und 2 | A6b und A6c erledigt. Die Abfrage sendet an das BMS (nur lesend), vorher absprechen. Nicht am selben Anschluss wie die OpenDTU WP, zwei Abfragende auf einem Bus stören sich. | je Pack alle Zellspannungen wie in der JK-App, Abfragen ohne Prüfsummenfehler | das BMS meldet Fehler, die OpenDTU WP verliert die Batteriedaten |

## Ergebnisse

| Schritt | Datum | Ergebnis | Bemerkung |
|---|---|---|---|
| A1 Versorgung | 14.11.2026 | offen | Über USB am Laptop: mit dem alten Kabel Unterspannung, mit einem anderen nicht. Im Betrieb wird die Versorgung anders gelöst. |
| A1 Versorgung | 17.11.2026 | offen | Über die COM-Buchse kommt keine Versorgung mehr an, vermutlich ist die Diode am COM defekt. Am 16.11. davor Startschleife wegen Unterspannung. |
| A2 Isolierte Busseiten | 14.11.2026 | in Ordnung | Durchgang S3-Masse zu Busmasse offen. |
| A3 TX-Pegel | 14.11.2026 | in Ordnung | ADuM A1 (GPIO 4) und B1 (GPIO 14) bei gedrücktem RST, im Download-Modus und im Betrieb immer über 3 V. Seit 15.11. sendet VE.Bus über GPIO 5 auf ADuM-Pin B1, dort neu messen. |
| A3 TX-Pegel | 17.11.2026 | hingenommen | ADuM B1 (GPIO 5, VE.Bus-TX) bei RST: Spitzen gegen GND, unter 1 V. Ich nehme das hin: Sie stören höchstens einzelne Frames während des Resets. Nach einem Reset baut ESP_venus VE.Bus ohnehin neu auf, der MultiPlus wartet 60 s auf den Sollwert. Download-Modus nicht gemessen. |
| A4 I²C und BME280 | 14.11.2026 | in Ordnung | SDA und SCL waren anders verdrahtet als geplant, übernommen: SDA 16, SCL 15. 0x76 außen, 0x77 der zweite Sensor, Lesefehler 0. |
| A5 MT681-Eingänge | 14.11.2026 | entfällt | Gleich am echten Zähler geprüft, siehe B1 und B2. |
| A6 Linke RS485-Kette | 14.11.2026 | teilweise | Pinprüfung in Ordnung: TX (GPIO 4) offen, RX (GPIO 5) vom ADuM high getrieben. Gesendet 15 Bytes, kein Echo. Ob das Modul sein Senden zurückgibt, ist offen. Sendeweg: A gegen B beim 10-s-Dauersenden messen. |
| A6b SC16IS752 am SPI | 15.11.2026 | in Ordnung | Scratchpad beider Kanäle 8 von 8 richtig, Gegenprobe Kanalwahl A `5A`, B `A5` richtig, Line-Status bei beiden `60`, Line-Control `1D`. |
| A6c RS485 über Kreuz | 15.11.2026 | fehlerhaft | Pack 1 mit Pack 2 verbunden (A, B, Busmasse): bei 115200 und 9600 Baud in keine Richtung etwas empfangen, kein Kanal gibt sein Senden zurück. Interne Schleife des SC16IS752 bei beiden UARTs und beiden Baudraten in Ordnung, Fehlerzähler 0. Der Fehler liegt also zwischen SC16IS752 und Bus. Ursache: TXD am RS485-Modul ist der Ausgang (MS2548 Pin 1, am Oszilloskop: unser Sendesignal kam an Pin 1 an), im Plan war es umgekehrt. ADuM gewendet, nach dem Umbau wiederholen. |
| A6c RS485 über Kreuz | 16.11.2026 | Pack 1 ↔ Pack 2 in Ordnung | Nach dem Wenden der ADuM: Jeder Kanal hörte nur sein eigenes Senden, beim anderen kam nichts an, auch mit anderen Kabeln. Beim 10-s-Dauersenden mit 0x55 überall konstant high gemessen. Ursache: A0 und B0 am ADuM1201 waren vertauscht, der ADuM1201 selbst ist in Ordnung. Danach Pack 1 → Pack 2 und Pack 2 → Pack 1 bei 115200 und 9600 Baud: `55 AA 3x C3` kommt unverändert an, Fehlerzähler 0, kein Echo mehr. Dauersenden Pack 2 → Pack 1: 2 Überläufe im SC16IS752. Der linke Kanal fehlt noch, sein ADuM ist nicht gewendet. Während der Messungen startete ESP_venus mehrmals wegen Unterspannung neu. |
| A6c RS485 über Kreuz | 17.11.2026 | in Ordnung, alle drei Paare | ADuM VE.Bus gewendet, Firmware mit RX GPIO 4 und TX GPIO 5 (Pinprüfung: RX high getrieben, TX offen). Links mit Pack 1 verbunden, Knopf Kreuztest bei 115200 und 9600 Baud: links → Pack 1 und Pack 1 → links kommen unverändert an, kein Echo. Danach links mit Pack 2 verbunden: ebenso in beide Richtungen bei 115200 und 9600 Baud in Ordnung. Damit sind alle 6 Richtungen geprüft. Beim Pack-Test vom 16.11. war A zeitweise offen, die Übertragung ging trotzdem: Eine Leitung und Busmasse reichen am Tisch, ein Ausfall fällt so nicht auf. Am 16.11. Neustartschleife mit Unterspannung, jedes Mal beim Start des WLAN. |
| A7 CAN-Kette | 14.11.2026 | in Ordnung | 10 gesendet, 10 richtig zurück, TX-Fehlerzähler 0, Busfehler 0. Zuerst fehlgeschlagen: Die Pins waren noch nicht verdrahtet, danach TX und RX vertauscht. Übernommen wie verdrahtet: TX 14, RX 13. Der Selbsttest liest jetzt vorher beide Pins und sendet nur, wenn RX getrieben und TX offen ist. |
| A8 CH348 | 14.11.2026 | mit Einschränkung | Version `8A 88 80 00`, 8 Ports mit 19200 Baud. Nach einem Neustart des S3 meldet er sich nur, wenn er selbst kurz ohne Strom war. |
| A9 CMT2300A | 15.11.2026 | in Ordnung | Beide HMS antworten, siehe [tests.md](tests.md). Download-Modus mit SCLK auf GPIO 46 noch nicht geprüft. |
| A10 Relais-Pins | | | |
| B1 MT681 Haus | 14.11.2026 | in Ordnung | GPIO 7 (getauscht wie verdrahtet), 124 Telegramme in 124 s, 0 CRC-Fehler. Ob parallel zum Tasmota, habe ich nicht notiert. |
| B2 MT681 WP | 14.11.2026 | in Ordnung | GPIO 6 per RMT, 34 Telegramme, 0 CRC-Fehler, 0 Rahmenfehler. |
| B3 CAN am JK-BMS | 17.11.2026 | in Ordnung, ohne Venus | CAN direkt von Venus auf ESP_venus umgeklemmt, Abschluss am Modul dran. Zuerst H und L vertauscht: 0 Frames, 0 Busfehler. Nach dem Tausch, listen-only: rund 3900 Frames/s, alle 0x35E („JK-BMS“). Ohne Quittung wiederholt das BMS seinen ersten Frame endlos. Firmware seitdem im Normalbetrieb: quittiert, sendet keine eigenen Frames. Danach 25 Frames/s, 19 IDs, Busfehler 0. Es fehlen nur 0x305 und 0x307, die kamen von Venus. Werte (Victron-Format): Ladeschluss 51,8 V, Lade-/Entladegrenze 161,5/289,7 A, Entladeschluss 40,5 V, SoC 26 %, SoH 100 %, 48,69 V, 0 A, 17,3 °C. Außerdem startete der CAN-Treiber zeitweise nicht (kein Level-1-Interrupt auf Core 0 frei), seitdem darf er Level 1 bis 3 nehmen. |
| B4 VE.Bus am MultiPlus | 17.11.2026 | abgebrochen | An der zweiten VE.Bus-Buchse eingesteckt, Firmware „nur hören“: Der MultiPlus ging sofort auf Störung. Ich hatte die 100 kΩ vorher nicht messen lassen. Danach am Modul gemessen: GND–A 6,3 kΩ, GND–B 4,5 kΩ, A–B 10 kΩ, also weit unter 100 kΩ. Auf dem Modul sitzt ein Pull-up an A und ein Pull-down an B. Beide müssen vor dem nächsten Versuch runter. |
| B4 VE.Bus am MultiPlus | 17.11.2026 | in Ordnung | Pull-up und Pull-down am Modul entfernt, der MultiPlus läuft mit ESP_venus am Bus. Zuerst kein Sync: Im Rohmitschnitt (RS485-Test, 256000 Baud) lag RX in Ruhe low und Frames kamen als `00 3E 3E` statt `FF 83 83`, A und B waren vertauscht. Nach dem Tausch: Frames `83 83 FD …`/`83 83 FE … FF`, rund 80 pro Sekunde. Im Modus „nur hören“: Sync ja, Prüfsummenfehler 0, DC-Strom 0,0–0,1 A, Temperatur 13 °C, Eingangsstromgrenze 25 A (Bereich 0–50 A). Werte stimmen mit Venus überein, Venus ohne VE.Bus-Fehler. |
| B5 VE.Direct | 14.11.2026 | ein MPPT in Ordnung | Port 0, `HQ2503KHTNV` = solarcharger 294, 27 Blöcke ohne Prüfsummenfehler. RX und TX waren zuerst gekreuzt. |
| B6 JK-BMS über RS485 | | | |
| B7 Regelung WP-HMS | | | |
| C1 ESS am MultiPlus, Senden | 17.11.2026 | Kommunikation in Ordnung, Regelung in Arbeit | MK3 ab, CAN direkt an ESP_venus, ESP_venus sendet den Sollwert (RAM 131, + einspeisen). Fest 0/+200/−200 W: jeder Befehl quittiert, Prüfsummenfehler 0, der MP2 folgt nach rund 4 s (AC −179 W bzw. +181 W). Der MS2548 sendet ohne Pull-up/Pull-down. Vorzeichen: Wechselrichter (RAM 14) negativ beim Entladen, AC-in (RAM 15) positiv beim Einspeisen, AC-out (RAM 16) offen (Kühltruhe lief nicht). Nulleinspeisung, Ziel 20 W, je Zählertelegramm (1 s) die halbe Abweichung nachführen: in Ruhe ±25 W, ein Sprung von 0 auf 3,5 kW dauert rund 9 s. Ein schneller Pfad (bei mehr als 150 W Abweichung direkt auf gemessene Einspeisung plus Abweichung) gab eine Dauerschwingung von −400 bis +700 W, wieder aus. Der MP2 folgt nur mit 300–400 W/s. Kurze Herdpulse (+1,9 kW für 3–4 s) ließen den Sollwert davonlaufen, danach bis 1,7 kW Einspeisung für 3–5 s. Seitdem bleibt der Sollwert höchstens 500 W neben der gemessenen Einspeisung, noch nicht ausgewertet (Lauf b nur 71 s, Last bis 6,6 kW über der Grenze des MP2). Aufzeichnungen: `docs/ess_regelung_2026-11-17_a.csv` (bis 88 s mit schnellem Pfad), `_b.csv`. Danach Senden aus, Rückbau auf Venus. |
