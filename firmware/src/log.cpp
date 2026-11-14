#include "log.h"

#include <esp_log.h>
#include <stdarg.h>

#include "util.h"

#define LOG_LINES 150
#define LOG_LEN   200

static char lines[LOG_LINES][LOG_LEN];
static uint32_t next_seq;
static SemaphoreHandle_t mtx;

static void store(const char *text)
{
    if (mtx == NULL) {
        return;
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    char *line = lines[next_seq % LOG_LINES];
    unsigned long s = millis() / 1000;
    int n = snprintf(line, LOG_LEN, "%02lu:%02lu:%02lu ", s / 3600, (s / 60) % 60, s % 60);
    strlcpy(line + n, text, LOG_LEN - n);
    next_seq++;
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
    mtx = xSemaphoreCreateMutex();
    esp_log_set_vprintf(idf_vprintf);
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
    uint32_t first = next_seq > LOG_LINES ? next_seq - LOG_LINES : 0;
    if (from < first || from > next_seq) {
        from = first;
    }
    out += '[';
    for (uint32_t i = from; i < next_seq; i++) {
        if (i != from) {
            out += ',';
        }
        json_str(out, lines[i % LOG_LINES]);
    }
    out += ']';
    uint32_t seq = next_seq;
    xSemaphoreGive(mtx);
    return seq;
}
