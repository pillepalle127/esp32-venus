# Plan: ESP32-S3 statt Venus OS

Ziel, Anlage und Quellen stehen in der [README](README.md). Hier steht die Reihenfolge.

## Grundsätze

- Zuerst kläre ich, was das Projekt kippen kann.
- Die Phasen 0 bis 3 schreiben nichts in die Anlage. Der Pi regelt weiter.
- Der Pi bleibt bis zum Schluss betriebsbereit. Der Rückweg ist immer die alte Verkabelung.
- Jeden Schritt am VE.Bus oder an der Regelung spreche ich einzeln ab.
- Der S3 protokolliert selbst und vergleicht selbst. Niemand muss etwas beobachten.

## Phase 0: Bestand erfassen

Keine Hardware. Ich will wissen, was der Pi wirklich alles tut.

- Alle Dienste auf Venus, bisher habe ich nur fünf Typen abgefragt. Dazu die Zusatzdienste in `/data`.
- ESS- und DVCC-Einstellungen (`/Settings/CGwacs`, `/Settings/SystemSetup`, `/Settings/Services`).
- CAN-Mitschnitt von `can1`: Welche Telegramme sendet das JK-BMS, woher kommen die Zellspannungen?
- Die genaue Liste der 38 N-Themen und 3 W-Themen, die ioBroker nutzt.
- Wer den MQTT-Keepalive an Venus schickt. Ein leerer Keepalive löst eine volle Neuveröffentlichung aus, das muss der S3 genauso können.
- Wer `mqtt_pv_100` speist und wie `dbus-opendtu` an die HMS-Werte kommt.
- Woher DTUoB_WP seinen Zählerwert und die Batterie-Zellen bekommt.
- Versorgung des Pi heute.

**Fertig, wenn** jede Funktion als „nachbauen“ oder „entfällt“ eingetragen ist.

## Phase 1: Machbarkeit auf dem Tisch

Keine Verbindung zur Anlage.

1. **CH348 am USB-Host.** Erledigt am 03.11.2026: erkannt mit Full Speed, 5 von 8 Kanälen.
2. **Treiber.** Erledigt am 03.11.2026: alle 8 Ports als Schleife mit 19200 Baud ohne Fehler.
3. **Gesamtbuild.** `ve.bus-library`, `lib/Hoymiles`, WLAN, TWAI, USB-Host und MQTT-Broker in einer Firmware (`firmware/`). Erledigt am 03.11.2026 mit Arduino 3.3.12 auf ESP-IDF 5.5.5: Sie läuft auf dem Board, der CH348-Schleifentest läuft darin fehlerfrei mit, WLAN ist verbunden. CMT2300A und Transceiver waren noch nicht angeschlossen, VE.Bus und CAN hatten also nichts zu tun.
4. **Timing unter Last.** Ein zweiter S3 spielt den MultiPlus und sendet alle 20 ms einen Sync-Frame. Der Prüfling antwortet im Fenster von 8,0–9,5 ms, während USB, WLAN, MQTT und Funk voll laufen. Ich zähle die verpassten Fenster, auch während Schreibvorgängen im Flash, denn die halten beide Kerne kurz an. Erledigt am 03.11.2026: 9000 von 9000 Antworten, ohne Fehler, Beginn 8,95–10,53 ms nach dem Sync. Core 1 hat dabei 5 % Last, Core 0 rund 50 %, davon der größte Teil durch künstlichen Netzverkehr. Für die Last muss ich den Funk deshalb nicht anschließen.
**Fertig, wenn** alles läuft und die Stückliste feststeht.

**Hält das Timing nicht,** melde ich das mit Messwerten, und du entscheidest.

## Phase 2: Mithören

Der S3 hängt an der Anlage und sendet nichts.

- **VE.Bus:** An der zweiten VE.Bus-Buchse des MultiPlus, TX-Leitung gar nicht angeschlossen. Vor dem Stecken messe ich, dass der Bus über 100 kΩ sieht. Der Mitschnitt zeigt auch, was `mk2-dbus` heute an den MultiPlus schickt, vor allem Sollwert und Grenzen.
- **CAN:** TWAI im Listen-only-Modus am Bus `can1`, Abgleich mit dem Mitschnitt aus Phase 0.
- **VE.Direct:** Empfang parallel an den Isolator-Ausgängen, der FT4232 bleibt dran. So stoppt kein MPPT mit Fehler #67. Geht das elektrisch nicht, stecke ich nachts einzeln um.
- **MT681:** Empfang parallel zum Tasmota. Geht das nicht, nehme ich die Werte in dieser Phase per MQTT vom Tasmota.- **Vergleich:** Der S3 liest die N-Themen von Venus mit und protokolliert die Abweichungen.

