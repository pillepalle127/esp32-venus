// Pinbelegung, Vorschlag aus der README. Verdrahtet und benutzt sind bisher nur
// VE.Bus RX/TX. GPIO 19/20 sind USB, 33-37 PSRAM, 43/44 die Konsole auf UART0.
// Die Strapping-Pins 0, 3, 45, 46 bleiben frei.
#pragma once

#define VEBUS_RX    4
#define VEBUS_TX    5
#define VEBUS_DE    6

#define MT681_RX    7
#define MT681_WP_RX 21      // per RMT, die UARTs sind belegt

#define CAN_TX      15
#define CAN_RX      16

#define CMT_SDIO    11
#define CMT_CLK     12
#define CMT_CS      10
#define CMT_FCS     13
#define CMT_GPIO2   9
#define CMT_GPIO3   8

// Kellerlueftung: zwei BME280 (0x76, 0x77), Luefter-Relais
#define I2C_SDA     17
#define I2C_SCL     18
#define FAN_RELAY   14

// E-Paper, optional
#define EPD_SCK     39
#define EPD_MOSI    40
#define EPD_CS      41
#define EPD_DC      42
#define EPD_RST     2
#define EPD_BUSY    1

// Ports am CH348: 0-5 MPPT, 6 SmartShunt, 7 JK-BMS ueber RS485 (nur Zellspannungen)
#define CH348_PORT_JK_RS485 7
