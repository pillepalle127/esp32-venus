#include "vedirect.h"

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

// Aufbau wie im Beispiel aus Victrons VE.Direct-Protokoll: Die Pruefsumme laeuft
// ueber alle Bytes eines Blocks ausser den Hex-Meldungen, einschliesslich des
// Pruefsummenbytes, und muss 0 ergeben.
void VeDirectParser::feed(const uint8_t *data, size_t len)
{
    bytes += len;
    for (size_t i = 0; i < len; i++) {
        put(data[i]);
    }
}

void VeDirectParser::put(uint8_t b)
{
    if (b == ':' && state != S_CHECKSUM && state != S_HEX) {
        before_hex = state;
        state = S_HEX;
    }
    if (state != S_HEX) {
        sum += b;
    }
    switch (state) {
    case S_IDLE:
        if (b == '\n') {
            state = S_NAME;
            pos = 0;
        }
        break;
    case S_NAME: {
        VedField &f = tmp[tmp_count];
        if (b == '\t') {
            f.name[pos] = '\0';
            state = strcmp(f.name, "Checksum") == 0 ? S_CHECKSUM : S_VALUE;
            pos = 0;
        } else if (b == '\r' || b == '\n') {
            pos = 0;        // leere Zeile
        } else if (pos < VED_NAME_LEN - 1) {
            f.name[pos++] = (char)b;
        }
        break;
    }
    case S_VALUE: {
        VedField &f = tmp[tmp_count];
        if (b == '\n') {
            f.value[pos] = '\0';
            if (tmp_count < VED_MAX_FIELDS - 1) {
                tmp_count++;
            } else {
                overflow = true;    // letztes Feld wird ueberschrieben, Block verworfen
            }
            state = S_NAME;
            pos = 0;
        } else if (b != '\r' && pos < VED_VALUE_LEN - 1) {
            f.value[pos++] = (char)b;
        }
        break;
    }
    case S_CHECKSUM:
        if (sum == 0 && !overflow) {
            memcpy(fields, tmp, sizeof(VedField) * tmp_count);
            num_fields = tmp_count;
            blocks_ok++;
            last_ok_ms = millis();
        } else {
            blocks_bad++;
        }
        sum = 0;
        tmp_count = 0;
        overflow = false;
        state = S_IDLE;
        break;
    case S_HEX:
        if (b == '\n') {
            state = before_hex;
        }
        break;
    }
}

const char *VeDirectParser::get(const char *name) const
{
    for (uint8_t i = 0; i < num_fields; i++) {
        if (strcmp(fields[i].name, name) == 0) {
            return fields[i].value;
        }
    }
    return nullptr;
}
