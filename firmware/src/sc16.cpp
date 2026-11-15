#include "sc16.h"

#include <Preferences.h>
#include <SPI.h>

#include "log.h"
#include "pins.h"
#include "util.h"

// Register (Datenblatt NXP SC16IS752)
#define REG_RHR   0x00      // lesen
#define REG_THR   0x00      // schreiben
#define REG_IER   0x01
#define REG_FCR   0x02      // schreiben
#define REG_LCR   0x03
#define REG_MCR   0x04
#define REG_LSR   0x05
#define REG_SPR   0x07
#define REG_TXLVL 0x08
#define REG_RXLVL 0x09
#define REG_EFCR  0x0F
#define REG_DLL   0x00      // mit LCR Bit 7
#define REG_DLH   0x01

#define FRAMES    16
#define FRAME_MAX 96

// Eigener SPI-Bus (SPI2). Der CMT2300A belegt SPI3.
static SPIClass spi(FSPI);
static const SPISettings SET(2000000, MSBFIRST, SPI_MODE0);   // Datenblatt: bis 4 MHz, mit Reserve fuer die Draehte
static SemaphoreHandle_t spi_mtx, data_mtx;
static bool chip_ok;
static Preferences prefs;

struct Frame {
    uint32_t ms;
    uint16_t len;
    bool tx;
    uint8_t d[FRAME_MAX];
};

struct Chan {
    const char *name, *pack;
    uint32_t burst_until = 0;
    uint8_t burst_value = 0;
    uint32_t baud = 115200;
    uint32_t rx = 0, tx = 0, frames = 0;
    uint32_t ovr = 0, par = 0, frm = 0, brk = 0;
    Frame ring[FRAMES];
    uint32_t count = 0;
    Frame cur = {};
    uint32_t last_us = 0;
    bool cap_on = false;
    uint8_t cap[64];
    size_t cap_n = 0;
};
static Chan chans[2];

// Erstes Byte: Bit 7 lesen, Bit 6-3 Register, Bit 2-1 Kanal
static uint8_t cmd(bool rd, uint8_t ch, uint8_t reg)
{
    return (rd ? 0x80 : 0x00) | (reg & 0x0F) << 3 | (ch & 0x03) << 1;
}

