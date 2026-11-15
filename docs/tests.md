# Tests und Messungen

Ausführliche Protokolle. Den kurzen Stand zeigt die [README](../README.md#stand), die Prüfschritte mit Ergebnissen stehen im [Testplan](testplan.md).

## Test CH348 (03.11.2026)

Aufbau:
- ESP32-S3 mit 16 MB Flash und 8 MB PSRAM, MAC `7C:4F:AD:1E:04:5C`.
- CH348 am nativen USB-Port, PC am COM-Port (UART0).
- 5 V für den CH348 über die geschlossene Brücke `USB-OTG`. Der native Port und COM dürfen dann nicht gleichzeitig am PC stecken.
- Mit dem ersten Kabel versorgte die COM-Buchse das Board nur in einer Steckrichtung. Ob das am Kabel oder an der Buchse lag, habe ich nicht geprüft.

Erkennung ([ganze Ausgabe](test_ch348_2026-11-03.log)):

```
ERGEBNIS: 1a86:55d9 erkannt, Full Speed, 1 Interfaces belegt, 0 fehlgeschlagen, 5 von 8 Host-Kanaelen (Endpunkte + EP0)
```

Schleifentest mit dem Treiber aus `firmware/lib/ch348/`, an jedem Port TX auf RX ([Ausgabe](test_ch348_schleife_2026-11-03.log)):

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

`firmware/` läuft auf dem Testboard ([Ausgabe](gesamt_2026-11-03.log)):

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

Ergebnis über 6 Blöcke zu je 30 s ([Simulation](timing_simulation_2026-11-03.log), [Testboard](timing_testboard_2026-11-03.log)):

- 9000 Sync-Frames, 9000 Antworten, keine Prüfsummenfehler, keine falsche Frame-Nummer.
- Beginn der Antwort 8,95 bis 10,53 ms nach dem Beginn des Sync-Frames, im Mittel 10,0 ms.

Rechenlast dabei, je Task in Prozent eines Kerns ([Ausgabe](auslastung_2026-11-03.log)):

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

Damit ich die Anschlüsse vorab prüfen kann, zeigt die Firmware (Env `s3`) auf einer Webseite, was ankommt: `http://<IP>/` oder `http://esp32-venus.local/`. Ablauf der Prüfungen: [docs/testplan.md](testplan.md). Die alte Gesamtfirmware mit CH348-Schleifentest liegt jetzt in `firmware/timing/` und baut weiter als Env `s3_timing`.

| Reiter | Inhalt |
|---|---|
| Übersicht | alle Werte in einer Tabelle, Relais-Pins schalten |
| I²C | Scan, beide BME280 mit Temperatur, Feuchte, Druck, Taupunkt, absoluter Feuchte |
| Zähler | MT681 Haus (UART2) und WP (RMT): Leistung, 1.8.0, 2.8.0, Telegramme, CRC- und Rahmenfehler |
| CAN | JK-BMS nach Victron-Format gelesen, alle IDs mit Rohdaten, Fehlerzähler |
| VE.Bus / RS485 | linker Kanal: VE.Bus nur hören oder RS485-Test |
| VE.Direct | 8 Ports am CH348: Gerät, Spannung, Strom, PV, Zustand, Prüfsummen |
| Funk | CMT2300A: Mithören, Kanalscan, gehörte Geräte, Pakete, Spannung am Chip (seit 15.11.) |
| DTU | Geräteliste mit Name, Seriennummer, Abfrage ein/aus und Abstand, Werte der HMS (seit 15.11.) |
| Konsole | Protokoll der Firmware, auch Meldungen der IDF-Treiber |
| System | Laufzeit, Neustartgrund, WLAN, Speicher, Firmware-Update über WLAN |

Gesendet wird nur auf Knopfdruck im RS485-Test und für Geräte, deren Abfrage im Reiter DTU eingeschaltet ist:
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

## Funk mit dem CMT2300A (15.11.2026)

Modul per Pigtail an CSB (GPIO 8), SDIO (3), SCLK (46) und FCSB (9), VCC an 3V3, GND. GPIO1 bis GPIO3 des Moduls bleiben frei: Ohne GPIO3 fragt die Bibliothek den Paketeingang per SPI ab, GPIO2 wertet sie nicht aus.

- Reiter Funk: Mithören auf einer Frequenz, Kanalscan 863–870 MHz mit 6 s je Kanal, gehörte Geräte, die letzten 24 Pakete. Sendet nie. Die Kennung im Funk sind die letzten 8 Stellen der Seriennummer.
- Reiter DTU: Geräteliste mit Name, Seriennummer, Abfrage ein/aus und Abstand je Gerät, dazu DTU-Frequenz und Sendeleistung. Gesendet wird nur für eingeschaltete Geräte. Alles steht im NVS. Die Seriennummern sind nicht im Repo: Die erste Belegung kommt aus `firmware/src/inverters.h` (Vorlage `inverters.h.example`), danach gilt die Seite.
- Mithören und Kanalscan unterbrechen die Abfrage, ein Knopf setzt sie fort.

Ergebnisse:
- Die Seite meldete „gefunden“ auch mit abgerissenem GND am Modul. Der Test schreibt 0xAA in ein Register und liest es zurück, das lief trotzdem durch. Gültige Pakete kamen erst mit GND.
- Spannung am Chip aus der Messung des CMT2300A (VDD = LBD_RESULT / 255 × 4,8 V, AN147): 3,01–3,12 V. Ohne GND nicht gemessen.
- Ein HMS sendet nur, wenn eine DTU ihn abfragt. Mithören zeigt also nur Verkehr, wenn eine DTU arbeitet.
- Garagen-HMS: Die OpenDTU Garage fragte ihn auf 863,25 MHz ab und bekam keine Antwort. ESP_venus als DTU auf 865,00 MHz bekam zuerst auch keine. Nach dem Kanalwechsel-Befehl auf 868,00 MHz antwortete er sofort, mit −51 dBm. Er hörte also nicht auf 865 MHz, wie die OpenDTU-Doku es nach 15 Minuten ohne Kontakt beschreibt.
- Frequenzwechsel der DTU von 865,00 auf 869,50 MHz (14:41 Uhr): ESP_venus schickte den Kanalwechsel auf 868,00 MHz, beide HMS antworteten aber nicht. Sie arbeiteten weiter auf 865,00 MHz und hörten 868,00 MHz nicht. Um 14:56 Uhr, rund 15 Minuten nach dem letzten Kontakt, antworteten beide auf 869,50 MHz. Ein Wechsel der DTU-Frequenz kostet also rund 15 Minuten ohne Daten.
- Sendeleistung (je 60 s): bei −10 dBm antwortete der Garagen-HMS nicht (20 Anfragen, 0 Antworten), der WP-HMS kam nur 34-mal statt 60-mal dran. Bei 0 dBm beide fehlerfrei. Eingestellt: 0 dBm.
- WP-HMS im Abstand von 1 s: 244 Anfragen, 244 Antworten, kein Ausfall. Signal −65 bis −84 dBm je nach Standort von ESP_venus. Garagen-HMS alle 5 s.
- Die Bibliothek hat nur einen gemeinsamen Abstand und fragt reihum ab. Den Abstand je Gerät regelt die Firmware: Sie gibt immer nur ein fälliges Gerät frei, und nur bei leerer Warteschlange der Bibliothek.
- `isReachable()` und `isProducing()` der Bibliothek sind nur bei freigegebener Abfrage wahr. Die Seite rechnet „erreichbar“ selbst: Daten da, Fehlerzähler unter der Schwelle.
- Pakete mit Länge 0 beim Mithören: Der Chip meldet Präambel, Sync und „Paket fertig“, aber kein CRC_OK, der FIFO ist leer. Er verwirft also Pakete mit falscher CRC. Auf 863,25 MHz, wo die OpenDTU Garage sendete, kamen in 60 s 32 gültige Anfragen und 88 solche Pakete, auf 865,00 MHz ohne fremde DTU keines. Am wahrscheinlichsten sind es schwach empfangene Pakete der OpenDTU Garage. Die Seite zeigt sie als „CRC-Fehler im Chip“.
- Antenne bei allen Tests: eine 2,4-GHz-WLAN-Antenne.

Am PC getestet, bevor die Firmware aufs Board ging:
- SML: ein Telegramm mit 1.8.0, 2.8.0 und 16.7.0, Mehrbyte-Längenfeld, Müll davor. Werte richtig, verfälschte CRC wird verworfen.
- RMT-Empfang: Der Decoder tastet wie ein UART in Bitmitte ab, synchronisiert auf jede Startflanke. Fehlerfrei, solange Baudabweichung über ein Zeichen, Flankenverzögerung und Zittern zusammen unter einer halben Bitzeit bleiben (52 µs). Bei exakter Baudrate sind das zum Beispiel ±10 µs Zittern plus 20 µs Verzögerung der steigenden Flanke. Wie gut der Lesekopf das einhält, zeigen die Rahmenfehler am echten Zähler.
- BME280: Temperatur und Druck mit dem Beispiel aus dem BMP280-Datenblatt (25,08 °C), Feuchte gegen die Gleitkomma-Formel von Bosch, Abweichung unter 0,01 %.
- VE.Direct: Blöcke mit Prüfsumme, eingeschobene Hex-Meldung, verfälschter Block wird verworfen.
