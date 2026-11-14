#include "sml.h"

#include <math.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

static const uint64_t SML_START = 0x1B1B1B1B01010101ULL;

uint16_t sml_crc16(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
        }
    }
    return crc ^ 0xFFFF;
}

void SmlReader::feed(const uint8_t *data, size_t n)
{
    bytes = bytes + n;
    for (size_t i = 0; i < n; i++) {
        put(data[i]);
    }
}

void SmlReader::put(uint8_t b)
{
    window = (window << 8) | b;
    if (window == SML_START) {
        // Neue Nachricht, auch wenn die vorige kein Ende hatte
        for (int i = 0; i < 8; i++) {
            buf[i] = (uint8_t)(SML_START >> (56 - 8 * i));
        }
        len = 8;
        in_msg = true;
        return;
    }
    if (!in_msg) {
        return;
    }
    if (len >= sizeof(buf)) {
        overflows = overflows + 1;
        in_msg = false;
        return;
    }
    buf[len++] = b;
    // Ende: 1B 1B 1B 1B 1A, Fuellbytes, 2 Bytes CRC
    if (len >= 16 && buf[len - 4] == 0x1A && buf[len - 5] == 0x1B && buf[len - 6] == 0x1B &&
        buf[len - 7] == 0x1B && buf[len - 8] == 0x1B) {
        in_msg = false;
        process();
    }
}

// Typ-Laenge-Feld lesen. Bei Listen ist `n` die Zahl der Elemente, sonst die
// Zahl der Datenbytes ohne das Typ-Laenge-Feld.
static bool read_tl(const uint8_t *b, size_t end, size_t &p, uint8_t &type, size_t &n)
{
    if (p >= end) {
        return false;
    }
    uint8_t c = b[p];
    type = (c >> 4) & 0x07;
    size_t l = c & 0x0F;
    size_t tl_bytes = 1;
    while (c & 0x80) {
        if (p + tl_bytes >= end) {
            return false;
        }
        c = b[p + tl_bytes];
        l = (l << 4) | (c & 0x0F);
        tl_bytes++;
    }
    p += tl_bytes;
    if (type == 7) {
        n = l;
    } else {
        if (l < tl_bytes) {
            return false;
        }
        n = l - tl_bytes;
    }
    return true;
}

static bool skip(const uint8_t *b, size_t end, size_t &p, int depth = 0)
{
    uint8_t type;
    size_t n;
    if (depth > 8 || !read_tl(b, end, p, type, n)) {
        return false;
    }
    if (type == 7) {
        for (size_t i = 0; i < n; i++) {
            if (!skip(b, end, p, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    p += n;
    return p <= end;
}

// Ganzzahl lesen (Typ 5 mit Vorzeichen, Typ 6 ohne). `present` ist false bei einem
// leeren optionalen Feld (01).
static bool read_int(const uint8_t *b, size_t end, size_t &p, int64_t &v, bool &present)
{
    uint8_t type;
    size_t n;
    if (!read_tl(b, end, p, type, n)) {
        return false;
    }
    if (type == 0 && n == 0) {
        present = false;
        return true;
    }
    if ((type != 5 && type != 6) || n == 0 || n > 8 || p + n > end) {
        return false;
    }
    uint64_t u = 0;
    for (size_t i = 0; i < n; i++) {
        u = (u << 8) | b[p + i];
    }
    if (type == 5 && n < 8 && (b[p] & 0x80)) {
        u |= ~0ULL << (8 * n);
    }
    p += n;
    v = (int64_t)u;
    present = true;
    return true;
}

// Ein SML_ListEntry ab dem Wert nach objName: status, valTime, unit, scaler, value
static bool read_entry(const uint8_t *b, size_t end, size_t p, double &out)
{
    int64_t unit, scaler, value;
    bool has_unit, has_scaler, has_value;
    if (!skip(b, end, p) || !skip(b, end, p)) {   // status, valTime
        return false;
    }
    if (!read_int(b, end, p, unit, has_unit) || !read_int(b, end, p, scaler, has_scaler) ||
        !read_int(b, end, p, value, has_value) || !has_value) {
        return false;
    }
    out = (double)value * pow(10.0, has_scaler ? (double)scaler : 0.0);
    return true;
}

void SmlReader::process()
{
    uint16_t crc = sml_crc16(buf, len - 2);
    bool ok = buf[len - 2] == (crc & 0xFF) && buf[len - 1] == (crc >> 8);
    if (!ok) {
        if (buf[len - 2] == (crc >> 8) && buf[len - 1] == (crc & 0xFF)) {
            crc_swapped = crc_swapped + 1;
        } else {
            crc_errors = crc_errors + 1;
            return;
        }
    }

    static const uint8_t OBIS_IMPORT[6] = {0x01, 0x00, 0x01, 0x08, 0x00, 0xFF};
    static const uint8_t OBIS_EXPORT[6] = {0x01, 0x00, 0x02, 0x08, 0x00, 0xFF};
    static const uint8_t OBIS_POWER[6] = {0x01, 0x00, 0x10, 0x07, 0x00, 0xFF};
    bool imp = false, exp = false, pwr = false;
    double v;
    size_t end = len - 8;
    for (size_t i = 0; i + 8 < end; i++) {
        if (buf[i] != 0x77 || buf[i + 1] != 0x07) {
            continue;
        }
        const uint8_t *obis = buf + i + 2;
        if (memcmp(obis, OBIS_IMPORT, 6) == 0 && read_entry(buf, end, i + 8, v)) {
            import_wh = v;
            imp = true;
        } else if (memcmp(obis, OBIS_EXPORT, 6) == 0 && read_entry(buf, end, i + 8, v)) {
            export_wh = v;
            exp = true;
        } else if (memcmp(obis, OBIS_POWER, 6) == 0 && read_entry(buf, end, i + 8, v)) {
            power_w = v;
            pwr = true;
        }
    }
    has_import = imp;
    has_export = exp;
    has_power = pwr;
    last_len = (uint16_t)len;
    last_ms = millis();
    telegrams = telegrams + 1;
}
