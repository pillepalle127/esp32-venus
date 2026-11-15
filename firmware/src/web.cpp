// Testseite und JSON-Schnittstelle. Laeuft in der Netz-Task (Core 0), wie I2C und
// der RS485-Monitor, deshalb braucht es dafuer keine Sperren.
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "app.h"
#include "pins.h"
#include "devices.h"
#include "dpl.h"
#include "log.h"
#include "radio.h"
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

static const char *reset_reason()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Einschalten";
    case ESP_RST_EXT: return "Reset-Pin";
    case ESP_RST_SW: return "Software";
    case ESP_RST_PANIC: return "Absturz";
    case ESP_RST_INT_WDT: return "Watchdog (Interrupt)";
    case ESP_RST_TASK_WDT: return "Watchdog (Task)";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung";
    case ESP_RST_DEEPSLEEP: return "Tiefschlaf";
    default: return "unbekannt";
    }
}

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
             wl ? WiFi.RSSI() : 0, left_mode == LEFT_RS485 ? "rs485" : "vebus", reset_reason(), BUILD,
             WiFi.macAddress().c_str());
    o += b;
    if (wl) {
        json_str(o, WiFi.localIP().toString().c_str());
        o += ",\"ssid\":";
        json_str(o, WiFi.SSID().c_str());
    } else {
        o += "null";
    }
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

static void handle_send()
{
    if (left_mode != LEFT_RS485) {
        server.send(200, "application/json", "{\"err\":\"nur im RS485-Test\"}");
        return;
    }
    if (!rs_left.tx_ok) {
        server.send(200, "application/json", "{\"err\":\"Senden gesperrt, siehe Pinpruefung\"}");
        return;
    }
    uint8_t d[256];
    size_t n = 0;
    if (server.hasArg("text")) {
        String t = server.arg("text");
        t.replace("\\r", "\r");
        t.replace("\\n", "\n");
        n = t.length() < sizeof(d) ? t.length() : sizeof(d);
        memcpy(d, t.c_str(), n);
    } else {
        String h = server.arg("hex");
        int hi = -1;
        for (size_t i = 0; i < h.length() && n < sizeof(d); i++) {
            char c = h[i];
            if (c == ' ' || c == ',' || c == ':') {
                continue;
            }
            int v = hexval(c);
            if (v < 0) {
                server.send(200, "application/json", "{\"err\":\"kein Hex\"}");
                return;
            }
            if (hi < 0) {
                hi = v;
            } else {
                d[n++] = (uint8_t)(hi << 4 | v);
                hi = -1;
            }
        }
        if (hi >= 0) {
            server.send(200, "application/json", "{\"err\":\"ungerade Zahl Hex-Ziffern\"}");
            return;
        }
    }
    rs_left.send(d, n);
    server.send(200, "application/json", "{}");
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
