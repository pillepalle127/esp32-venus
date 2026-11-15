// SC16IS752 (CJMCU-752) per SPI: zwei UARTs fuer die RS485 zu den Packs.
// Pack 1 an UART B, Pack 2 an UART A. Bisher nur Registerzugriff und SPI-Test.
#pragma once

#include <Arduino.h>

#define SC16_CH_A 0
#define SC16_CH_B 1

void sc16_begin();
uint8_t sc16_read(uint8_t ch, uint8_t reg);
void sc16_write(uint8_t ch, uint8_t reg, uint8_t v);
// Testplan A6b: Scratchpad beider Kanaele schreiben und lesen, Line-Status lesen
void sc16_selftest(String &out);
