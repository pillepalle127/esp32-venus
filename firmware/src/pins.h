// Pinbelegung, siehe README und docs/layout_lochraster.drawio. Verdrahtet und benutzt
// sind bisher nur VE.Bus RX/TX. GPIO 19/20 sind USB, 33-37 PSRAM, 43/44 die Konsole auf
// UART0. Die Strapping-Pins 0 und 45 bleiben frei. GPIO 3 und 46 tragen nur Signale des
// CMT2300A, die beim Start niemand treibt.
// Frei: GPIO 1, 17, 21, 39, 40, 47.
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

// CMT2300A (Modul 10298-V1.0) per Pigtail oben am S3 angeloetet: sechs Pins nebeneinander von
// GPIO 18 bis 10, dazu VCC an 3V3 und GND. Namen wie auf dem Modul, GPIO1 des Moduls bleibt frei.
// GPIO 46 muss beim Reset low sein, sonst gibt es keinen Download-Modus. Der interne
// Pull-down haelt ihn low, SCLK ist am CMT2300A ein Eingang.
#define CMT_GPIO2   18
#define CMT_CSB     8
#define CMT_SDIO    3
#define CMT_SCLK    46
#define CMT_FCSB    9
#define CMT_GPIO3   10

// Relais nebeneinander, Bestueckung nicht im Lochraster-Plan
#define FAN_RELAY   11
#define RELAY2      12      // Reserve

// TJA1050 unten links, ueber ADuM1201 und B0505S isoliert
// So verdrahtet (14.11.2026): RX an GPIO 13 (ADuM A0), TX an GPIO 14 (ADuM B1)
#define CAN_TX      14
#define CAN_RX      13

// JK-BMS ueber RS485, nur Zellspannungen: SC16IS752 (CJMCU-752) per SPI, Pack 1 an UART B,
// Pack 2 an UART A (kuerzere Wege im Plan), dahinter je ADuM1201, B0505S und MAX13487. Die SPI-Pins liegen in denselben
// Reihen wie die Pins des Moduls, die Draehte gehen gerade hinueber. UART0 bleibt Konsole.
#define SC16_CS     2
#define SC16_MOSI   42      // SI
#define SC16_MISO   41      // SO
#define SC16_SCK    38      // SCLK

// E-Paper: Stiftleiste im Plan entfallen. Frei sind noch GPIO 1, 17, 21, 39, 40, 47.

// RGB-LED des Boards (WS2812), ueber die Loetbruecke "RGB" an GPIO 48
#define RGB_LED     48

// Ports am CH348: 0-5 MPPT, 6 SmartShunt, 7 frei fuer einen weiteren MPPT
