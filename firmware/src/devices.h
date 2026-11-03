// Geraete an VE.Direct, erkannt an der Seriennummer (Feld SER#). Die Instanzen
// sind die von Venus OS, damit Themen und History in ioBroker gleich bleiben.
#pragma once

#include <stdint.h>

typedef struct {
    const char *serial;
    const char *service;
    uint16_t instance;
} vedirect_device_t;

static const vedirect_device_t VEDIRECT_DEVICES[] = {
    {"HQ2440T6X3F", "solarcharger", 288},
    {"HQ24444FA7P", "solarcharger", 291},
    {"HQ2444X92KD", "solarcharger", 292},
    {"HQ250332J3K", "solarcharger", 293},
    {"HQ2503KHTNV", "solarcharger", 294},
    {"HQ2503PZF6F", "solarcharger", 296},
    {"HQ2512RP9C2", "battery", 291},
};
