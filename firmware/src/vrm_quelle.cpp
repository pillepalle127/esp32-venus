// Werte für VRM vom MQTT-Broker des Pi, nur lesend. Ablauf in docs/vrm.md.
#include "vrm_quelle.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mqtt_client.h>

#include "log.h"
#include "util.h"
#include "vrm_tabelle.h"

#if __has_include("vrm_secrets.h")
#include "vrm_secrets.h"
#endif
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"       // VRM_QUELLE_PASS kann auch hier stehen
#endif

#ifndef VRM_QUELLE_ID
#define VRM_QUELLE_ID ""
#endif
#ifndef VRM_QUELLE_USER
#define VRM_QUELLE_USER "remoteconsole"     // wie ioBroker (mqtt.1)
#endif
#ifndef VRM_QUELLE_PASS
#define VRM_QUELLE_PASS ""
#endif
#ifndef VRM_QUELLE_URI
#define VRM_QUELLE_URI "mqtt://192.168.1.10:1883"
#endif

static const int N = sizeof(VRM_TABELLE) / sizeof(VRM_TABELLE[0]);
static const int ROH = VQ_ROH;      // längere Texte werden verworfen
static const int PUFFER = 1024;     // esp-mqtt, Ein- und Ausgang
static const int SUBSCRIBE_MAX = PUFFER - 64;

struct Wert {
    char roh[ROH];
    uint8_t gueltig;
    uint8_t merker;
    uint32_t ms;
};

static Wert *werte;
static SemaphoreHandle_t mtx;
static esp_mqtt_client_handle_t client;
static volatile bool verbunden;
static uint32_t verbunden_ms;
static uint32_t empfangen;
static uint32_t verworfen;
static uint32_t letzte_ms;
static String zustand = "aus";

static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

static void setze(const char *z)
{
    lock();
    zustand = z;
    unlock();
}

int vq_anzahl() { return N; }
const VrmEintrag &vq_eintrag(int i) { return VRM_TABELLE[i]; }

