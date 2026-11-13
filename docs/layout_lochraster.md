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
┌ ADuM, MAX13487  -- TX -----|  4          1 |----- BUSY --┐ E-Paper
└ VE.Bus          -- RX -----|  5          2 |----- RST ---┤ optional
  Lüfter          -- OUT ----|  6         42 |----- DC ----┤
┌ Stecker MT681   -- Haus ---|  7         41 |----- CS ----┤
└                 -- WP -----| 15         40 |----- MOSI --┤
  CAN             -- RX -----| 16         39 |----- SCK ---┘
┌ Stecker BME280  -- SCL ----| 17         38 |  frei
└                 -- SDA ----| 18         37 |  belegt
  CAN             -- TX -----|  8         36 |  belegt
  Relais 2        -- OUT ----|  3         35 |  belegt
                     frei    | 46          0 |  frei
┌ CMT2300A        -- GPIO2 --|  9         45 |  frei
│ oben am S3      -- CS -----| 10         48 |  frei
│ angelötet       -- SDIO ---| 11         47 |----- RX ----┐ ADuM, MAX13487
│                 -- CLK ----| 12         21 |----- TX ----┘ RS485 JK-BMS
│                 -- FCS ----| 13         20 |  USB, frei
└                 -- GPIO3 --| 14         19 |  USB, frei
  5 V Einspeisung -----------| 5V        GND |--------------- GND-Schiene
  GND-Schiene, CMT2300A -----| GND       GND |--------------- GND-Schiene
                             +---------------+
                               USB-C   USB-C
                       nativ: CH348    COM: Flashen
```

Der CH348 hängt am USB-Kabel. Der CMT2300A ist oben am S3 angelötet, an GPIO 9–14, am zweiten 3V3-Pin und am untersten GND-Pin. Auf der Platine hat er keinen Draht.

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
| Rechts | E-Paper, MAX13487 JK-BMS außen | ADuM und B0505S JK-BMS | frei |

Die Klemmen beider MAX13487-Module zeigen zur Oberkante, der CAN-Stecker liegt an der Unterkante.

Der ADuM1201 für VE.Bus sitzt oben neben den S3-Pins, oberhalb der Pfostenstecker. GPIO 4 und 5 laufen gerade hinüber. Die vier Drähte zum MAX13487 laufen unter dem Modul und kreuzen sich nicht:

| S3-Seite | Busseite | MAX13487 |
|---|---|---|
| U2 an 3V3 | U1 | VCC |
| A1 an GPIO 4 (TX) | A0 | TXD |
| B0 an GPIO 5 (RX) | B1 | RXD |
| G2 an GND | G1 | GND |

Das setzt voraus, dass TXD zum IC-Pin 4 (DI) geht und RXD zum Pin 1 (RO). Ist es umgekehrt, tausche ich die beiden Drähte an TXD und RXD.

Der B0505S steht unten neben dem Modul: +Vo an VCC, 0V am GND-Draht, Vin und GND kommen von rechts.

Die Bestückung an GPIO 6 und GPIO 3 steht nicht im Plan, die mache ich anders. GPIO 3 ist beim Start noch nicht aktiv, die Ansteuerung muss das abfangen.