**Fertig, wenn** die Werte passen, keine Telegramme verloren gehen und der S3 mehrere Tage ohne Neustart läuft, bei Sonne und bei trübem Wetter.

## Phase 3: Schattenbetrieb

Der S3 rechnet alles, schickt aber nichts an die Geräte. So habe ich es auch bei der Kellerlüftung gemacht.

- **`system/0`** aus den offenen Quellen von `dbus-systemcalc-py`.
- **DVCC** nach `delegates/dvcc.py`: Grenzen aus dem BMS, Verteilung des Ladestroms, `NetworkMode` 13, Takt 3 s.
- **ESS-Regler** als Eigenbau: vom Netzwert zum Sollwert, dazu BatteryLife, 20 % Mindest-SOC, `AcPowerSetPoint`, `Mode`. `hub4control` steht in einer Drittliste als nur binär.
- **MQTT:** Broker mit den Themen von Venus, zunächst auf einer eigenen Adresse.
- **Vergleich:** Vorgaben je MPPT, Sollwert und alle genutzten Themen gegen Venus.
- **Störfälle:** Tests auf dem Rechner mit den Mitschnitten aus Phase 2. BMS weg, Zähler weg, Netz weg, Neustart.

**Fertig, wenn** Vorgaben und Sollwert im Rahmen liegen, jede Abweichung erklärt ist und alle Störfälle das erwartete Ergebnis liefern.

## Phase 4: Umschalten in Stufen

1. **Ein MPPT.** Nachts umstecken, mit TX. Der S3 schickt ihm die Vorgaben, die anderen bleiben an Venus. Prüfung am nächsten Tag: kein Fehler #67, er folgt der Grenze.
2. **MultiPlus, kurzer Test.** Du bist dabei. Pi vom VE.Bus, der S3 sendet zum ersten Mal, erst 0 W, dann kleine Werte. Ich messe auch, was bei ausbleibendem Sollwert passiert (laut Quelle Passthrough nach 60 s) und bei einem Neustart des S3. Danach kommt der Pi wieder dran.
3. **Alles.** Pi aus. Der S3 übernimmt 192.168.1.10, alle MPPT, BMS, MultiPlus und Zähler. Auf dem CAN-Bus sendet er dann auch `305` und `307` ans BMS, die heute vermutlich von Venus kommen. Der Lesekopf wechselt, `victron_meter.be` wird nicht mehr geladen, die Stände `vic_wh_out` und `vic_wh_in` übernehme ich. ioBroker bleibt unverändert, bis auf die Zählerwerte, die heute vom Tasmota kommen.

**Rückweg:** FT4232 und MK3 wieder an den Pi, Pi an, Lesekopf zurück, `load("victron_meter.be")`.

**Fertig, wenn** vier Wochen kein Vorfall auftritt. Logs werte ich nur bei einem Vorfall aus.

## Phase 5: Wechselrichter und Kellerlüftung

1. **HMS Garage.** OpenDTU aus, der S3 fragt per Funk ab. Die Themen unter `DTUoB_Garage/` bleiben gleich, die Leistung geht in `system/0` ein.
2. **DTUoB_WP.** Der Power Limiter kommt neu. Diese Themen bleiben gleich, die PV-Prognose hängt daran:
   - `DTUoB_WP/powerlimiter/cmd/mode` (0 oder 1, ohne retain)
   - `DTUoB_WP/powerlimiter/status/mode`
   - `DTUoB_WP/1164a012bcc6/0/yieldtotal`, `powerdc`
   - `DTUoB_WP/powermeter/*`

Der Modus steht heute nach einem Neustart auf 0, darauf verlässt sich das Skript. Der S3 übernimmt von der DTU den zweiten MT681 (GPIO 21) und die RS485-Leitung zum JK-BMS (CH348 Port 7). Bis dahin kommen die Zellspannungen weiter von der DTU per MQTT. Beim Umstecken klärt sich, ob ein Anschluss für beide Packs reicht. MultiPlus und DTUoB_WP teilen sich dann die Entladegrenze des BMS. Das ist neu, weil heute keiner vom anderen weiß.

3. **Kellerlüftung.** Die beiden BME280, der Lüfter und die Regelung aus `kellerlueftung.be` ziehen vom Tasmota um. Der S3 spricht dafür auch die Themen des Tasmota, damit die Datenpunkte unter `sonoff.0.Tasm_Kellercenter2` gleich bleiben. Danach entfällt der Tasmota.

**Fertig, wenn** beide DTUs und der Tasmota vier Wochen aus sind, ohne Vorfall.

## Phase 6: Abschluss

Pi, FT4232, MK3, die beiden DTUs und der Tasmota kommen in die Reserve. Die README beschreibt dann den Betrieb, nicht mehr den Plan.

## Aufbau der Firmware

