// Testseite und JSON-Schnittstelle. Laeuft in der Netz-Task (Core 0), wie I2C und
// der RS485-Monitor, deshalb braucht es dafuer keine Sperren.
#include <ESPmDNS.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "app.h"
#include "pins.h"
#include "devices.h"
#include "dpl.h"
#include "ess.h"
#include "log.h"
#include "radio.h"
#include "sc16.h"
#include "sysinfo.h"
#include "util.h"
#include "web_page.h"

static WebServer server(80);
static uint32_t restart_at;

// OTA mit den IDF-Funktionen: Beim sequentiellen Schreiben loescht der S3 je 4 KB direkt
// vor dem Beschreiben. Die Arduino-Bibliothek loescht 64-KB-Bloecke, waehrend denen
// stockte der Upload und brach genau an 64-KB-Grenzen ab.
static const esp_partition_t *ota_part;
static esp_ota_handle_t ota_handle;
static bool ota_open;
static esp_err_t ota_err = ESP_FAIL;


// Zahl als JSON, NaN und Unendlich als null (sonst ist das ganze JSON ungueltig)
static void addf(String &o, const char *key, double v, int dec)
{
    char b[48];
    if (isfinite(v)) {
        snprintf(b, sizeof(b), ",\"%s\":%.*f", key, dec, v);
    } else {
        snprintf(b, sizeof(b), ",\"%s\":null", key);
    }
    o += b;
}

static void json_sml(String &o, const char *name, const char *src, const SmlReader &s, long ferr, int rmt)
{
    o += "{\"name\":";
    json_str(o, name);
    o += ",\"src\":";
    json_str(o, src);
    char b[200];
    long age = s.telegrams + s.crc_swapped ? (long)(millis() - s.last_ms) : -1;
    snprintf(b, sizeof(b), ",\"bytes\":%lu,\"tel\":%lu,\"crc\":%lu,\"swap\":%lu,\"ovf\":%lu,\"age\":%ld,\"len\":%u",
             (unsigned long)s.bytes, (unsigned long)s.telegrams, (unsigned long)s.crc_errors,
             (unsigned long)s.crc_swapped, (unsigned long)s.overflows, age, (unsigned)s.last_len);
    o += b;
    addf(o, "imp", s.has_import ? s.import_wh : NAN, 1);
    addf(o, "exp", s.has_export ? s.export_wh : NAN, 1);
    addf(o, "pwr", s.has_power ? s.power_w : NAN, 1);
    if (ferr >= 0) {
        snprintf(b, sizeof(b), ",\"ferr\":%ld,\"rmt\":%s", ferr, rmt ? "true" : "false");
        o += b;
    }
    o += '}';
}

