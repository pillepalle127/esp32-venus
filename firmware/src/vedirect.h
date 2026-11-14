// VE.Direct Textprotokoll: Bloecke aus "Name<TAB>Wert"-Zeilen mit Pruefsumme.
// Hex-Meldungen (beginnen mit ':') werden uebersprungen.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define VED_MAX_FIELDS 28
#define VED_NAME_LEN   10
#define VED_VALUE_LEN  34

struct VedField {
    char name[VED_NAME_LEN];
    char value[VED_VALUE_LEN];
};

class VeDirectParser {
public:
    void feed(const uint8_t *data, size_t len);

    // Letzter Block mit gueltiger Pruefsumme
    VedField fields[VED_MAX_FIELDS];
    uint8_t num_fields = 0;

    uint32_t bytes = 0;
    uint32_t blocks_ok = 0;
    uint32_t blocks_bad = 0;
    uint32_t last_ok_ms = 0;

    const char *get(const char *name) const;

private:
    enum State { S_IDLE, S_NAME, S_VALUE, S_CHECKSUM, S_HEX };
    void put(uint8_t b);

    State state = S_IDLE;
    State before_hex = S_IDLE;
    uint8_t sum = 0;
    VedField tmp[VED_MAX_FIELDS];
    uint8_t tmp_count = 0;
    uint8_t pos = 0;
    bool overflow = false;
};
