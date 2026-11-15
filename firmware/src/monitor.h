// Mitschnitt fuer Testzwecke: RS485-Rahmen (getrennt durch Pausen) und CAN-Telegramme.
#pragma once

#include <Arduino.h>

#define MON_FRAMES    16
#define MON_FRAME_MAX 96

class SerialMonitor {
public:
    void begin(HardwareSerial &port, int rx, int tx, uint32_t baud);
    void poll();                          // regelmaessig aus derselben Task aufrufen
    void send(const uint8_t *d, size_t n);
    // Sendet `ms` lang ununterbrochen `value`, zum Messen am Bus (0x00 Multimeter, 0x55 Oszilloskop)
    void burst(uint32_t ms, uint8_t value);
    uint32_t burst_until = 0;
    uint8_t burst_value = 0;
    void json(String &out);
    // Kreuztest: empfangene Bytes zusaetzlich mitschreiben
    void capture(bool on);
    size_t captured(uint8_t *buf, size_t max);

    uint32_t baud = 0;
    uint32_t bytes_rx = 0, bytes_tx = 0, frames = 0;
    bool tx_ok = false;      // TX-Pin belegt
    char pins[96] = "";      // Ergebnis der Pinpruefung

private:
    struct Frame {
        uint32_t ms;
        uint16_t len;       // tatsaechliche Laenge, gespeichert hoechstens MON_FRAME_MAX
        bool tx;
        uint8_t d[MON_FRAME_MAX];
    };
    void close_frame();

    HardwareSerial *ser = nullptr;
    Frame ring[MON_FRAMES];
    uint32_t count = 0;
    Frame cur;
    uint32_t last_byte_us = 0;
    uint32_t gap_us = 0;
    bool cap_on = false;
    uint8_t cap[64];
    size_t cap_n = 0;
};

#define CAN_IDS 48

class CanMonitor {
public:
    void begin();
    void add(uint32_t id, bool ext, uint8_t dlc, const uint8_t *d);
    void clear();
    void json(String &out);
    // Letzte Daten einer ID. false, wenn sie nie kam. age_ms: Alter
    bool get(uint32_t id, uint8_t *d, uint32_t &age_ms);

    volatile uint32_t frames = 0;

private:
    struct Entry {
        uint32_t id;
        bool ext;
        uint8_t dlc;
        uint8_t d[8];
        uint32_t count;
        uint32_t last_ms;
    };
    Entry tab[CAN_IDS];
    uint8_t n = 0;
    uint32_t dropped = 0;
    SemaphoreHandle_t mtx = nullptr;
};
