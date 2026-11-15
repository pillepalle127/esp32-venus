#include "sc16.h"

#include <SPI.h>

#include "log.h"
#include "pins.h"

// Register (Datenblatt NXP SC16IS752, Tabelle der Register)
#define REG_LCR 0x03
#define REG_LSR 0x05
#define REG_SPR 0x07

// Eigener SPI-Bus (SPI2). Der CMT2300A belegt SPI3.
static SPIClass spi(FSPI);
static const SPISettings SET(1000000, MSBFIRST, SPI_MODE0);

void sc16_begin()
{
    pinMode(SC16_CS, OUTPUT);
    digitalWrite(SC16_CS, HIGH);
    spi.begin(SC16_SCK, SC16_MISO, SC16_MOSI, -1);
}

// Erstes Byte: Bit 7 lesen, Bit 6-3 Register, Bit 2-1 Kanal
static uint8_t xfer(bool rd, uint8_t ch, uint8_t reg, uint8_t v)
{
    spi.beginTransaction(SET);
    digitalWrite(SC16_CS, LOW);
    spi.transfer((rd ? 0x80 : 0x00) | (reg & 0x0F) << 3 | (ch & 0x03) << 1);
    uint8_t r = spi.transfer(v);
    digitalWrite(SC16_CS, HIGH);
    spi.endTransaction();
    return r;
}

uint8_t sc16_read(uint8_t ch, uint8_t reg)
{
    return xfer(true, ch, reg, 0xFF);
}

void sc16_write(uint8_t ch, uint8_t reg, uint8_t v)
{
    xfer(false, ch, reg, v);
}

void sc16_selftest(String &o)
{
    static const uint8_t pat[] = {0x55, 0xAA, 0x00, 0xFF};
    static const char *names[] = {"A", "B"};
    char b[64];
    int good = 0, total = 0;
    o += "{\"ch\":[";
    for (int ch = 0; ch < 2; ch++) {
        snprintf(b, sizeof(b), "%s{\"name\":\"%s\",\"spr\":[", ch ? "," : "", names[ch]);
        o += b;
        for (unsigned i = 0; i < sizeof(pat); i++) {
            sc16_write(ch, REG_SPR, pat[i]);
            uint8_t r = sc16_read(ch, REG_SPR);
            snprintf(b, sizeof(b), "%s[%u,%u]", i ? "," : "", pat[i], r);
            o += b;
            good += r == pat[i];
            total++;
        }
        uint8_t lsr = sc16_read(ch, REG_LSR);
        uint8_t lcr = sc16_read(ch, REG_LCR);
        snprintf(b, sizeof(b), "],\"lsr\":%u,\"lcr\":%u}", lsr, lcr);
        o += b;
        logf("SC16IS752 Kanal %s: LSR %02X, LCR %02X", names[ch], lsr, lcr);
    }
    // Gegenprobe Kanalwahl: verschiedene Werte schreiben, erst dann beide lesen
    sc16_write(SC16_CH_A, REG_SPR, 0x5A);
    sc16_write(SC16_CH_B, REG_SPR, 0xA5);
    uint8_t ra = sc16_read(SC16_CH_A, REG_SPR), rb = sc16_read(SC16_CH_B, REG_SPR);
    good += (ra == 0x5A) + (rb == 0xA5);
    total += 2;
    logf("SC16IS752 Kanalwahl: A 5A -> %02X, B A5 -> %02X", ra, rb);
    snprintf(b, sizeof(b), "],\"cross\":[%u,%u],\"good\":%d,\"total\":%d}", ra, rb, good, total);
    o += b;
    logf("SC16IS752 SPI-Test: %d von %d Werten richtig zurueck", good, total);
}
