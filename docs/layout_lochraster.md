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
┌ ADuM, MAX13487  -- TX -----|  4          1 |  frei
└ VE.Bus          -- RX -----|  5          2 |----- CS ----┐ SC16IS752,
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
| Links | MAX13487 VE.Bus außen, daneben ADuM VE.Bus und Pfostenstecker | B0505S VE.Bus | CAN |
| Rechts | SC16IS752 (CJMCU-752) | MAX13487 Pack 1 waagerecht, ragt über den Rand | MAX13487 Pack 2 waagerecht, ragt über den Rand |

Die Klemmen des MAX13487 für VE.Bus zeigen zur Oberkante, die der beiden Module für die Packs nach rechts über den Rand. Der CAN-Stecker liegt an der Unterkante.

Rechts, die beiden RS485 zu den Packs:
- Der SC16IS752 sitzt oben rechts, wo vorher die Stiftleiste des E-Paper war. Seine Einzelreihe (VCC bis SDA/VSS) liegt in den Reihen von GPIO 43 bis 37, CS, SI, SO und SCLK gehen gerade hinüber zu GPIO 2, 42, 41 und 38. I2C/SPI und SDA/VSS liegen an GND (SPI-Modus), RESET an 3V3, IRQ bleibt frei. Die Beschriftung steht auf der Unterseite des Moduls: Von oben gesehen liegt die Einzelreihe links, TXA bis CTSB in der Doppelreihe ganz rechts.
- Darunter die beiden MAX13487-Module waagerecht übereinander, Pins links, Klemmen rechts über den Rand.
- Der ADuM1201 jedes Packs hängt unter der Platine unter seinem Modul, mit den Bauteilen zur Platine. Seite 1 zeigt zum MAX13487, Seite 2 zum SC16IS752, wie beim ADuM für VE.Bus. Von oben gesehen liegt Seite 1 als U1, A0, B1, G1 in den Reihen von VCC, TXD, RXD und GND: Alle vier gehen gerade hinüber, ohne Kreuzung. Kanal A läuft vom SC16IS752 (A1) zum TXD (A0), Kanal B vom RXD (B1) zum SC16IS752 (B0).
- Der B0505S jedes Packs steht links neben seinem Modul.
- Jedes Pack hat seine eigene Trennung. Eine gemeinsame würde die Massen beider Packs über den SC16IS752 verbinden.
- Pack 1 hängt an UART B, Pack 2 an UART A. So liegen die Wege ganz rechts ineinander, und es kreuzen sich nur TX und RX je Pack einmal. Mit A für Pack 1 wären es sechs Kreuzungen. Gefädelt geht beides.

Der ADuM1201 für VE.Bus sitzt oben neben den S3-Pins, oberhalb der Pfostenstecker. GPIO 4 und 5 laufen gerade hinüber. Die vier Drähte zum MAX13487 laufen unter dem Modul und kreuzen sich nicht:

| S3-Seite | Busseite | MAX13487 |
|---|---|---|
| U2 an 3V3 | U1 | VCC |
| A1 an GPIO 4 (TX) | A0 | TXD |
| B0 an GPIO 5 (RX) | B1 | RXD |
| G2 an GND | G1 | GND |

Das setzt voraus, dass TXD zum IC-Pin 4 (DI) geht und RXD zum Pin 1 (RO). Ist es umgekehrt, tausche ich die beiden Drähte an TXD und RXD.

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