static uint8_t xfer(bool rd, uint8_t ch, uint8_t reg, uint8_t v)
{
    xSemaphoreTake(spi_mtx, portMAX_DELAY);
    spi.beginTransaction(SET);
    digitalWrite(SC16_CS, LOW);
    spi.transfer(cmd(rd, ch, reg));
    uint8_t r = spi.transfer(v);
    digitalWrite(SC16_CS, HIGH);
    spi.endTransaction();
    xSemaphoreGive(spi_mtx);
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

// Mehrere Bytes am Stueck aus dem RHR lesen oder ins THR schreiben
static void fifo(bool rd, uint8_t ch, uint8_t *d, size_t n)
{
    xSemaphoreTake(spi_mtx, portMAX_DELAY);
    spi.beginTransaction(SET);
    digitalWrite(SC16_CS, LOW);
    spi.transfer(cmd(rd, ch, rd ? REG_RHR : REG_THR));
    for (size_t i = 0; i < n; i++) {
        uint8_t r = spi.transfer(rd ? 0xFF : d[i]);
        if (rd) {
            d[i] = r;
        }
    }
    digitalWrite(SC16_CS, HIGH);
    spi.endTransaction();
    xSemaphoreGive(spi_mtx);
}

static uint32_t gap_us(uint32_t baud)
{
    // Rahmenende nach 3,5 Zeichen Pause wie bei Modbus RTU, mindestens 2 ms
    uint32_t g = (uint32_t)(35ULL * 1000000ULL / baud);
    return g < 2000 ? 2000 : g;
}

static void close_frame(Chan &c)
{
    if (c.cur.len == 0) {
        return;
    }
    c.ring[c.count % FRAMES] = c.cur;
    c.count++;
    c.frames++;
    c.cur.len = 0;
}

static void setup_uart(uint8_t ch, uint32_t baud)
{
    uint16_t div = SC16_XTAL / 16 / baud;
    sc16_write(ch, REG_LCR, 0x80);          // Teiler-Register frei
    sc16_write(ch, REG_DLL, div & 0xFF);
    sc16_write(ch, REG_DLH, div >> 8);
    sc16_write(ch, REG_LCR, 0x03);          // 8N1
    sc16_write(ch, REG_MCR, 0x00);          // Vorteiler 1
    sc16_write(ch, REG_FCR, 0x07);          // FIFO an, beide leeren
    sc16_write(ch, REG_IER, 0x00);          // abfragen statt Interrupt
    sc16_write(ch, REG_EFCR, 0x00);         // Richtung schaltet der MS2548 selbst
}

static bool valid_baud(uint32_t baud)
{
    return baud >= 300 && baud <= SC16_XTAL / 16 && (SC16_XTAL / 16) % baud == 0;
}

uint32_t sc16_baud(uint8_t ch)
{
    return ch < 2 ? chans[ch].baud : 0;
}

bool sc16_set_baud(uint8_t ch, uint32_t baud)
{
    if (ch > 1 || !valid_baud(baud) || !chip_ok) {
        return false;
    }
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    chans[ch].baud = baud;
    setup_uart(ch, baud);
    xSemaphoreGive(data_mtx);
    prefs.putUInt(ch ? "bB" : "bA", baud);
    logf("SC16IS752 UART %s (%s): %lu Baud", chans[ch].name, chans[ch].pack, (unsigned long)baud);
    return true;
}

bool sc16_send(uint8_t ch, const uint8_t *d, size_t n)
{
    if (ch > 1 || !chip_ok || n == 0) {
        return false;
    }
    Chan &c = chans[ch];
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    close_frame(c);
    Frame f;
    f.ms = millis();
    f.tx = true;
    f.len = n;
    memcpy(f.d, d, n < FRAME_MAX ? n : FRAME_MAX);
    c.ring[c.count % FRAMES] = f;
    c.count++;
    xSemaphoreGive(data_mtx);
    size_t done = 0;
    uint32_t start = millis();
    while (done < n && millis() - start < 1000) {
        uint8_t room = sc16_read(ch, REG_TXLVL);
        if (room == 0) {
            delay(1);
            continue;
        }
        size_t k = n - done < room ? n - done : room;
        fifo(false, ch, (uint8_t *)d + done, k);
        done += k;
    }
    c.tx += done;
    return done == n;
}

static void poll_chan(uint8_t ch)
{
    Chan &c = chans[ch];
    uint8_t lsr = sc16_read(ch, REG_LSR);   // Lesen loescht die Fehlerbits
    uint8_t n = sc16_read(ch, REG_RXLVL);
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    c.ovr += (lsr >> 1) & 1;
    c.par += (lsr >> 2) & 1;
    c.frm += (lsr >> 3) & 1;
    c.brk += (lsr >> 4) & 1;
    uint32_t now = micros();
    if (c.cur.len > 0 && n == 0 && now - c.last_us > gap_us(c.baud)) {
        close_frame(c);
    }
    xSemaphoreGive(data_mtx);
    if (n == 0) {
        return;
    }
    uint8_t buf[64];
    if (n > sizeof(buf)) {
        n = sizeof(buf);
    }
    fifo(true, ch, buf, n);
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < n; i++) {
        if (c.cur.len == 0) {
            c.cur.ms = millis();
            c.cur.tx = false;
        }
        if (c.cur.len < FRAME_MAX) {
            c.cur.d[c.cur.len] = buf[i];
        }
        c.cur.len++;
        if (c.cap_on && c.cap_n < sizeof(c.cap)) {
            c.cap[c.cap_n++] = buf[i];
        }
    }
    c.rx += n;
    c.last_us = micros();
    xSemaphoreGive(data_mtx);
}

static void burst_step(uint8_t ch)
{
    Chan &c = chans[ch];
    if (!c.burst_until) {
        return;
    }
    if ((int32_t)(millis() - c.burst_until) >= 0) {
        c.burst_until = 0;
        return;
    }
    uint8_t room = sc16_read(ch, REG_TXLVL);
    if (room) {
        uint8_t buf[64];
        memset(buf, c.burst_value, room);
        fifo(false, ch, buf, room);
        c.tx += room;
    }
}

static void task(void *)
{
    while (true) {
        poll_chan(SC16_CH_A);
        poll_chan(SC16_CH_B);
        burst_step(SC16_CH_A);
        burst_step(SC16_CH_B);
        vTaskDelay(1);
    }
}

bool sc16_burst(uint8_t ch, uint8_t value)
{
    if (ch > 1 || !chip_ok) {
        return false;
    }
    chans[ch].burst_value = value;
    chans[ch].burst_until = millis() + 10000;
    logf("SC16IS752 UART %s (%s): 10 s Dauersenden 0x%02X", chans[ch].name, chans[ch].pack, value);
    return true;
}