static void handle_status()
{
    String o;
    o.reserve(14000);
    char b[256];

    // System
    bool wl = WiFi.status() == WL_CONNECTED;
    o += "{\"sys\":{";
    snprintf(b, sizeof(b),
             "\"up\":%lu,\"heap\":%u,\"heapmin\":%u,\"psram\":%u,\"rssi\":%d,\"left\":\"%s\",\"reset\":\"%s\","
             "\"build\":\"%s\",\"mac\":\"%s\",\"ip\":",
             (unsigned long)(millis() / 1000), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             wl ? WiFi.RSSI() : 0, left_mode == LEFT_RS485 ? "rs485" : "vebus", sys_reset_reason(), BUILD,
             WiFi.macAddress().c_str());
    o += b;
    if (wl) {
        json_str(o, WiFi.localIP().toString().c_str());
        o += ",\"ssid\":";
        json_str(o, WiFi.SSID().c_str());
    } else {
        o += "null";
    }
    sys_json(o);
    o += "}";

    // BME280
    o += ",\"bme\":[";
    for (int i = 0; i < 2; i++) {
        snprintf(b, sizeof(b), "%s{\"addr\":%u,\"id\":%u,\"ok\":%s,\"err\":%lu", i ? "," : "", 0x76 + i,
                 bme[i].chip_id, bme[i].ok ? "true" : "false", (unsigned long)bme[i].errors);
        o += b;
        addf(o, "t", bme[i].temp_c, 2);
        addf(o, "h", bme[i].hum_pct, 2);
        addf(o, "p", bme[i].press_hpa, 2);
        o += '}';
    }
    o += "]";

    // Zaehler
    o += ",\"mt\":[";
    char src[24];
    snprintf(src, sizeof(src), "UART2, GPIO %d", MT681_RX);
    json_sml(o, "Haus", src, sml_house, -1, 0);
    o += ',';
    snprintf(src, sizeof(src), "RMT, GPIO %d", MT681_WP_RX);
    json_sml(o, "WP", src, sml_wp, (long)rmt_wp.decoder.framing_errors, rmt_wp.running);
    o += "]";

    // CAN
    o += ",\"can\":";
    if (can_ok) {
        can_mon.json(o);
    } else {
        o += "{\"ok\":false,\"state\":\"-\",\"frames\":0,\"rx_err\":0,\"bus_err\":0,\"rx_missed\":0,"
             "\"rx_overrun\":0,\"now\":0,\"ids\":[]}";
    }

    // Linker Kanal
    if (left_mode == LEFT_VEBUS) {
        snprintf(b, sizeof(b), ",\"vebus\":{\"sync\":%s,\"chk\":%lu,\"st80\":%u,\"led\":%u,\"blink\":%u",
                 vebus.hasNoSync() ? "false" : "true", (unsigned long)vebus.getChecksumFaults(),
                 vebus.getChargerStatus(), vebus.getLEDon(), vebus.getLEDblink());
        o += b;
        addf(o, "dci", vebus.getDCCurrent(), 1);
        addf(o, "temp", vebus.getTemp(), 1);
        addf(o, "ilim", vebus.getActInputCurrentLimit(), 1);
        addf(o, "imin", vebus.getMinInputCurrentLimit(), 1);
        addf(o, "imax", vebus.getMaxInputCurrentLimit(), 1);
        o += '}';
    } else {
        o += ",\"rs\":";
        rs_left.json(o);
    }

    // CMT2300A
    o += ",\"cmt\":";
    radio.json(o);
    o += ",\"dpl\":";
    dpl_json(o);
    if (left_mode == LEFT_VEBUS) {
        o += ",\"ess\":";
        ess_json(o);
    }
    o += ",\"sc16\":";
    sc16_json(o);

    // CH348 und VE.Direct
    o += ",\"ch348\":{\"ok\":";
    o += ch348_running ? "true" : "false";
    const uint8_t *ver = ch348_running ? ch348_version() : nullptr;
    snprintf(b, sizeof(b), ",\"ver\":\"%02X %02X %02X %02X\",\"bad\":%lu,\"ports\":[", ver ? ver[0] : 0,
             ver ? ver[1] : 0, ver ? ver[2] : 0, ver ? ver[3] : 0, (unsigned long)ch348_bad_blocks());
    o += b;
    xSemaphoreTake(ved_mtx, portMAX_DELAY);
    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        const VeDirectParser &v = ved[p];
        long age = v.blocks_ok ? (long)(millis() - v.last_ok_ms) : -1;
        snprintf(b, sizeof(b), "%s{\"bytes\":%lu,\"ok\":%lu,\"bad\":%lu,\"age\":%ld", p ? "," : "",
                 (unsigned long)v.bytes, (unsigned long)v.blocks_ok, (unsigned long)v.blocks_bad, age);
        o += b;
        const char *ser = v.get("SER#");
        if (ser) {
            for (const auto &d : VEDIRECT_DEVICES) {
                if (strcmp(d.serial, ser) == 0) {
                    snprintf(b, sizeof(b), ",\"dev\":\"%s %u\"", d.service, d.instance);
                    o += b;
                }
            }
        }
        o += ",\"f\":{";
        for (uint8_t i = 0; i < v.num_fields; i++) {
            if (i) {
                o += ',';
            }
            json_str(o, v.fields[i].name);
            o += ':';
            json_str(o, v.fields[i].value);
        }
        o += "}}";
    }
    xSemaphoreGive(ved_mtx);
    o += "]}";

    // Relais
    o += ",\"relay\":[";
    for (int i = 0; i < 2; i++) {
        snprintf(b, sizeof(b), "%s{\"gpio\":%d,\"name\":\"%s\",\"mode\":\"%s\"}", i ? "," : "", relays[i].gpio,
                 relays[i].name, relays[i].mode < 0 ? "frei" : relays[i].mode ? "high" : "low");
        o += b;
    }
    o += "]}";
    server.send(200, "application/json", o);
}