int vq_index(const char *thema)
{
    // Die Tabelle ist nach Thema sortiert (tools/vrm_tabelle.py)
    int a = 0, b = N - 1;
    while (a <= b) {
        int m = (a + b) / 2;
        int c = strcmp(thema, VRM_TABELLE[m].thema);
        if (c == 0) {
            return m;
        }
        if (c < 0) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    return -1;
}

bool vq_roh(int i, char *buf, size_t n)
{
    lock();
    bool ok = werte[i].gueltig;
    if (ok) {
        strlcpy(buf, werte[i].roh, n);
    }
    unlock();
    return ok;
}

bool vq_zahl(int i, double &v)
{
    char b[ROH];
    if (!vq_roh(i, b, sizeof(b))) {
        return false;
    }
    if (!strcmp(b, "true") || !strcmp(b, "false")) {
        v = b[0] == 't';
        return true;
    }
    char *ende;
    v = strtod(b, &ende);
    return ende != b && *ende == 0;
}

bool vq_merker_nehmen(int i, uint8_t merker)
{
    lock();
    bool r = werte[i].merker & merker;
    werte[i].merker &= ~merker;
    unlock();
    return r;
}

bool vq_verbunden() { return verbunden; }
uint32_t vq_verbunden_seit_ms() { return verbunden_ms; }

// Holt aus {"value": X, ...} den JSON-Text von X. Leer bei Fehler.
static int json_value(const char *p, int n, char *out, int outn)
{
    const char *e = p + n;
    const char *k = NULL;
    for (const char *q = p; q + 7 <= e; q++) {
        if (!memcmp(q, "\"value\"", 7)) {
            k = q + 7;
            break;
        }
    }
    if (!k) {
        return -1;
    }
    while (k < e && (*k == ' ' || *k == ':')) {
        k++;
    }
    const char *a = k;
    if (k < e && *k == '"') {
        for (k++; k < e && *k != '"'; k++) {
            if (*k == '\\') {
                k++;
            }
        }
        k++;
    } else if (k < e && *k == '[') {
        while (k < e && *k != ']') {
            k++;
        }
        k++;
    } else {
        while (k < e && *k != ',' && *k != '}' && *k != ' ') {
            k++;
        }
    }
    int len = k - a;
    if (len <= 0 || len >= outn || k > e) {
        return -1;
    }
    memcpy(out, a, len);
    out[len] = 0;
    return len;
}

static void ereignis(void *, esp_event_base_t, int32_t id, void *data)
{
    auto *e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        verbunden = true;
        verbunden_ms = millis();
        setze("verbunden, abonniere");
        logf("VRM-Quelle: verbunden mit %s, abonniere %d Themen", VRM_QUELLE_URI, N);
        // In Gruppen abonnieren: höchstens 16 Themen und SUBSCRIBE_MAX Bytes je SUBSCRIBE.
        // 16 lange Themen brauchten 1128 Bytes; die Nachricht passte nicht in den Puffer,
        // danach kamen keine Werte und kein PINGRESP mehr (19.11.2026).
        static char themen[16][96];
        esp_mqtt_topic_t liste[16];
        int k = 0, bytes = 7, fehler = 0;
        for (int i = 0; i < N; i++) {
            snprintf(themen[k], sizeof(themen[k]), "N/%s/%s", VRM_QUELLE_ID, VRM_TABELLE[i].thema);
            liste[k].filter = themen[k];
            liste[k].qos = 0;
            bytes += 2 + strlen(themen[k]) + 1;
            k++;
            int naechstes = i + 1 < N ? 3 + 3 + strlen(VRM_QUELLE_ID) + 1 + strlen(VRM_TABELLE[i + 1].thema) : 0;
            if (k == 16 || i == N - 1 || bytes + naechstes > SUBSCRIBE_MAX) {
                if (esp_mqtt_client_subscribe_multiple(e->client, liste, k) < 0) {
                    fehler++;
                }
                k = 0;
                bytes = 7;
            }
        }
        if (fehler) {
            logf("VRM-Quelle: %d SUBSCRIBE nicht gesendet", fehler);
        }
        setze(fehler ? "verbunden, Abonnieren unvollständig" : "verbunden, hört mit");
        break;
    }
    case MQTT_EVENT_DATA: {
        if (e->topic_len == 0 || e->data_len != e->total_data_len) {
            verworfen++;     // Teil einer langen Nachricht: braucht die Tabelle nicht
            break;
        }
        // Thema ohne "N/<ID>/"
        int vor = 3 + strlen(VRM_QUELLE_ID);
        if (e->topic_len <= vor || e->topic_len - vor >= 96) {
            break;
        }
        char thema[96];
        memcpy(thema, e->topic + vor, e->topic_len - vor);
        thema[e->topic_len - vor] = 0;
        int i = vq_index(thema);
        if (i < 0) {
            break;
        }
        char roh[ROH];
        int len = e->data_len ? json_value(e->data, e->data_len, roh, sizeof(roh)) : -1;
        bool gueltig = len > 0 && strcmp(roh, "null") != 0;
        lock();
        Wert &w = werte[i];
        bool neu = gueltig != (bool)w.gueltig || (gueltig && strcmp(w.roh, roh) != 0);
        if (gueltig) {
            strlcpy(w.roh, roh, ROH);
        }
        w.gueltig = gueltig;
        w.ms = millis();
        if (neu) {
            w.merker |= VQ_NEU_VC | VQ_NEU_VK | VQ_NEU_LIVE;
        }
        empfangen++;
        letzte_ms = w.ms;
        unlock();
        if (len < 0 && e->data_len) {
            verworfen++;
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        if (verbunden) {
            logf("VRM-Quelle: getrennt");
        }
        verbunden = false;
        setze("getrennt");
        break;
    case MQTT_EVENT_ERROR: {
        static int letzter = -1;
        int code = e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED
                       ? (int)e->error_handle->connect_return_code : 0;
        if (code == 5 || code == 4) {
            setze("Broker lehnt ab: nicht berechtigt (Benutzer/Passwort in vrm_secrets.h)");
        } else if (code) {
            setze("Broker lehnt ab");
        } else {
            setze("keine Verbindung, versucht es wieder");
        }
        if (code != letzter) {
            letzter = code;
            logf("VRM-Quelle: Verbindung abgelehnt, Code %d", code);
        }
        break;
    }
    default:
        break;
    }
}

void vq_begin()
{
    if (client) {
        return;
    }
    mtx = xSemaphoreCreateMutex();
    werte = (Wert *)heap_caps_calloc(N, sizeof(Wert), MALLOC_CAP_SPIRAM);
    if (!*VRM_QUELLE_ID) {
        zustand = "VRM_QUELLE_ID fehlt in vrm_secrets.h";
        logf("VRM-Quelle: %s", zustand.c_str());
        return;
    }
    char cid[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(cid, sizeof(cid), "esp_venus_%02x%02x%02x", mac[3], mac[4], mac[5]);
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = VRM_QUELLE_URI;
    cfg.credentials.client_id = cid;
    cfg.credentials.username = VRM_QUELLE_USER;
    cfg.credentials.authentication.password = VRM_QUELLE_PASS;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 30000;
    // Gemessen 19.11.2026: Stapel 3,1 kB benutzt; Nachrichten unter 300 Byte. SUBSCRIBE
    // wird oben auf SUBSCRIBE_MAX begrenzt.
    cfg.buffer.size = PUFFER;
    cfg.buffer.out_size = PUFFER;
    cfg.task.stack_size = 5120;
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, ereignis, NULL);
    esp_mqtt_client_start(client);
    setze("verbinde");
}

void vq_json(String &o)
{
    int mit = 0;
    lock();
    for (int i = 0; i < N; i++) {
        mit += werte && werte[i].gueltig;
    }
    o += "{\"zustand\":";
    json_str(o, zustand.c_str());
    o += ",\"quelle\":";
    json_str(o, VRM_QUELLE_URI);
    o += ",";
    json_int(o, "themen", N);
    o += ",";
    json_int(o, "mit_wert", mit);
    o += ",";
    json_int(o, "empfangen", empfangen);
    o += ",";
    json_int(o, "verworfen", verworfen);
    o += ",";
    json_int(o, "alter_s", letzte_ms ? (long)((millis() - letzte_ms) / 1000) : -1);
    o += "}";
    unlock();
}
