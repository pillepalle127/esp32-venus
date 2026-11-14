// Pinbelegung, siehe README und docs/layout_lochraster.drawio. Verdrahtet und benutzt
// sind bisher nur VE.Bus RX/TX. GPIO 19/20 sind USB, 33-37 PSRAM, 43/44 die Konsole auf
// UART0. Die Strapping-Pins 0 und 45 bleiben frei. GPIO 3 und 46 tragen nur Signale des
// CMT2300A, die beim Start niemand treibt.
// Frei: GPIO 17, 38.
#pragma once

// ADuM1201 neben den S3-Pins, GPIO 4 und 5 laufen gerade hinueber, siehe docs/layout_lochraster.md
#define VEBUS_RX    5
#define VEBUS_TX    4
#define VEBUS_DE    -1      // MAX13487 schaltet die Richtung selbst

// Beide Zaehler am selben Pfostenstecker: GND, 3V3, Haus, WP
// So verdrahtet (14.11.2026): Haus an GPIO 7, WP an GPIO 6
#define MT681_RX    7
#define MT681_WP_RX 6       // per RMT, die UARTs sind belegt

// Kellerlueftung: zwei BME280 (0x76, 0x77) an je einem Pfostenstecker
// So verdrahtet, getestet am 14.11.2026 mit einem BME280 an 0x77
#define I2C_SDA     16
#define I2C_SCL     15

// CMT2300A oben am S3 angeloetet: sechs Pins nebeneinander von GPIO 18 bis 10, dazu 3V3 und GND.
// GPIO 46 muss beim Reset low sein, sonst gibt es keinen Download-Modus. Der interne
// Pull-down haelt ihn low, CLK ist am CMT2300A ein Eingang.
#define CMT_GPIO2   18
#define CMT_CS      8
#define CMT_SDIO    3
#define CMT_CLK     46
#define CMT_FCS     9
#define CMT_GPIO3   10

// Relais nebeneinander, Bestueckung nicht im Lochraster-Plan
#define FAN_RELAY   11
#define RELAY2      12      // Reserve

// TJA1050 unten links, ueber ADuM1201 und B0505S isoliert
// So verdrahtet (14.11.2026): RX an GPIO 13 (ADuM A0), TX an GPIO 14 (ADuM B1)
#define CAN_TX      14
#define CAN_RX      13

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

// RGB-LED des Boards (WS2812), ueber die Loetbruecke "RGB" an GPIO 48
#define RGB_LED     48

// Ports am CH348: 0-5 MPPT, 6 SmartShunt, 7 frei fuer einen weiteren MPPT
