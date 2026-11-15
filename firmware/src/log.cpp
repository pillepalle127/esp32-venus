#include "log.h"

#include <esp_attr.h>
#include <esp_log.h>
#include <stdarg.h>
#include <time.h>

#include "util.h"

#define LOG_LINES 200
#define LOG_LEN   200
#define LOG_MAGIC 0x4C4F4732

// Im Bereich .noinit: Der Start loescht ihn nicht, das Log uebersteht also Neustart,
// Absturz und Watchdog. Nach Stromausfall ist es leer (Magic passt nicht).
struct LogBuf {
    uint32_t magic;
    uint32_t next_seq;
    char lines[LOG_LINES][LOG_LEN];
};
static __NOINIT_ATTR LogBuf lb;
static SemaphoreHandle_t mtx;

static void store(const char *text)
{
    if (mtx == NULL) {
        return;
    }
    char stamp[24];
    time_t t = time(nullptr);
    if (t > 1700000000) {
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(stamp, sizeof(stamp), "%02d.%02d. %02d:%02d:%02d ", lt.tm_mday, lt.tm_mon + 1, lt.tm_hour,
                 lt.tm_min, lt.tm_sec);
    } else {
        // Vor dem Stellen der Uhr: Laufzeit seit dem Start
        unsigned long s = millis() / 1000;
        snprintf(stamp, sizeof(stamp), "+%02lu:%02lu:%02lu ", s / 3600, (s / 60) % 60, s % 60);
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    char *line = lb.lines[lb.next_seq % LOG_LINES];
    int n = strlcpy(line, stamp, LOG_LEN);
    strlcpy(line + n, text, LOG_LEN - n);
    lb.next_seq++;
    xSemaphoreGive(mtx);
}

static void vlog(const char *fmt, va_list ap)
{
    char buf[LOG_LEN];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    // Zeilenende und Farbcodes der IDF-Meldungen entfernen
    char clean[LOG_LEN];
    size_t j = 0;
    for (size_t i = 0; buf[i] != '\0' && j < sizeof(clean) - 1; i++) {
        if (buf[i] == '\033') {
            while (buf[i] != '\0' && buf[i] != 'm') {
                i++;
            }
            if (buf[i] == '\0') {
                break;
            }
            continue;
        }
        if (buf[i] != '\r' && buf[i] != '\n') {
            clean[j++] = buf[i];
        }
    }
    clean[j] = '\0';
    if (j == 0) {
        return;
    }
    Serial.println(clean);
    store(clean);
}

static int idf_vprintf(const char *fmt, va_list ap)
{
    vlog(fmt, ap);
    return 0;
}

void log_init()
{
    bool kept = lb.magic == LOG_MAGIC && lb.next_seq < 0x40000000;
    if (!kept) {
        memset(&lb, 0, sizeof(lb));
        lb.magic = LOG_MAGIC;
    } else {
        // Eine Zeile kann beim Absturz halb geschrieben sein
        for (int i = 0; i < LOG_LINES; i++) {
            lb.lines[i][LOG_LEN - 1] = 0;
        }
    }
    mtx = xSemaphoreCreateMutex();
    esp_log_set_vprintf(idf_vprintf);
    if (kept) {
        store("======== Neustart, das Log davor ist erhalten ========");
    } else {
        store("======== Start, Log leer (Einschalten oder Stromausfall) ========");
    }
}

void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

uint32_t log_json(uint32_t from, String &out)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t first = lb.next_seq > LOG_LINES ? lb.next_seq - LOG_LINES : 0;
    if (from < first || from > lb.next_seq) {
        from = first;
    }
    out += '[';
    for (uint32_t i = from; i < lb.next_seq; i++) {
        if (i != from) {
            out += ',';
        }
        json_str(out, lb.lines[i % LOG_LINES]);
    }
    out += ']';
    uint32_t seq = lb.next_seq;
    xSemaphoreGive(mtx);
    return seq;
}

void log_text(String &out)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t first = lb.next_seq > LOG_LINES ? lb.next_seq - LOG_LINES : 0;
    out.reserve((lb.next_seq - first) * 80);
    for (uint32_t i = first; i < lb.next_seq; i++) {
        out += lb.lines[i % LOG_LINES];
        out += '\n';
    }
    xSemaphoreGive(mtx);
}