static void handle_i2c()
{
    uint8_t found[32];
    int n = i2c_scan(found, 32);
    String o = "{\"found\":[";
    for (int i = 0; i < n; i++) {
        if (i) {
            o += ',';
        }
        o += String(found[i]);
    }
    o += "]}";
    bme_init();
    server.send(200, "application/json", o);
}

static void handle_relay()
{
    int i = server.arg("i").toInt();
    String m = server.arg("m");
    if (i < 0 || i > 1 || (m != "frei" && m != "low" && m != "high")) {
        server.send(400, "application/json", "{\"err\":\"falsche Angabe\"}");
        return;
    }
    relay_set(i, m == "frei" ? -1 : m == "high" ? 1 : 0);
    server.send(200, "application/json", "{}");
}

static void handle_mode()
{
    String m = server.arg("m");
    uint32_t baud = server.arg("baud").toInt();
    if ((m != "vebus" && m != "rs485") || baud < 1200 || baud > 1000000) {
        server.send(400, "application/json", "{\"err\":\"falsche Angabe\"}");
        return;
    }
    left_mode_save(m == "rs485" ? LEFT_RS485 : LEFT_VEBUS, baud);
    logf("Linker Kanal: %s, %lu Baud, Neustart", m.c_str(), (unsigned long)baud);
    server.send(200, "application/json", "{}");
    restart_at = millis() + 500;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Nutzdaten aus ?text= oder ?hex=. Liefert eine Fehlermeldung oder nullptr.
static const char *payload(uint8_t *d, size_t max, size_t &n)
{
    n = 0;
    if (server.hasArg("text")) {
        String t = server.arg("text");
        t.replace("\\r", "\r");
        t.replace("\\n", "\n");
        n = t.length() < max ? t.length() : max;
        memcpy(d, t.c_str(), n);
        return nullptr;
    }
    String h = server.arg("hex");
    int hi = -1;
    for (size_t i = 0; i < h.length() && n < max; i++) {
        char c = h[i];
        if (c == ' ' || c == ',' || c == ':') {
            continue;
        }
        int v = hexval(c);
        if (v < 0) {
            return "kein Hex";
        }
        if (hi < 0) {
            hi = v;
        } else {
            d[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return hi >= 0 ? "ungerade Zahl Hex-Ziffern" : nullptr;
}

static void send_err(const char *e)
{
    String o = "{\"err\":";
    json_str(o, e);
    o += '}';
    server.send(200, "application/json", o);
}

static void handle_send()
{
    if (left_mode != LEFT_RS485) {
        send_err("nur im RS485-Test");
        return;
    }
    if (!rs_left.tx_ok) {
        send_err("Senden gesperrt, siehe Pinpruefung");
        return;
    }
    uint8_t d[256];
    size_t n;
    const char *e = payload(d, sizeof(d), n);
    if (e) {
        send_err(e);
        return;
    }
    rs_left.send(d, n);
    server.send(200, "application/json", "{}");
}

// Rechte Kanaele: ?ch=A|B, Baudrate mit &baud=, Senden mit &hex= oder &text=
static void handle_pack()
{
    String c = server.arg("ch");
    uint8_t ch = c == "A" ? SC16_CH_A : c == "B" ? SC16_CH_B : 0xFF;
    if (ch == 0xFF || !sc16_ok()) {
        send_err(sc16_ok() ? "Kanal A oder B" : "SC16IS752 antwortet nicht");
        return;
    }
    if (server.hasArg("baud")) {
        if (!sc16_set_baud(ch, server.arg("baud").toInt())) {
            send_err("Baudrate geht mit dem Quarz nicht ohne Rest");
            return;
        }
    }
    if (server.hasArg("hex") || server.hasArg("text")) {
        uint8_t d[256];
        size_t n;
        const char *e = payload(d, sizeof(d), n);
        if (e) {
            send_err(e);
            return;
        }
        if (!sc16_send(ch, d, n)) {
            send_err("Senden fehlgeschlagen");
            return;
        }
    }
    server.send(200, "application/json", "{}");
}

// Testplan A6c: jeder Kanal sendet eine eigene Folge, alle anderen hoeren mit.
// Teilnehmer: 0 links (UART1, nur im RS485-Test mit freiem Senden), 1 Pack 1 (UART B), 2 Pack 2 (UART A)
static void handle_cross()
{
    static const char *names[] = {"links", "Pack 1", "Pack 2"};
    static const uint8_t sc_ch[] = {0xFF, SC16_CH_B, SC16_CH_A};
    bool left_ok = left_mode == LEFT_RS485 && rs_left.tx_ok;
    bool right_ok = sc16_ok();
    char b[96];
    snprintf(b, sizeof(b), "{\"left\":%s,\"right\":%s,\"baud\":[%lu,%lu,%lu],\"res\":[", left_ok ? "true" : "false",
             right_ok ? "true" : "false", (unsigned long)(left_ok ? rs_left.baud : 0),
             (unsigned long)sc16_baud(SC16_CH_B), (unsigned long)sc16_baud(SC16_CH_A));
    String o = b;
    bool first = true;
    for (int s = 0; s < 3; s++) {
        if ((s == 0 && !left_ok) || (s > 0 && !right_ok)) {
            continue;
        }
        uint8_t pat[4] = {0x55, 0xAA, (uint8_t)(0x30 + s), 0xC3};
        if (left_ok) {
            rs_left.poll();
            rs_left.capture(true);
        }
        if (right_ok) {
            sc16_capture(SC16_CH_A, true);
            sc16_capture(SC16_CH_B, true);
        }
        if (s == 0) {
            rs_left.send(pat, sizeof(pat));
        } else {
            sc16_send(sc_ch[s], pat, sizeof(pat));
        }
        // Bei 2400 Baud brauchen 4 Bytes knapp 17 ms
        uint32_t t0 = millis();
        while (millis() - t0 < 80) {
            if (left_ok) {
                rs_left.poll();
            }
            delay(1);
        }
        o += first ? "{\"from\":" : ",{\"from\":";
        first = false;
        json_str(o, names[s]);
        o += ",\"sent\":";
        json_hex(o, pat, sizeof(pat));
        o += ",\"to\":[";
        bool f2 = true;
        for (int r = 0; r < 3; r++) {
            if ((r == 0 && !left_ok) || (r > 0 && !right_ok)) {
                continue;
            }
            uint8_t got[64];
            size_t n = r == 0 ? rs_left.captured(got, sizeof(got)) : sc16_captured(sc_ch[r], got, sizeof(got));
            bool ok = n == sizeof(pat) && memcmp(got, pat, n) == 0;
            o += f2 ? "{\"name\":" : ",{\"name\":";
            f2 = false;
            json_str(o, names[r]);
            o += r == s ? ",\"self\":true" : ",\"self\":false";
            o += ok ? ",\"ok\":true,\"got\":" : ",\"ok\":false,\"got\":";
            json_hex(o, got, n);
            o += '}';
        }
        o += "]}";
        if (left_ok) {
            rs_left.capture(false);
        }
        if (right_ok) {
            sc16_capture(SC16_CH_A, false);
            sc16_capture(SC16_CH_B, false);
        }
        logf("RS485-Kreuztest: %s sendet 55 AA %02X C3", names[s], pat[2]);
    }
    o += "]}";
    server.send(200, "application/json", o);
}

// Hex-Zahl mit genau n Stellen, sonst false
static bool parse_hex(const String &s, unsigned n, uint64_t &v)
{
    if (s.length() != n) {
        return false;
    }
    v = 0;
    for (unsigned i = 0; i < n; i++) {
        int h = hexval(s[i]);
        if (h < 0) {
            return false;
        }
        v = v << 4 | (uint64_t)h;
    }
    return true;
}

// Geraeteliste: ?id=<8 oder 12 Hex-Stellen>&name=..&poll=0|1&iv=<s>, loeschen mit &del=1
static void handle_dev()
{
    String id = server.arg("id");
    id.trim();
    id.toLowerCase();
    uint64_t v = 0, serial = 0;
    if (parse_hex(id, 12, v)) {
        serial = v;
    } else if (!parse_hex(id, 8, v)) {
        server.send(200, "application/json", "{\"err\":\"Seriennummer (12 Stellen) oder Kennung (8 Stellen) in Hex\"}");
        return;
    }
    bool ok;
    if (server.hasArg("del")) {
        ok = radio.dev_del((uint32_t)v);
    } else {
        String name = server.arg("name");
        name.trim();
        int8_t poll = server.hasArg("poll") ? (server.arg("poll") == "1" ? 1 : 0) : -1;
        long iv = server.hasArg("iv") ? server.arg("iv").toInt() : 0;
        if (iv < 0 || iv > 3600) {
            server.send(200, "application/json", "{\"err\":\"Abstand 1 bis 3600 s\"}");
            return;
        }
        ok = radio.dev_set((uint32_t)v, serial, server.hasArg("name") ? name.c_str() : nullptr, poll, (uint16_t)iv);
    }
    server.send(200, "application/json", ok ? "{}" : "{\"err\":\"Funk nicht bereit\"}");
}

// Alle sichtbaren Netze mit BSSID, Kanal und Signalstaerke
static void handle_scan()
{
    int n = WiFi.scanNetworks(false, true);
    String o = "{\"bssid\":";
    json_str(o, WiFi.BSSIDstr().c_str());
    o += ",\"list\":[";
    for (int i = 0; i < n; i++) {
        if (i) {
            o += ',';
        }
        o += "{\"ssid\":";
        json_str(o, WiFi.SSID(i).c_str());
        o += ",\"bssid\":";
        json_str(o, WiFi.BSSIDstr(i).c_str());
        char b[64];
        snprintf(b, sizeof(b), ",\"ch\":%ld,\"rssi\":%ld}", (long)WiFi.channel(i), (long)WiFi.RSSI(i));
        o += b;
    }
    o += "]}";
    WiFi.scanDelete();
    logf("WLAN-Scan: %d Netze", n);
    server.send(200, "application/json", o);
}

static void handle_log()
{
    String o = "{\"lines\":";
    uint32_t next = log_json((uint32_t)server.arg("from").toInt(), o);
    o += ",\"next\":";
    o += String(next);
    o += '}';
    server.send(200, "application/json", o);
}

static void handle_update_done()
{
    if (ota_err == ESP_OK && !ota_open) {
        server.send(200, "text/plain; charset=utf-8", "fertig, Neustart");
        restart_at = millis() + 500;
    } else {
        server.send(200, "text/plain; charset=utf-8", (String("Fehler: ") + esp_err_to_name(ota_err)).c_str());
    }
}

static void handle_update_upload()
{
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        logf("Update: %s", up.filename.c_str());
        vebus.setTxEnabled(false);
        // Der Webserver bricht den Upload ab, wenn zwischen zwei Paketen laenger als diese
        // Zeit nichts kommt. Voreinstellung 1 s.
        server.client().setTimeout(15000);
        if (ota_open) {
            esp_ota_abort(ota_handle);
            ota_open = false;
        }
        ota_part = esp_ota_get_next_update_partition(NULL);
        ota_err = ota_part ? esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) : ESP_ERR_NOT_FOUND;
        ota_open = ota_err == ESP_OK;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (ota_open && ota_err == ESP_OK) {
            ota_err = esp_ota_write(ota_handle, up.buf, up.currentSize);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (ota_open) {
            if (ota_err == ESP_OK) {
                ota_err = esp_ota_end(ota_handle);      // prueft das Image
                if (ota_err == ESP_OK) {
                    ota_err = esp_ota_set_boot_partition(ota_part);
                }
            } else {
                esp_ota_abort(ota_handle);
            }
            ota_open = false;
        }
        logf("Update: %u Bytes, %s", (unsigned)up.totalSize, esp_err_to_name(ota_err));
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (ota_open) {
            esp_ota_abort(ota_handle);
            ota_open = false;
        }
        ota_err = ESP_FAIL;
        logf("Update: abgebrochen nach %u Bytes", (unsigned)up.totalSize);
    }
}

void web_begin()
{
    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/i2c", HTTP_GET, handle_i2c);
    server.on("/api/relay", HTTP_GET, handle_relay);
    server.on("/api/mode", HTTP_GET, handle_mode);
    server.on("/api/send", HTTP_GET, handle_send);
    server.on("/api/log", HTTP_GET, handle_log);
    server.on("/api/scan", HTTP_GET, handle_scan);
    server.on("/api/burst", HTTP_GET, []() {
        if (left_mode != LEFT_RS485 || !rs_left.tx_ok) {
            server.send(200, "application/json", "{\"err\":\"nur im RS485-Test mit freiem Senden\"}");
            return;
        }
        uint8_t v = server.arg("b") == "55" ? 0x55 : 0x00;
        rs_left.burst(10000, v);
        logf("RS485-Test: 10 s Dauersenden 0x%02X", v);
        server.send(200, "application/json", "{}");
    });
    server.on("/api/can_selftest", HTTP_GET, []() {
        String o;
        can_selftest(o);
        server.send(200, "application/json", o);
    });
    server.on("/api/can_clear", HTTP_GET, []() {
        can_mon.clear();
        server.send(200, "application/json", "{}");
    });
    server.on("/api/dev", HTTP_GET, handle_dev);
    // Log als Text, auch das vom Lauf vor dem letzten Neustart
    server.on("/log.txt", HTTP_GET, []() {
        String o;
        log_text(o);
        server.sendHeader("Content-Disposition", "inline; filename=esp_venus_log.txt");
        server.send(200, "text/plain; charset=utf-8", o);
    });
    // Coredump nach einem Absturz, zum Entschluesseln mit der ELF-Datei des Builds
    server.on("/coredump.elf", HTTP_GET, []() {
        uint32_t addr, size;
        if (!sys_coredump(addr, size)) {
            server.send(404, "text/plain; charset=utf-8", "kein Coredump im Flash");
            return;
        }
        server.sendHeader("Content-Disposition", "attachment; filename=coredump.elf");
        server.setContentLength(size);
        server.send(200, "application/octet-stream", "");
        uint8_t buf[1024];
        for (uint32_t off = 0; off < size; off += sizeof(buf)) {
            uint32_t n = size - off < sizeof(buf) ? size - off : sizeof(buf);
            if (esp_flash_read(NULL, buf, addr + off, n) != ESP_OK) {
                break;
            }
            server.sendContent((const char *)buf, n);
        }
    });
    server.on("/api/coredump_erase", HTTP_GET, []() {
        sys_coredump_erase();
        server.send(200, "application/json", "{}");
    });
    server.on("/api/boot_other", HTTP_GET, []() {
        String msg;
        bool ok = sys_boot_other(msg);
        String o = "{\"ok\":";
        o += ok ? "true" : "false";
        o += ",\"msg\":";
        json_str(o, msg.c_str());
        o += '}';
        server.send(200, "application/json", o);
        if (ok) {
            restart_at = millis() + 500;
        }
    });
    // Absturztest fuer Log und Coredump, nur mit ?wirklich=1
    server.on("/api/crash_test", HTTP_GET, []() {
        if (server.arg("wirklich") != "1") {
            server.send(200, "application/json", "{\"err\":\"nur mit ?wirklich=1\"}");
            return;
        }
        server.send(200, "application/json", "{}");
        logf("Absturztest auf Wunsch, gleich folgt ein Absturz");
        delay(300);
        *(volatile uint32_t *)0 = 0;
    });
    server.on("/api/pack", HTTP_GET, handle_pack);
    server.on("/api/pack_loop", HTTP_GET, []() {
        String o = "[";
        for (uint8_t ch : {SC16_CH_B, SC16_CH_A}) {
            if (o.length() > 1) {
                o += ',';
            }
            sc16_loopback(ch, o);
        }
        o += ']';
        server.send(200, "application/json", o);
    });
    server.on("/api/pack_burst", HTTP_GET, []() {
        String c = server.arg("ch");
        uint8_t ch = c == "A" ? SC16_CH_A : c == "B" ? SC16_CH_B : 0xFF;
        bool ok = ch != 0xFF && sc16_burst(ch, server.arg("b") == "55" ? 0x55 : 0x00);
        server.send(200, "application/json", ok ? "{}" : "{\"err\":\"Kanal A oder B\"}");
    });
    server.on("/api/rs485_cross", HTTP_GET, handle_cross);
    server.on("/api/sc16_test", HTTP_GET, []() {
        String o;
        sc16_selftest(o);
        server.send(200, "application/json", o);
    });
    server.on("/api/ess", HTTP_GET, []() {
        if (left_mode != LEFT_VEBUS) {
            send_err("linker Kanal nicht auf VE.Bus");
            return;
        }
        auto f = [](const char *k) { return server.hasArg(k) ? server.arg(k).toFloat() : NAN; };
        String m = server.arg("mode");
        int mode = m == "aus" ? ESS_OFF : m == "null" ? ESS_ZERO : m == "fest" ? ESS_FIXED : -1;
        int send = server.hasArg("send") ? server.arg("send") == "1" : -1;
        ess_set(send, mode, f("target"), f("fixed"), f("soc_min"), f("max_dis"), f("max_ch"));
        if (server.hasArg("sw")) {
            ess_switch(server.arg("sw").c_str());
        }
        server.send(200, "application/json", "{}");
    });
    server.on("/api/dpl", HTTP_GET, []() {
        if (server.hasArg("on")) {
            dpl_set_enabled(server.arg("on") == "1");
            logf("Regelung: von der Seite %s", server.arg("on") == "1" ? "eingeschaltet" : "ausgeschaltet");
        }
        server.send(200, "application/json", "{}");
    });
    server.on("/api/radio", HTTP_GET, []() {
        if (server.hasArg("ch")) {
            radio.listen((uint8_t)server.arg("ch").toInt());
        }
        if (server.hasArg("scan")) {
            radio.scan();
        }
        if (server.hasArg("clear")) {
            radio.clear();
        }
        if (server.hasArg("resume")) {
            radio.resume();
        }
        if (server.hasArg("dtu")) {
            radio.set_dtu_channel((uint8_t)server.arg("dtu").toInt());
        }
        if (server.hasArg("pa")) {
            radio.set_pa((int8_t)server.arg("pa").toInt());
        }
        server.send(200, "application/json", "{}");
    });
    server.on("/api/restart", HTTP_GET, []() {
        server.send(200, "application/json", "{}");
        restart_at = millis() + 500;
    });
    server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
    server.begin();
}

void web_loop()
{
    static bool mdns;
    if (!mdns && WiFi.status() == WL_CONNECTED) {
        mdns = true;
        MDNS.begin("esp32-venus");
        MDNS.addService("http", "tcp", 80);
        logf("WLAN: %s, %s, Seite http://%s/ oder http://esp32-venus.local/", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
    }
    server.handleClient();
    if (restart_at && (int32_t)(millis() - restart_at) >= 0) {
        vebus.setTxEnabled(false);
        ESP.restart();
    }
}
