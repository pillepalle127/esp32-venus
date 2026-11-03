// multiplus_sim: Spielt fuer den Timing-Test (PLAN.md, Phase 1, Punkt 4) den
// VE.Bus-Takt des MultiPlus. Laeuft auf einem zweiten ESP32-S3, nur diese eine
// Aufgabe, damit die Messung sauber bleibt.
//
// Sendet alle 20 ms einen Sync-Frame im Format aus der esp32ess-Doku und misst
// von aussen, wann die Antwort des Prueflings beginnt: erste Flanke auf RX,
// bezogen auf den Beginn des Sync-Frames.
//
// Verbindung ueber Kreuz: GPIO 5 hier auf GPIO 4 dort, GPIO 4 hier auf GPIO 5
// dort, dazu GND. Ausgabe ueber den nativen USB-Port.

#include <Arduino.h>

#define PIN_RX      4
#define PIN_TX      5
#define BAUD        256000
#define SYNC_US     20000
#define REPORT_MS   30000
#define MARGIN_US   300     // so lange vor dem naechsten Sync endet die Auswertung

#define NUM_BINS    7
static const char *const BIN_NAMES[NUM_BINS] = {"<8.0", "8.0-8.5", "8.5-9.0", "9.0-9.5", "9.5-10.0", "10.0-11", ">11"};
static const int32_t BIN_LIMITS_US[NUM_BINS - 1] = {8000, 8500, 9000, 9500, 10000, 11000};

typedef struct {
    uint32_t syncs;
    uint32_t replies;
    uint32_t missed;
    uint32_t extra_frames;
    uint32_t bad_checksum;
    uint32_t bad_number;
    uint32_t bad_header;
    uint32_t max_missed_row;
    int32_t min_us;
    int32_t max_us;
    int64_t sum_us;
    uint32_t bins[NUM_BINS];
} stats_t;

static stats_t stats;
static stats_t total;
static portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t first_edge_us;

static void IRAM_ATTR on_rx_edge()
{
    if (first_edge_us == 0) {
        first_edge_us = esp_timer_get_time();
    }
}

static void stats_reset(stats_t *s)
{
    memset(s, 0, sizeof(*s));
    s->min_us = INT32_MAX;
    s->max_us = INT32_MIN;
}

static void stats_add_latency(stats_t *s, int32_t us)
{
    s->replies++;
    s->sum_us += us;
    if (us < s->min_us) s->min_us = us;
    if (us > s->max_us) s->max_us = us;
    int b = 0;
    while (b < NUM_BINS - 1 && us >= BIN_LIMITS_US[b]) b++;
    s->bins[b]++;
}

// Prueft einen vollstaendigen Frame des Prueflings: 98 F7 FE nr ... cs FF.
// Die Summe ab Byte 2 einschliesslich Pruefsumme und FF ergibt modulo 256 null.
static void check_frame(const uint8_t *f, size_t len, uint8_t expected_nr, stats_t *s)
{
    if (len < 6 || f[0] != 0x98 || f[1] != 0xF7 || f[2] != 0xFE) {
        s->bad_header++;
        return;
    }
    uint8_t sum = 0;
    for (size_t i = 2; i < len; i++) {
        sum += f[i];
    }
    if (sum != 0) {
        s->bad_checksum++;
    }
    if (f[3] != expected_nr) {
        s->bad_number++;
    }
}

static size_t build_sync(uint8_t *out, uint8_t nr, uint16_t stamp)
{
    size_t n = 0;
    out[n++] = 0x83;
    out[n++] = 0x83;
    out[n++] = 0xFD;
    out[n++] = nr;
    out[n++] = 0x55;
    out[n++] = 0x51;
    out[n++] = stamp & 0xFF;
    out[n++] = stamp >> 8;
    // Laut Doku zaehlt die 0x55 nicht zur Pruefsumme.
    uint8_t cs = 1 - (uint8_t)(0xFD + nr + 0x51 + (stamp & 0xFF) + (stamp >> 8));
    out[n++] = cs;
    out[n++] = 0xFF;
    return n;
}

