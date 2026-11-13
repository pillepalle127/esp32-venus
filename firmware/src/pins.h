// Pinbelegung, siehe README und docs/layout_lochraster.drawio. Verdrahtet und benutzt
// sind bisher nur VE.Bus RX/TX. GPIO 19/20 sind USB, 33-37 PSRAM, 43/44 die Konsole auf
// UART0. Die Strapping-Pins 0, 45, 46 bleiben frei, GPIO 3 traegt nur Relais 2.
// Frei: GPIO 38, 48 (auf 48 sitzt oft die RGB-LED des Boards).
#pragma once

// ADuM1201 neben den S3-Pins, GPIO 4 und 5 laufen gerade hinueber, siehe docs/layout_lochraster.md
#define VEBUS_RX    5
#define VEBUS_TX    4
#define VEBUS_DE    -1      // MAX13487 schaltet die Richtung selbst

// Beide Zaehler am selben Pfostenstecker: GND, 3V3, Haus, WP
#define MT681_RX    7
#define MT681_WP_RX 15      // per RMT, die UARTs sind belegt

#define CAN_TX      8
#define CAN_RX      16

// CMT2300A oben am S3 angeloetet: GPIO 9-14 liegen nebeneinander, dazu 3V3 und GND
#define CMT_GPIO2   9
#define CMT_CS      10
#define CMT_SDIO    11
#define CMT_CLK     12
#define CMT_FCS     13
#define CMT_GPIO3   14

// Kellerlueftung: zwei BME280 (0x76, 0x77) an je einem Pfostenstecker, Luefter-Relais
#define I2C_SDA     18
#define I2C_SCL     17
#define FAN_RELAY   6
#define RELAY2      3       // Reserve, braucht einen Pull-down an der Ansteuerung

// E-Paper, optional
#define EPD_SCK     39
#define EPD_MOSI    40
#define EPD_CS      41
#define EPD_DC      42
#define EPD_RST     2
#define EPD_BUSY    1

// JK-BMS ueber RS485, nur Zellspannungen. UART0 gibt dafuer im Betrieb die Konsole
// ab, Flashen ueber COM (GPIO 43/44) bleibt davon unberuehrt.
#define JK_RS485_RX 47
#define JK_RS485_TX 21
#define JK_RS485_DE -1      // MAX13487 schaltet die Richtung selbst

// Ports am CH348: 0-5 MPPT, 6 SmartShunt, 7 frei fuer einen weiteren MPPT
