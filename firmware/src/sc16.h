// SC16IS752 (CJMCU-752) per SPI: zwei UARTs fuer die RS485 zu den Packs.
// Pack 1 an UART B, Pack 2 an UART A. Eine eigene Task leert alle 1 ms die
// Empfangspuffer (64 Byte, bei 115200 Baud in 5,5 ms voll).
#pragma once

#include <Arduino.h>

#define SC16_CH_A 0     // Pack 2
#define SC16_CH_B 1     // Pack 1

void sc16_begin(int core);
bool sc16_ok();                                 // Chip antwortet auf SPI
uint8_t sc16_read(uint8_t ch, uint8_t reg);
void sc16_write(uint8_t ch, uint8_t reg, uint8_t v);
bool sc16_set_baud(uint8_t ch, uint32_t baud);  // nur Teiler ohne Rest, gespeichert
bool sc16_send(uint8_t ch, const uint8_t *d, size_t n);
void sc16_json(String &out);
// Kreuztest: empfangene Bytes zusaetzlich mitschreiben
void sc16_capture(uint8_t ch, bool on);
size_t sc16_captured(uint8_t ch, uint8_t *buf, size_t max);
uint32_t sc16_baud(uint8_t ch);
// Interne Schleife (MCR Bit 4): TX intern auf RX, nach aussen bleibt TX auf 1.
// Prueft Quarz, Baudrate und FIFO des Chips ohne die Kette dahinter.
void sc16_loopback(uint8_t ch, String &out);
// 10 s ununterbrochen `value` senden, zum Messen an ADuM und Klemmen
bool sc16_burst(uint8_t ch, uint8_t value);
// Testplan A6b: Scratchpad beider Kanaele schreiben und lesen, Line-Status lesen
void sc16_selftest(String &out);
