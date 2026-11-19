// VRM, Schritt V1: Verbindung pruefen, ohne Werte zu senden. Ablauf in docs/vrm.md.
#include "vrm.h"

#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <mqtt_client.h>

#include "log.h"
#include "util.h"

#if __has_include("vrm_secrets.h")
#include "vrm_secrets.h"
#define VRM_HAVE_SECRETS 1
#else
#define VRM_HAVE_SECRETS 0
#endif

static const char VRM_LOG_URL[] = "https://ccgxlogging.victronenergy.com/log/log.php";
static const int VRM_INTERVAL = 300;    // wie /Settings/Vrmlogger/LogInterval auf dem Pi

static SemaphoreHandle_t mtx;

// Verbindungstest
static volatile bool post_busy;
static String post_result = "noch nicht gelaufen";
static uint32_t post_at;                // Laufzeit in s beim letzten Ergebnis

// MQTT
static esp_mqtt_client_handle_t client;
static volatile bool mqtt_busy;
static String mqtt_state = "aus";
static String mqtt_broker;
static uint32_t mqtt_msgs;
static uint32_t mqtt_until;             // millis() bis zum Trennen
static String mqtt_last[10];            // zuletzt empfangene Themen mit Inhalt
static int mqtt_last_n;

static void lock()
{
    if (!mtx) {
        mtx = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
}

static void unlock() { xSemaphoreGive(mtx); }

static void set_post(const String &s)
{
    lock();
    post_result = s;
    post_at = millis() / 1000;
    unlock();
    logf("VRM: Verbindungstest: %s", s.c_str());
}

static void set_mqtt(const String &s)
{
    lock();
    mqtt_state = s;
    unlock();
    logf("VRM: MQTT: %s", s.c_str());
}

// ---------------------------------------------------------------- Verbindungstest

static void post_task(void *)
{
#if VRM_HAVE_SECRETS
    // Wie vrm_encode(TEST_POST, {}, 300) im vrmlogger: d, IMEI, c vorne, dann t, dann das Token
    String body = "d=2&IMEI=" VRM_PORTAL_ID "&c=6&t=" + String(VRM_INTERVAL) + "&VRMAUTHTOKEN=" VRM_AUTH_TOKEN;
    esp_http_client_config_t cfg = {};
    cfg.url = VRM_LOG_URL;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 10000;
    // ccgxlogging hat ein Zertifikat der eigenen CCGX-CA von Victron, nicht aus dem Bundle
    static const char ca[] = VRM_CA_PEM;
    cfg.cert_pem = ca;
    cfg.user_agent = "VE/CCGX/2";
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    esp_http_client_set_header(h, "Content-Type", "application/x-www-form-urlencoded");
    uint32_t t0 = millis();
    esp_err_t err = esp_http_client_open(h, body.length());
    String res;
    if (err != ESP_OK) {
        res = String("keine Verbindung: ") + esp_err_to_name(err);
    } else {
        esp_http_client_write(h, body.c_str(), body.length());
        esp_http_client_fetch_headers(h);
        int code = esp_http_client_get_status_code(h);
        char buf[64] = {};
        int n = esp_http_client_read(h, buf, sizeof(buf) - 1);
        String text = n > 0 ? String(buf) : String();
        text.trim();
        bool ok = code == 200 && text == "vrm: OK";
        res = String(ok ? "in Ordnung" : "abgelehnt") + ", HTTP " + code + ", Antwort \"" + text + "\", " +
              String(millis() - t0) + " ms";
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    set_post(res);
#endif
    post_busy = false;
    vTaskDelete(NULL);
}

void vrm_test_post()
{
#if !VRM_HAVE_SECRETS
    set_post("src/vrm_secrets.h fehlt");
    return;
#endif
    if (post_busy) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        set_post("kein WLAN");
        return;
    }
    post_busy = true;
    set_post("läuft …");
    xTaskCreatePinnedToCore(post_task, "vrm_post", 8192, NULL, 1, NULL, 0);
}

// ---------------------------------------------------------------- MQTT mithoeren

// Wie _get_vrm_broker_url() in mosquitto_bridge_registrator.py
static String broker_host(const char *id)
{
    unsigned sum = 0;
    for (const char *p = id; *p; p++) {
        sum += (unsigned char)tolower(*p);
    }
    return "mqtt" + String(sum % 128) + ".victronenergy.com";
}

static void mqtt_event(void *, esp_event_base_t, int32_t id, void *data)
{
#if VRM_HAVE_SECRETS
    auto *e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        set_mqtt("angemeldet bei " + mqtt_broker + ", abonniere R/<ID>/#");
        esp_mqtt_client_subscribe(e->client, "R/" VRM_PORTAL_ID "/#", 0);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        set_mqtt("angemeldet, hört auf R/<ID>/#, sendet nichts");
        break;
    case MQTT_EVENT_DATA: {
        // Bei langen Nachrichten kommt der Rest ohne Thema, nur den ersten Teil zeigen
        if (e->topic_len == 0) {
            break;
        }
        String t(e->topic, e->topic_len);
        t.replace(VRM_PORTAL_ID, "<ID>");
        String p(e->data, min(e->data_len, 120));
        String line = t + " " + (e->total_data_len ? p : String("(leer)"));
        lock();
        mqtt_msgs++;
        for (int i = min(mqtt_last_n, 9); i > 0; i--) {
            mqtt_last[i] = mqtt_last[i - 1];
        }
        mqtt_last[0] = line;
        mqtt_last_n = min(mqtt_last_n + 1, 10);
        unlock();
        logf("VRM: MQTT empfangen: %s", line.c_str());
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        set_mqtt("getrennt");
        break;
    case MQTT_EVENT_ERROR: {
        auto *eh = e->error_handle;
        String s = "Fehler";
        if (eh->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            s += ", Broker lehnt ab, Code " + String((int)eh->connect_return_code);
        } else if (eh->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            s += ", TLS/TCP: esp_tls " + String(eh->esp_tls_last_esp_err) + ", mbedtls " + String(eh->esp_tls_stack_err) +
                 ", errno " + String(eh->esp_transport_sock_errno);
        }
        set_mqtt(s);
        break;
    }
    default:
        break;
    }
#endif
}

static void listen_task(void *)
{
#if VRM_HAVE_SECRETS
    static const char ca[] = VRM_CA_PEM;
    mqtt_broker = broker_host(VRM_PORTAL_ID);
    String uri = "mqtts://" + mqtt_broker + ":443";
    char cid[32];
    snprintf(cid, sizeof(cid), "esp_venus_%08lx", (unsigned long)esp_random());
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri.c_str();
    cfg.broker.verification.certificate = ca;
    cfg.credentials.username = "ccgxapikey_" VRM_PORTAL_ID;
    cfg.credentials.client_id = cid;
    cfg.credentials.authentication.password = VRM_MQTT_PASSWORD;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 30000;   // nicht im Sekundentakt neu versuchen
    cfg.task.stack_size = 8192;
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event, NULL);
    set_mqtt("verbinde mit " + mqtt_broker + " als " + String(cid));
    esp_mqtt_client_start(client);
    while ((int32_t)(millis() - mqtt_until) < 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    client = NULL;
    lock();
    String n = String(mqtt_msgs);
    unlock();
    set_mqtt("aus, " + n + " Nachrichten empfangen");
#endif
    mqtt_busy = false;
    vTaskDelete(NULL);
}

void vrm_listen(int seconds)
{
#if !VRM_HAVE_SECRETS
    set_mqtt("src/vrm_secrets.h fehlt");
    return;
#endif
    seconds = constrain(seconds, 10, 300);
    if (mqtt_busy) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        set_mqtt("kein WLAN");
        return;
    }
    lock();
    mqtt_msgs = 0;
    mqtt_last_n = 0;
    unlock();
    mqtt_until = millis() + seconds * 1000UL;
    mqtt_busy = true;
    logf("VRM: MQTT mithören für %d s", seconds);
    xTaskCreatePinnedToCore(listen_task, "vrm_mqtt", 6144, NULL, 1, NULL, 0);
}

void vrm_stop()
{
    if (mqtt_busy) {
        mqtt_until = millis();
    }
}

void vrm_json(String &o)
{
    lock();
    o += "{\"secrets\":";
    o += VRM_HAVE_SECRETS ? "true" : "false";
    o += ",\"post\":";
    json_str(o, post_result.c_str());
    o += ",";
    json_int(o, "post_at", post_at);
    o += ",\"post_busy\":";
    o += post_busy ? "true" : "false";
    o += ",\"mqtt\":";
    json_str(o, mqtt_state.c_str());
    o += ",\"mqtt_busy\":";
    o += mqtt_busy ? "true" : "false";
    o += ",";
    json_int(o, "left", mqtt_busy ? max(0L, (long)((int32_t)(mqtt_until - millis()) / 1000)) : 0);
    o += ",";
    json_int(o, "msgs", mqtt_msgs);
    o += ",\"last\":[";
    for (int i = 0; i < mqtt_last_n; i++) {
        if (i) {
            o += ',';
        }
        json_str(o, mqtt_last[i].c_str());
    }
    o += "]}";
    unlock();
}
