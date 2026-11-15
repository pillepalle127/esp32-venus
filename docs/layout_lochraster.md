# Layout auf Lochraster (grobe Platzierung)

Der Plan mit allen Löchern und Drähten steht in [layout_lochraster.drawio](layout_lochraster.drawio). Hier steht nur die Belegung der Stiftleisten.

Board: ESP32-S3-WROOM-1 N16R8. Pinbelegung wie in `firmware/src/pins.h`. Jede Zeile ist ein Pin in der Reihenfolge der Stiftleiste.

```
                                  ANTENNE
                               oben freihalten
                             +---------------+
  3V3-Schiene ---------------| 3V3       GND |--------------- GND-Schiene
  CMT2300A ------------------| 3V3    TX  43 |  frei
                             | RST    RX  44 |  frei
┌ ADuM, RS485     -- RX -----|  4          1 |  frei
└ VE.Bus          -- TX -----|  5          2 |----- CS ----┐ SC16IS752,
┌ Stecker MT681   -- WP -----|  6         42 |----- SI ----┤ Drähte gerade
└                 -- Haus ---|  7         41 |----- SO ----┤ hinüber
┌ Stecker BME280  -- SCL ----| 15         40 |  frei       │
└                 -- SDA ----| 16         39 |  frei       │
                     frei    | 17         38 |----- SCLK --┘
                     frei    | 18         37 |  belegt
┌ CMT2300A        -- CSB ----|  8         36 |  belegt
│ oben am S3      -- SDIO ---|  3         35 |  belegt
│ angelötet       -- SCLK ---| 46          0 |  frei
└                 -- FCSB ---|  9         45 |  frei
                     frei    | 10         48 |  RGB-LED des Boards
  Lüfter          -- OUT ----| 11         47 |  frei
  Relais 2        -- OUT ----| 12         21 |  frei
┌ ADuM, TJA1050   -- RX -----| 13         20 |  USB, frei
└ CAN             -- TX -----| 14         19 |  USB, frei
  5 V Einspeisung -----------| 5V        GND |--------------- GND-Schiene
  GND-Schiene, CMT2300A -----| GND       GND |--------------- GND-Schiene
                             +---------------+
                               USB-C   USB-C
                       nativ: CH348    COM: Flashen
```

Der CH348 hängt am USB-Kabel. Der CMT2300A ist per Pigtail oben am S3 angelötet, an den vier Pins von GPIO 8 bis GPIO 9, am zweiten 3V3-Pin und am untersten GND-Pin. Die Signalnamen stehen so auf dem Modul. GPIO1 bis GPIO3 des Moduls bleiben frei: Ohne GPIO3 fragt die Firmware den Paketeingang per SPI ab, GPIO2 wertet sie nicht aus. Auf der Platine hat er keinen Draht.

GPIO 3 und 46 sind Strapping-Pins. GPIO 46 muss beim Reset low sein, sonst kommt der S3 nicht in den Download-Modus. Der interne Pull-down hält ihn low, und SCLK ist am CMT2300A ein Eingang. GPIO 3 trägt SDIO, das treibt beim Start niemand.

Bei den Pfostensteckern kreuzen sich zwei Drähte zweimal ohne Verbindung: SCL läuft von GPIO 15 neben dem S3 nach unten zu den C-Pins, SDA von GPIO 16 kreuzt es auf dem Weg zu den D-Pins. SDA auf 16 und SCL auf 15 ist so verdrahtet und am 14.11.2026 mit einem BME280 getestet. Beim MT681-Stecker kreuzen sich die beiden Zählerdrähte einmal ohne Verbindung: Haus liegt auf GPIO 7, WP auf GPIO 6, so ist es verdrahtet. Ebenso kreuzen sich die CAN-Drähte einmal: RX liegt auf GPIO 13, TX auf GPIO 14, so ist es verdrahtet.

Drei Pfostenstecker 1 x 4 sitzen übereinander neben den GPIOs:

| Stecker, von oben | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| MT681 | GND | 3V3 | Haus | WP |
| BME280 1 | GND | 3V3 | SDA | SCL |
| BME280 2 | GND | 3V3 | SDA | SCL |

Nach Bereichen:

| Seite | Oben | Mitte | Unten |
|---|---|---|---|
| Links | RS485-Modul VE.Bus außen, daneben ADuM VE.Bus und Pfostenstecker | B0505S VE.Bus | CAN |
| Rechts | SC16IS752 (CJMCU-752) | RS485-Modul Pack 1 waagerecht, ragt über den Rand | RS485-Modul Pack 2 waagerecht, ragt über den Rand |