void sc16_loopback(uint8_t ch, String &o)
{
    static const uint8_t pat[] = {0x55, 0xAA, 0x01, 0x02, 0xC3};
    Chan &c = chans[ch];
    uint8_t mcr = sc16_read(ch, REG_MCR);
    sc16_capture(ch, true);
    sc16_write(ch, REG_MCR, mcr | 0x10);
    fifo(false, ch, (uint8_t *)pat, sizeof(pat));
    delay(20 + sizeof(pat) * 10000 / c.baud);       // Sendezeit plus Abholung durch die Task
    delay(10);
    sc16_write(ch, REG_MCR, mcr);
    uint8_t got[64];
    size_t n = sc16_captured(ch, got, sizeof(got));
    sc16_capture(ch, false);
    bool ok = n == sizeof(pat) && memcmp(got, pat, n) == 0;
    char b[96];
    snprintf(b, sizeof(b), "{\"name\":\"%s\",\"pack\":\"%s\",\"baud\":%lu,\"ok\":%s,\"sent\":", c.name, c.pack,
             (unsigned long)c.baud, ok ? "true" : "false");
    o += b;
    json_hex(o, pat, sizeof(pat));
    o += ",\"got\":";
    json_hex(o, got, n);
    o += '}';
    logf("SC16IS752 UART %s interne Schleife bei %lu Baud: %s (%u Bytes zurueck)", c.name, (unsigned long)c.baud,
         ok ? "in Ordnung" : "fehlerhaft", (unsigned)n);
}

void sc16_begin(int core)
{
    spi_mtx = xSemaphoreCreateMutex();
    data_mtx = xSemaphoreCreateMutex();
    chans[SC16_CH_A].name = "A";
    chans[SC16_CH_A].pack = "Pack 2";
    chans[SC16_CH_B].name = "B";
    chans[SC16_CH_B].pack = "Pack 1";
    pinMode(SC16_CS, OUTPUT);
    digitalWrite(SC16_CS, HIGH);
    spi.begin(SC16_SCK, SC16_MISO, SC16_MOSI, -1);
    // Chip da? Zwei Werte durchs Scratchpad
    sc16_write(SC16_CH_A, REG_SPR, 0x5A);
    sc16_write(SC16_CH_B, REG_SPR, 0xA5);
    chip_ok = sc16_read(SC16_CH_A, REG_SPR) == 0x5A && sc16_read(SC16_CH_B, REG_SPR) == 0xA5;
    if (!chip_ok) {
        logf("SC16IS752: antwortet nicht auf SPI");
        return;
    }
    prefs.begin("sc16", false);
    for (uint8_t ch = 0; ch < 2; ch++) {
        uint32_t b = prefs.getUInt(ch ? "bB" : "bA", 115200);
        chans[ch].baud = valid_baud(b) ? b : 115200;
        setup_uart(ch, chans[ch].baud);
    }
    logf("SC16IS752: UART A (Pack 2) %lu Baud, UART B (Pack 1) %lu Baud", (unsigned long)chans[0].baud,
         (unsigned long)chans[1].baud);
    xTaskCreatePinnedToCore(task, "sc16", 3072, NULL, 2, NULL, core);
}

bool sc16_ok()
{
    return chip_ok;
}

void sc16_capture(uint8_t ch, bool on)
{
    if (ch > 1) {
        return;
    }
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    chans[ch].cap_on = on;
    if (on) {
        chans[ch].cap_n = 0;
    }
    xSemaphoreGive(data_mtx);
}

size_t sc16_captured(uint8_t ch, uint8_t *buf, size_t max)
{
    if (ch > 1) {
        return 0;
    }
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    size_t n = chans[ch].cap_n < max ? chans[ch].cap_n : max;
    memcpy(buf, chans[ch].cap, n);
    xSemaphoreGive(data_mtx);
    return n;
}

void sc16_json(String &o)
{
    char b[200];
    snprintf(b, sizeof(b), "{\"ok\":%s,\"ch\":[", chip_ok ? "true" : "false");
    o += b;
    if (!chip_ok) {
        o += "]}";
        return;
    }
    xSemaphoreTake(data_mtx, portMAX_DELAY);
    for (int ch = 0; ch < 2; ch++) {
        const Chan &c = chans[ch];
        snprintf(b, sizeof(b),
                 "%s{\"name\":\"%s\",\"pack\":\"%s\",\"baud\":%lu,\"rx\":%lu,\"tx\":%lu,\"frames\":%lu,\"ovr\":%lu,"
                 "\"par\":%lu,\"frm\":%lu,\"brk\":%lu,\"list\":[",
                 ch ? "," : "", c.name, c.pack, (unsigned long)c.baud, (unsigned long)c.rx, (unsigned long)c.tx,
                 (unsigned long)c.frames, (unsigned long)c.ovr, (unsigned long)c.par, (unsigned long)c.frm,
                 (unsigned long)c.brk);
        o += b;
        uint32_t first = c.count > FRAMES ? c.count - FRAMES : 0;
        for (uint32_t i = c.count; i > first; i--) {
            const Frame &f = c.ring[(i - 1) % FRAMES];
            snprintf(b, sizeof(b), "%s{\"ms\":%lu,\"len\":%u,\"tx\":%s,\"d\":", i == c.count ? "" : ",",
                     (unsigned long)f.ms, f.len, f.tx ? "true" : "false");
            o += b;
            json_hex(o, f.d, f.len < FRAME_MAX ? f.len : FRAME_MAX);
            o += '}';
        }
        o += "]}";
    }
    xSemaphoreGive(data_mtx);
    o += "]}";
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
