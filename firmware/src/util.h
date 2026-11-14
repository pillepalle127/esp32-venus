// Kleine Helfer fuer JSON und Hex.
#pragma once

#include <Arduino.h>

// Haengt `s` als JSON-String mit Anfuehrungszeichen an.
static inline void json_str(String &out, const char *s)
{
    out += '"';
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20 || c == 0x7f) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
        } else {
            out += (char)c;
        }
    }
    out += '"';
}

static inline void json_hex(String &out, const uint8_t *d, size_t n)
{
    static const char digits[] = "0123456789ABCDEF";
    out += '"';
    for (size_t i = 0; i < n; i++) {
        out += digits[d[i] >> 4];
        out += digits[d[i] & 0x0f];
    }
    out += '"';
}

static inline void json_num(String &out, const char *key, double v, int decimals)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "\"%s\":%.*f", key, decimals, v);
    out += buf;
}

static inline void json_int(String &out, const char *key, long long v)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "\"%s\":%lld", key, v);
    out += buf;
}