- **Core 1** nur für VE.Bus.
- **Core 0** für alles andere: WLAN, MQTT, USB, CAN, SML, Funk, DVCC, ESS.
- **Ein Datenmodell** nach den Pfaden von Venus. Die Treiber schreiben hinein, MQTT und Regelung lesen daraus.

Regeln:

1. Die Regelung hängt nie am Netz. Fällt ioBroker, Broker oder DNS aus, laufen VE.Bus, DVCC und Zähler weiter. Netzwerkaufrufe blockieren nie, Adressen sind fest.
2. Ist das BMS nicht frisch, gehen keine Vorgaben mehr an die MPPT. Sie stoppen dann mit Fehler #67, statt blind zu laden.
3. Ist der Zähler nicht frisch (10 s wie in `victron_meter.be`), geht der Sollwert auf 0 W. Bleibt das so, geht der MultiPlus in Passthrough, wie heute.
4. Der Sollwert ist immer begrenzt durch die Lade- und Entladegrenze des BMS und durch den Mindest-SOC.
5. Watchdog auf allen Tasks. Vor jedem Neustart und Update schalte ich den Bus stumm (`setTxEnabled(false)`). DE und TX sind per Widerstand so vorgespannt, dass der Bus beim Hochfahren frei bleibt.
6. Updates über WLAN mit zwei Partitionen. Besteht die neue Firmware ihre Selbstprüfung nicht, geht sie automatisch zurück.
7. Parser und Regelung teste ich auf dem Rechner, mit echten Mitschnitten.

## Webseite

Der S3 bekommt eine eigene Webseite, sie muss sein. Sie ersetzt auch das HDMI-Display am Pi, denn der S3 hat kein HDMI. Die Oberfläche von Venus lässt sich nicht übernehmen.

- **Anzeigen, ab Phase 2:** eine Übersicht wie bei Venus mit Netz, Verbrauch, PV und Batterie, dazu SoC, jeder MPPT, BMS-Grenzen, Zustand von VE.Bus, Zähler und CAN, Fehlermeldungen, Neustarts.
- **Einstellen, ab Phase 4:** ESS-Modus, Mindest-SoC, Netz-Sollwert, Firmware-Update. Nur mit Passwort.
- Sie läuft auf Core 0 und blockiert die Regelung nie. Ein Aufruf darf VE.Bus, DVCC und Zähler nicht berühren.
- Die Diagramme bleiben in ioBroker. Die Seite zeigt den aktuellen Stand und funktioniert auch ohne ioBroker.
- **E-Paper, optional:** Die Pins sind reserviert. Es zeigt langsame Werte wie SoC, PV und Fehler, ein paar Mal pro Stunde neu gezeichnet.

## Hardware (vorläufig)

- ESP32-S3 mit 16 MB Flash und 8 MB PSRAM, COM-Port und nativem USB.
- Ein CMT2300A mit Antenne für beide HMS.
- E-Paper-Display, optional.
- CH348 (oder 4 × SC16IS752), die vorhandenen Isolatoren für die 7 VE.Direct-Kanäle.
- RS485-Transceiver für VE.Bus, isoliert.
- CAN-Transceiver für 3,3 V, isoliert.
- Zwei Leseköpfe für die beiden MT681, rund 2 m Leitung.
- Die beiden BME280 und das Lüfter-Relais vom Tasmota.

## Größte Risiken

1. **VE.Bus-Timing am echten MultiPlus.** Neben USB und WLAN hält es gegen die Simulation. Ob der MultiPlus die Zeitpunkte annimmt, zeigt erst Phase 2 und 4.
1a. **Versorgung.** Das Testboard hatte unter Last an der COM-Buchse einen Brownout. Die endgültige Versorgung muss USB, WLAN, Funk und CH348 sicher tragen.
2. **CH348 im Betrieb.** Treiber und Schleifentest laufen. Das Protokoll ist aber nicht offen, und Abstecken behandelt der Treiber noch nicht. Die Ausweichlösung 4 × SC16IS752 bleibt im Plan.
3. **Grenzen am MultiPlus.** Wie `mk2-dbus` sie über den VE.Bus schickt, ist nicht offen. Der Mitschnitt in Phase 2 soll es zeigen. Sonst begrenze ich nur über den Sollwert.
4. **Qualität des eigenen ESS-Reglers.** Sie hängt am Takt des MT681, den ich noch nicht kenne.
5. **Ausfall im BMS-Modus.** Ohne Vorgaben stoppen alle sechs MPPT. Das soll so sein, kostet aber PV-Ertrag, bis der S3 wieder läuft.

## Offene Entscheidungen

- Platz und Versorgung des S3.
- Lizenz: `lib/Hoymiles` steht unter GPL-2.0-or-later, die CH348-Befehle stammen aus einem GPL-2.0-Treiber, PicoMQTT unter LGPL-3.0. Das zählt erst, wenn ich die Firmware veröffentliche.
