#include "monitor.h"

#include <driver/twai.h>

#include "util.h"

void SerialMonitor::begin(HardwareSerial &port, int rx, int tx, uint32_t b)
{
    ser = &port;
    baud = b;
    ser->setRxBufferSize(2048);
    ser->begin(baud, SERIAL_8N1, rx, tx);
    tx_ok = tx >= 0;
    // Rahmenende nach 3,5 Zeichen Pause wie bei Modbus RTU, mindestens 2 ms
    gap_us = (uint32_t)(35ULL * 1000000ULL / baud);
    if (gap_us < 2000) {
        gap_us = 2000;
    }
    cur.len = 0;
}

void SerialMonitor::close_frame()
{
    if (cur.len == 0) {
        return;
    }
    ring[count % MON_FRAMES] = cur;
    count++;
    frames++;
    cur.len = 0;
}

void SerialMonitor::burst(uint32_t ms, uint8_t value)
{
    if (ser == nullptr || !tx_ok) {
        return;
    }
    close_frame();
    burst_value = value;
    burst_until = millis() + ms;
}

void SerialMonitor::poll()
{
    if (ser == nullptr) {
        return;
    }
    if (burst_until) {
        if ((int32_t)(millis() - burst_until) >= 0) {
            burst_until = 0;
        } else {
            uint8_t block[64];
            memset(block, burst_value, sizeof(block));
            int room = ser->availableForWrite();
            if (room > 0) {
                size_t n = room < (int)sizeof(block) ? room : sizeof(block);
                ser->write(block, n);
                bytes_tx += n;
            }
        }
    }
    uint32_t now = micros();
    if (cur.len > 0 && now - last_byte_us > gap_us) {
        close_frame();
    }
    while (ser->available()) {
        if (cur.len == 0) {
            cur.ms = millis();
            cur.tx = false;
        }
        uint8_t b = ser->read();
        if (cap_on && cap_n < sizeof(cap)) {
            cap[cap_n++] = b;
        }
        if (cur.len < MON_FRAME_MAX) {
            cur.d[cur.len] = b;
        }
        cur.len++;
        bytes_rx++;
        last_byte_us = micros();
    }
}

void SerialMonitor::capture(bool on)
{
    cap_on = on;
    if (on) {
        cap_n = 0;
    }
}

size_t SerialMonitor::captured(uint8_t *buf, size_t max)
{
    size_t n = cap_n < max ? cap_n : max;
    memcpy(buf, cap, n);
    return n;
}

void SerialMonitor::send(const uint8_t *d, size_t n)
{
    if (ser == nullptr || n == 0 || !tx_ok) {
        return;
    }
    close_frame();
    Frame f;
    f.ms = millis();
    f.tx = true;
    f.len = (uint16_t)n;
    memcpy(f.d, d, n < MON_FRAME_MAX ? n : MON_FRAME_MAX);
    ring[count % MON_FRAMES] = f;
    count++;
    ser->write(d, n);
    bytes_tx += n;
}

void SerialMonitor::json(String &out)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"baud\":%lu,\"rx\":%lu,\"tx\":%lu,\"frames\":%lu,\"txok\":%s,\"pins\":",
             (unsigned long)baud, (unsigned long)bytes_rx, (unsigned long)bytes_tx, (unsigned long)frames,
             tx_ok ? "true" : "false");
    out += buf;
    json_str(out, pins);
    out += ",\"list\":[";
    uint32_t first = count > MON_FRAMES ? count - MON_FRAMES : 0;
    for (uint32_t i = count; i > first; i--) {
        const Frame &f = ring[(i - 1) % MON_FRAMES];
        snprintf(buf, sizeof(buf), "%s{\"ms\":%lu,\"len\":%u,\"tx\":%s,\"d\":", i == count ? "" : ",",
                 (unsigned long)f.ms, f.len, f.tx ? "true" : "false");
        out += buf;
        json_hex(out, f.d, f.len < MON_FRAME_MAX ? f.len : MON_FRAME_MAX);
        out += '}';
    }
    out += "]}";
}

void CanMonitor::begin()
{
    mtx = xSemaphoreCreateMutex();
}

void CanMonitor::add(uint32_t id, bool ext, uint8_t dlc, const uint8_t *d)
{
    frames = frames + 1;
    xSemaphoreTake(mtx, portMAX_DELAY);
    Entry *e = nullptr;
    for (uint8_t i = 0; i < n; i++) {
        if (tab[i].id == id && tab[i].ext == ext) {
            e = &tab[i];
            break;
        }
    }
    if (e == nullptr) {
        if (n < CAN_IDS) {
            e = &tab[n++];
            e->id = id;
            e->ext = ext;
            e->count = 0;
        } else {
            dropped++;
        }
    }
    if (e != nullptr) {
        e->dlc = dlc > 8 ? 8 : dlc;
        memcpy(e->d, d, e->dlc);
        e->count++;
        e->last_ms = millis();
    }
    xSemaphoreGive(mtx);
}

void CanMonitor::clear()
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    n = 0;
    dropped = 0;
    xSemaphoreGive(mtx);
}

bool CanMonitor::get(uint32_t id, uint8_t *d, uint32_t &age_ms)
{
    if (mtx == nullptr) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < n; i++) {
        if (tab[i].id == id && !tab[i].ext) {
            memcpy(d, tab[i].d, tab[i].dlc);
            age_ms = millis() - tab[i].last_ms;
            ok = true;
            break;
        }
    }
    xSemaphoreGive(mtx);
    return ok;
}

void CanMonitor::json(String &out)
{
    twai_status_info_t st = {};
    bool have = twai_get_status_info(&st) == ESP_OK;
    static const char *states[] = {"gestoppt", "laeuft", "Bus-Off", "Wiederanlauf"};
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"ok\":%s,\"state\":\"%s\",\"frames\":%lu,\"rx_err\":%lu,\"bus_err\":%lu,\"rx_missed\":%lu,"
             "\"rx_overrun\":%lu,\"arb_lost\":%lu,\"dropped\":%lu,\"now\":%lu,\"ids\":[",
             have ? "true" : "false", have && st.state < 4 ? states[st.state] : "-", (unsigned long)frames,
             (unsigned long)st.rx_error_counter, (unsigned long)st.bus_error_count, (unsigned long)st.rx_missed_count,
             (unsigned long)st.rx_overrun_count, (unsigned long)st.arb_lost_count, (unsigned long)dropped,
             (unsigned long)millis());
    out += buf;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < n; i++) {
        const Entry &e = tab[i];
        snprintf(buf, sizeof(buf), "%s{\"id\":%lu,\"ext\":%s,\"n\":%lu,\"ms\":%lu,\"d\":", i ? "," : "",
                 (unsigned long)e.id, e.ext ? "true" : "false", (unsigned long)e.count, (unsigned long)e.last_ms);
        out += buf;
        json_hex(out, e.d, e.dlc);
        out += '}';
    }
    xSemaphoreGive(mtx);
    out += "]}";
}
