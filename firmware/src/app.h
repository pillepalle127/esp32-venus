// Gemeinsame Objekte der Firmware, damit die Webseite sie lesen kann.
#pragma once

#include <Arduino.h>
#include <VEBus.h>
#include <ch348.h>

#include "bme280.h"
#include "monitor.h"
#include "rmt_uart.h"
#include "sml.h"
#include "vedirect.h"

// Linker Kanal (GPIO 4/5): VE.Bus nur hoeren oder RS485-Test
enum LeftMode : uint8_t { LEFT_VEBUS = 0, LEFT_RS485 = 1 };

struct Relay {
    int gpio;
    const char *name;
    int8_t mode;        // -1 nicht getrieben, 0 low, 1 high
};

extern SmlReader sml_house, sml_wp;
extern RmtUart rmt_wp;
extern Bme280 bme[2];
extern VeDirectParser ved[CH348_NUM_PORTS];
extern SemaphoreHandle_t ved_mtx;
extern volatile bool ch348_running;
extern CanMonitor can_mon;
extern bool can_ok;
extern VEBus vebus;
extern SerialMonitor rs_left;
extern LeftMode left_mode;
extern Relay relays[2];
extern const char BUILD[];   // Datum und Uhrzeit des Builds aus main.cpp

void relay_set(int i, int mode);
void left_mode_save(LeftMode mode, uint32_t baud);
int i2c_scan(uint8_t *found, int max);
void bme_init();
void can_selftest(String &out);

void web_begin();
void web_loop();
