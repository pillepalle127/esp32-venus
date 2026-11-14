// SML-Leser fuer die MT681: sucht Start- und Endsequenz, prueft die CRC und
// liest 1.8.0 (Bezug), 2.8.0 (Einspeisung) und 16.7.0 (Leistung).
#pragma once

#include <stddef.h>
#include <stdint.h>

class SmlReader {
public:
    void feed(const uint8_t *data, size_t len);

    // Zaehler, nur von der Task geschrieben, die feed() aufruft
    volatile uint32_t bytes = 0;
    volatile uint32_t telegrams = 0;     // CRC in Ordnung
    volatile uint32_t crc_errors = 0;
    volatile uint32_t crc_swapped = 0;   // CRC stimmt nur mit vertauschten Bytes
    volatile uint32_t overflows = 0;
    volatile uint32_t last_ms = 0;       // letztes gueltiges Telegramm
    volatile uint16_t last_len = 0;

    // Werte aus dem letzten gueltigen Telegramm; has_* sagt, ob sie darin standen
    volatile double import_wh = 0;
    volatile double export_wh = 0;
    volatile double power_w = 0;
    volatile bool has_import = false;
    volatile bool has_export = false;
    volatile bool has_power = false;

private:
    void put(uint8_t b);
    void process();

    uint8_t buf[1024];
    size_t len = 0;
    bool in_msg = false;
    uint64_t window = 0;
};

// Fuer Tests auf dem PC: CRC-16/X-25 wie in SML
uint16_t sml_crc16(const uint8_t *d, size_t n);