static void bus_task(void *)
{
    uint8_t nr = 0;
    uint32_t missed_row = 0;
    uint8_t rx[128];
    size_t rx_len = 0;
    int64_t next_us = esp_timer_get_time() + SYNC_US;

    while (true) {
        while (esp_timer_get_time() < next_us) {
        }
        first_edge_us = 0;
        int64_t t0 = esp_timer_get_time();
        next_us += SYNC_US;

        uint8_t sync[10];
        size_t sync_len = build_sync(sync, nr, (uint16_t)(t0 / 90));
        Serial1.write(sync, sync_len);

        uint32_t frames = 0;
        stats_t local;
        memset(&local, 0, sizeof(local));

        while (esp_timer_get_time() < next_us - MARGIN_US) {
            while (Serial1.available()) {
                uint8_t c = Serial1.read();
                if (rx_len < sizeof(rx)) {
                    rx[rx_len++] = c;
                }
                if (c == 0xFF) {
                    check_frame(rx, rx_len, (nr + 1) & 0x7F, &local);
                    frames++;
                    rx_len = 0;
                }
            }
        }

        int64_t edge = first_edge_us;
        portENTER_CRITICAL(&stats_mux);
        stats.syncs++;
        stats.bad_checksum += local.bad_checksum;
        stats.bad_number += local.bad_number;
        stats.bad_header += local.bad_header;
        if (frames > 1) {
            stats.extra_frames += frames - 1;
        }
        if (edge == 0) {
            stats.missed++;
            missed_row++;
            if (missed_row > stats.max_missed_row) {
                stats.max_missed_row = missed_row;
            }
        } else {
            missed_row = 0;
            stats_add_latency(&stats, (int32_t)(edge - t0));
        }
        portEXIT_CRITICAL(&stats_mux);

        // Die Frame-Nummer zaehlt auch die Antwort mit.
        nr = (nr + (frames > 0 ? 2 : 1)) & 0x7F;
    }
}

static void print_stats(const char *name, const stats_t *s)
{
    Serial.printf("%s: Syncs %lu, Antworten %lu, ohne Antwort %lu (hoechstens %lu am Stueck), zusaetzlich %lu, "
                  "Pruefsumme falsch %lu, Nummer falsch %lu, Kopf falsch %lu\n",
                  name, (unsigned long)s->syncs, (unsigned long)s->replies, (unsigned long)s->missed,
                  (unsigned long)s->max_missed_row, (unsigned long)s->extra_frames,
                  (unsigned long)s->bad_checksum, (unsigned long)s->bad_number, (unsigned long)s->bad_header);
    if (s->replies > 0) {
        Serial.printf("  Beginn der Antwort nach dem Sync: min %.2f ms, Mittel %.2f ms, max %.2f ms |", s->min_us / 1000.0,
                      (double)s->sum_us / s->replies / 1000.0, s->max_us / 1000.0);
        for (int b = 0; b < NUM_BINS; b++) {
            Serial.printf(" %s: %lu", BIN_NAMES[b], (unsigned long)s->bins[b]);
        }
        Serial.println();
    }
}

static void add_stats(stats_t *to, const stats_t *from)
{
    to->syncs += from->syncs;
    to->replies += from->replies;
    to->missed += from->missed;
    to->extra_frames += from->extra_frames;
    to->bad_checksum += from->bad_checksum;
    to->bad_number += from->bad_number;
    to->bad_header += from->bad_header;
    if (from->max_missed_row > to->max_missed_row) to->max_missed_row = from->max_missed_row;
    if (from->min_us < to->min_us) to->min_us = from->min_us;
    if (from->max_us > to->max_us) to->max_us = from->max_us;
    to->sum_us += from->sum_us;
    for (int b = 0; b < NUM_BINS; b++) {
        to->bins[b] += from->bins[b];
    }
}

static void report_task(void *)
{
    stats_reset(&total);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_MS));
        stats_t snap;
        portENTER_CRITICAL(&stats_mux);
        snap = stats;
        stats_reset(&stats);
        portEXIT_CRITICAL(&stats_mux);
        add_stats(&total, &snap);
        Serial.printf("t=%lus\n", (unsigned long)(millis() / 1000));
        print_stats("  letzte 30 s", &snap);
        print_stats("  gesamt", &total);
    }
}

void setup()
{
    Serial.begin(115200);
    stats_reset(&stats);

    // setup() laeuft auf Core 1: UART- und Flanken-Interrupt liegen damit beim Takt.
    Serial1.setRxBufferSize(512);
    Serial1.begin(BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
    attachInterrupt(digitalPinToInterrupt(PIN_RX), on_rx_edge, FALLING);

    xTaskCreatePinnedToCore(report_task, "report", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(bus_task, "bus", 4096, NULL, 10, NULL, 1);
}

void loop()
{
    vTaskDelete(NULL);
}