Die Klemmen des RS485-Moduls für VE.Bus zeigen zur Oberkante, die der beiden Module für die Packs nach rechts über den Rand. Der CAN-Stecker liegt an der Unterkante.

Rechts, die beiden RS485 zu den Packs:
- Der SC16IS752 sitzt oben rechts, wo vorher die Stiftleiste des E-Paper war. Seine Einzelreihe (VCC bis SDA/VSS) liegt in den Reihen von GPIO 43 bis 37, CS, SI, SO und SCLK gehen gerade hinüber zu GPIO 2, 42, 41 und 38. I2C/SPI und SDA/VSS liegen an GND (SPI-Modus), RESET an 3V3, IRQ bleibt frei. Die Beschriftung steht auf der Unterseite des Moduls: Von oben gesehen liegt die Einzelreihe links, TXA bis CTSB in der Doppelreihe ganz rechts.
- Darunter die beiden RS485-Module waagerecht übereinander, Pins links, Klemmen rechts über den Rand.
- Der ADuM1201 jedes Packs hängt unter der Platine unter seinem Modul, gewendet: Bauteile nach unten. Seite 2 zeigt zum RS485-Modul, Seite 1 zum SC16IS752, wie beim ADuM für VE.Bus. Von oben gesehen liegt Seite 2 als U2, A1, B0, G2 in den Reihen von VCC, TXD, RXD und GND: Alle vier gehen gerade hinüber, ohne Kreuzung. Kanal A läuft vom TXD des Moduls (A1) zum RX des SC16IS752 (A0), Kanal B vom TX des SC16IS752 (B1) zum RXD des Moduls (B0).
- Der B0505S jedes Packs steht links neben seinem Modul.
- Jedes Pack hat seine eigene Trennung. Eine gemeinsame würde die Massen beider Packs über den SC16IS752 verbinden.
- Pack 1 hängt an UART B, Pack 2 an UART A. So liegen die Wege ganz rechts ineinander, ohne Kreuzung.

Der ADuM1201 für VE.Bus sitzt oben neben den S3-Pins, oberhalb der Pfostenstecker, gewendet: Bauteile zur Platine. GPIO 4 und 5 laufen gerade hinüber. Die vier Drähte zum RS485-Modul laufen unter dem Modul und kreuzen sich nicht:

| S3-Seite | Busseite | RS485-Modul |
|---|---|---|
| U1 an 3V3 | U2 | VCC |
| A0 an GPIO 4 (RX) | A1 | TXD, Ausgang des Moduls |
| B1 an GPIO 5 (TX) | B0 | RXD, Eingang des Moduls |
| G1 an GND | G2 | GND |

TXD am Modul ist der Ausgang des Empfängers (MS2548 Pin 1, gemessen am 15.11.2026), RXD geht an den Eingang des Treibers (Pin 4). Bis zum 15.11. war es hier umgekehrt gezeichnet.

Beim ADuM1201 läuft Kanal A von Seite 2 nach Seite 1, Kanal B umgekehrt (Datenblatt, Tabelle 16). Auf dem Modul heißt die Ziffer 1 Eingang und 0 Ausgang:

| Modul | IC-Pin | Datenblatt | Richtung |
|---|---|---|---|
| U1 | 1 | VDD1 | Versorgung Seite 1 |
| A0 | 2 | VOA | Ausgang Seite 1 |
| B1 | 3 | VIB | Eingang Seite 1 |
| G1 | 4 | GND1 | Masse Seite 1 |
| G2 | 5 | GND2 | Masse Seite 2 |
| B0 | 6 | VOB | Ausgang Seite 2 |
| A1 | 7 | VIA | Eingang Seite 2 |
| U2 | 8 | VDD2 | Versorgung Seite 2 |

TX vom S3 geht deshalb immer auf A1 oder B1, RX zum S3 kommt immer von A0 oder B0. Der ADuM für VE.Bus hat Seite 2 am S3, die für CAN und RS485 haben Seite 1 am S3. Beides funktioniert, solange alle vier Pins einer Seite auf derselben Seite der Trennung hängen. Die Netzliste aus dem Plan habe ich am 14.11.2026 für alle drei Module so geprüft. Die Beschriftung habe ich nur am Foto des Händlers mit dem Datenblatt verglichen.

Der B0505S steht unten neben dem Modul: +Vo an VCC, 0V am GND-Draht, Vin und GND kommen von rechts.

Die Bestückung an GPIO 11 und GPIO 12 steht nicht im Plan, die mache ich anders.
