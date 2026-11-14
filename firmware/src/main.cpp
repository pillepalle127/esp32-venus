// Testfirmware mit Webseite: zeigt, was an den Anschluessen ankommt, bevor die
// Funktionen fuer die Anlage dazukommen. Gesendet wird nur auf ausdruecklichen
// Knopfdruck im RS485-Test. VE.Bus hoert nur zu, CAN ist listen-only, der CH348
// empfaengt nur.
//
// Aufteilung auf die Kerne wie spaeter: Core 1 nur VE.Bus, Core 0 alles andere.

#include <Arduino.h>
#include <Hoymiles.h>
#include <PicoMQTT.h>
#include <Preferences.h>
#include <SpiManager.h>
#include <VEBus.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <ch348.h>
#include <driver/twai.h>
#include <esp_wifi.h>
#include <usb/usb_host.h>

#include "app.h"
#include "log.h"
#include "pins.h"

// Zugangsdaten stehen nicht im Code: wifi_secrets.h.example kopieren und anpassen.
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#define CORE_VEBUS   1
#define CORE_OTHER   0
#define REPORT_MS    60000
#define SENSOR_MS    2000
#define VED_BAUD     19200

VEBus vebus;
static PicoMQTT::Server mqtt;

SmlReader sml_house, sml_wp;
RmtUart rmt_wp;
Bme280 bme[2];
VeDirectParser ved[CH348_NUM_PORTS];
SemaphoreHandle_t ved_mtx;
volatile bool ch348_running;
CanMonitor can_mon;
bool can_ok;
SerialMonitor rs_left;
LeftMode left_mode = LEFT_VEBUS;
bool cmt_found;
Relay relays[2] = {{FAN_RELAY, "Lüfter", -1}, {RELAY2, "Relais 2", -1}};
const char BUILD[] = __DATE__ " " __TIME__;

static uint32_t left_baud = 115200;
static usb_host_client_handle_t usb_client;
static volatile uint8_t usb_new_addr;
static SemaphoreHandle_t usb_ready;
static TaskHandle_t task_net, task_can;

// ---------------------------------------------------------------- Einstellungen

static void settings_load()
{
    Preferences p;
    p.begin("venus", false);    // legt den Bereich beim ersten Start an
    left_mode = (LeftMode)p.getUChar("left", LEFT_VEBUS);
    left_baud = p.getUInt("lbaud", 115200);
    p.end();
}

void left_mode_save(LeftMode mode, uint32_t baud)
{
    Preferences p;
    p.begin("venus", false);
    p.putUChar("left", mode);
    p.putUInt("lbaud", baud);
    p.end();
}

// ---------------------------------------------------------------- Relais

void relay_set(int i, int mode)
{
    relays[i].mode = (int8_t)mode;
    if (mode < 0) {
        pinMode(relays[i].gpio, INPUT);
    } else {
        digitalWrite(relays[i].gpio, mode ? HIGH : LOW);
        pinMode(relays[i].gpio, OUTPUT);
    }
    logf("Relais %s (GPIO %d): %s", relays[i].name, relays[i].gpio, mode < 0 ? "nicht getrieben" : mode ? "high" : "low");
}

// ---------------------------------------------------------------- I2C

int i2c_scan(uint8_t *found, int max)
{
    int n = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0 && n < max) {
            found[n++] = a;
        }
    }
    logf("I2C-Scan: %d Geraet(e)", n);
    return n;
}

void bme_init()
{
    for (int i = 0; i < 2; i++) {
        bool ok = bme[i].begin(Wire, 0x76 + i);
        logf("BME280 0x%02X: %s (Chip-ID 0x%02X)", 0x76 + i, ok ? "gefunden" : "fehlt", bme[i].chip_id);
    }
}

// ---------------------------------------------------------------- CH348

static void on_ch348_rx(uint8_t port, const uint8_t *data, size_t len)
{
    xSemaphoreTake(ved_mtx, portMAX_DELAY);
    ved[port].feed(data, len);
    xSemaphoreGive(ved_mtx);
}

static void usb_lib_task(void *)
{
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void usb_event_cb(const usb_host_client_event_msg_t *msg, void *)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        usb_new_addr = msg->new_dev.address;
        xSemaphoreGive(usb_ready);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        logf("USB-Geraet entfernt, Neustart");
        ESP.restart();
    }
}

static void usb_client_task(void *)
{
    usb_host_client_config_t config = {};
    config.is_synchronous = false;
    config.max_num_event_msg = 5;
    config.async.client_event_callback = usb_event_cb;
    ESP_ERROR_CHECK(usb_host_client_register(&config, &usb_client));

    // Ein Geraet, das vor dem Anmelden schon erkannt war, meldet kein NEW_DEV.
    uint8_t list[4];
    int count = 0;
    if (usb_host_device_addr_list_fill(sizeof(list), list, &count) == ESP_OK && count > 0) {
        usb_new_addr = list[0];
        xSemaphoreGive(usb_ready);
    }
    while (true) {
        usb_host_client_handle_events(usb_client, portMAX_DELAY);
    }
}

// Alle 8 Ports nur empfangen. Gesendet wird an VE.Direct nichts.
static void ch348_task(void *)
{
    // Wird innerhalb von 10 s kein Geraet angemeldet, Port aus und wieder an. Beobachtet am
    // 14.11.2026: Nach einem Software-Neustart sah der Host den CH348 gar nicht, nach einem
    // Reset ueber EN scheiterte die Anmeldung ("USBH: Dev 0 EP 0 Error").
    int tries = 0;
    while (xSemaphoreTake(usb_ready, pdMS_TO_TICKS(10000)) != pdTRUE) {
        tries++;
        logf("USB: kein Geraet angemeldet, Port aus und wieder an (Versuch %d)", tries);
        usb_host_lib_set_root_port_power(false);
        vTaskDelay(pdMS_TO_TICKS(500));
        usb_host_lib_set_root_port_power(true);
    }
    if (tries > 0) {
        logf("USB: Geraet nach %d Versuch(en) angemeldet", tries);
    }
    usb_device_handle_t dev;
    esp_err_t err = usb_host_device_open(usb_client, usb_new_addr, &dev);
    if (err == ESP_OK) {
        err = ch348_open(usb_client, dev, on_ch348_rx);
    }
    for (int p = 0; err == ESP_OK && p < CH348_NUM_PORTS; p++) {
        err = ch348_port_open(p, VED_BAUD);
    }
    if (err != ESP_OK) {
        logf("CH348: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    ch348_running = true;
    const uint8_t *v = ch348_version();
    logf("CH348: 8 Ports mit %d Baud, Version %02X %02X %02X %02X", VED_BAUD, v[0], v[1], v[2], v[3]);
    vTaskDelete(NULL);
}

// Arduino baut den USB-Host mit Enumerationsfilter. Ohne diese Funktion bricht
// ESP-IDF die Erkennung jedes Geraets ab, ohne dass eine Meldung erscheint.
static bool usb_enum_filter(const usb_device_desc_t *, uint8_t *config_value)
{
    *config_value = 1;
    return true;
}

static void start_usb()
{
    ved_mtx = xSemaphoreCreateMutex();
    usb_ready = xSemaphoreCreateBinary();
    usb_host_config_t config = {};
    config.skip_phy_setup = false;
    // Level 1 allein reicht nicht mehr, die anderen Treiber belegen die Eingaenge.
    config.intr_flags = ESP_INTR_FLAG_LOWMED;
    config.enum_filter_cb = usb_enum_filter;
    config.root_port_unpowered = true;    // Port schaltet ch348_task, damit sie ihn neu starten kann
    ESP_ERROR_CHECK(usb_host_install(&config));
    usb_host_lib_set_root_port_power(true);

    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 4, NULL, CORE_OTHER);
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 4096, NULL, 3, NULL, CORE_OTHER);
    xTaskCreatePinnedToCore(ch348_task, "ch348", 4096, NULL, 2, NULL, CORE_OTHER);
}

// ---------------------------------------------------------------- CAN

// Liest einen Pin als Eingang mit Pull-down und mit Pull-up: Wird er von aussen getrieben
// oder ist er offen? Danach ist der Pin zurueckgesetzt und muss neu belegt werden.
const char *pin_probe(int gpio)
{
    gpio_num_t g = (gpio_num_t)gpio;
    gpio_reset_pin(g);
    gpio_set_direction(g, GPIO_MODE_INPUT);
    gpio_set_pull_mode(g, GPIO_PULLDOWN_ONLY);
    delayMicroseconds(200);
    int pd = gpio_get_level(g);
    gpio_set_pull_mode(g, GPIO_PULLUP_ONLY);
    delayMicroseconds(200);
    int pu = gpio_get_level(g);
    gpio_set_pull_mode(g, GPIO_FLOATING);
    return pd && pu ? "high getrieben" : !pd && !pu ? "low getrieben" : !pd && pu ? "offen" : "unklar";
}

static volatile bool can_pause;
static volatile bool can_paused;

static void can_task(void *)
{
    while (true) {
        if (can_pause) {
            can_paused = true;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        can_paused = false;
        twai_message_t msg;
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            can_mon.add(msg.identifier, msg.extd, msg.data_length_code, msg.data);
        }
    }
}

static esp_err_t can_install(twai_mode_t mode)
{
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, mode);
    general.rx_queue_len = 32;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err == ESP_OK) {
        err = twai_start();
    }
    return err;
}

static void start_can()
{
    can_mon.begin();
    // Listen-only: Der S3 quittiert und sendet nichts auf dem Bus.
    esp_err_t err = can_install(TWAI_MODE_LISTEN_ONLY);
    logf("CAN: %s (TX GPIO %d, RX GPIO %d, listen-only)", esp_err_to_name(err), CAN_TX, CAN_RX);
    can_ok = err == ESP_OK;
    if (can_ok) {
        xTaskCreatePinnedToCore(can_task, "can", 3072, NULL, 3, &task_can, CORE_OTHER);
    }
}

// Selbsttest der CAN-Kette: ohne Quittung senden und ueber TJA1050 und ADuM selbst
// empfangen. Sendet auf dem Bus, deshalb nur ohne BMS. Danach wieder listen-only.
void can_selftest(String &out)
{
    char b[200];
    if (!can_ok) {
        out += "{\"err\":\"CAN-Treiber laeuft nicht\"}";
        return;
    }
    can_pause = true;
    for (int i = 0; i < 50 && !can_paused; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Pegel an RX vorher: Bei freiem Bus ist RX high (rezessiv). Liegt er dauerhaft low,
    // sieht der Controller nie einen freien Bus und sendet nicht.
    int rx_high = 0;
    for (int i = 0; i < 2000; i++) {
        rx_high += gpio_get_level((gpio_num_t)CAN_RX);
        delayMicroseconds(5);
    }
    twai_stop();
    twai_driver_uninstall();
    // Beide Pins als Eingang, je einmal mit Pull-down und Pull-up gelesen: getrieben oder offen?
    char pins[2][24];
    strlcpy(pins[0], pin_probe(CAN_TX), sizeof(pins[0]));
    strlcpy(pins[1], pin_probe(CAN_RX), sizeof(pins[1]));
    // Nur senden, wenn RX vom ADuM high getrieben wird und TX offen ist. Wird TX getrieben,
    // haengt er an einem Ausgang (z. B. TX und RX vertauscht), dann wuerden beide gegeneinander treiben.
    bool safe = strcmp(pins[1], "high getrieben") == 0 && strcmp(pins[0], "offen") == 0;
    esp_err_t err = safe ? can_install(TWAI_MODE_NO_ACK) : ESP_ERR_INVALID_STATE;
    int sent = 0, ok = 0, wrong = 0, lost = 0;
    if (err == ESP_OK) {
        for (int n = 0; n < 10; n++) {
            twai_message_t tx = {};
            tx.identifier = 0x7E5;
            tx.self = 1;
            tx.data_length_code = 8;
            for (int k = 0; k < 8; k++) {
                tx.data[k] = (uint8_t)(n * 16 + k * 37);
            }
            if (twai_transmit(&tx, pdMS_TO_TICKS(100)) != ESP_OK) {
                lost++;
                continue;
            }
            sent++;
            twai_message_t rx;
            if (twai_receive(&rx, pdMS_TO_TICKS(100)) != ESP_OK) {
                lost++;
            } else if (rx.identifier == tx.identifier && rx.data_length_code == 8 &&
                       memcmp(rx.data, tx.data, 8) == 0) {
                ok++;
            } else {
                wrong++;
            }
        }
    }
    twai_status_info_t st = {};
    if (err == ESP_OK) {
        twai_get_status_info(&st);
    }
    static const char *states[] = {"gestoppt", "laeuft", "Bus-Off", "Wiederanlauf"};
    snprintf(b, sizeof(b),
             "{\"start\":\"%s\",\"sent\":%d,\"ok\":%d,\"wrong\":%d,\"lost\":%d,\"tx_err\":%lu,\"bus_err\":%lu,"
             "\"state\":\"%s\",\"rx_high\":%d,\"pin_tx\":\"%s\",\"pin_rx\":\"%s\"}",
             esp_err_to_name(err), sent, ok, wrong, lost, (unsigned long)st.tx_error_counter,
             (unsigned long)st.bus_error_count, st.state < 4 ? states[st.state] : "-", rx_high / 20, pins[0], pins[1]);
    out += b;
    logf("CAN-Pins als Eingang: GPIO %d (TX) %s, GPIO %d (RX) %s%s", CAN_TX, pins[0], CAN_RX, pins[1],
         safe ? "" : ", deshalb nicht gesendet");
    logf("CAN-Selbsttest: RX vorher %d %% high; %d gesendet, %d richtig zurueck, %d falsch, %d verloren, "
         "TX-Fehlerzaehler %lu, Busfehler %lu, Zustand %s",
         rx_high / 20, sent, ok, wrong, lost, (unsigned long)st.tx_error_counter, (unsigned long)st.bus_error_count,
         st.state < 4 ? states[st.state] : "-");
    if (err == ESP_OK) {
        twai_stop();
        twai_driver_uninstall();
    }
    err = can_install(TWAI_MODE_LISTEN_ONLY);
    can_ok = err == ESP_OK;
    logf("CAN: wieder listen-only, %s", esp_err_to_name(err));
    can_pause = false;
}

// ---------------------------------------------------------------- Funk

static void start_radio()
{
    SpiManagerInst.register_bus(SPI3_HOST);
    Hoymiles.init();
    Hoymiles.initCMT(CMT_SDIO, CMT_CLK, CMT_CS, CMT_FCS, CMT_GPIO2, CMT_GPIO3);
    Hoymiles.getRadioCmt()->setCountryMode(CountryModeId_t::MODE_EU);
    Hoymiles.setPollInterval(5);
    cmt_found = Hoymiles.getRadioCmt()->isConnected();
    logf("CMT2300A: %s", cmt_found ? "gefunden" : "nicht gefunden");
}

// ---------------------------------------------------------------- Zaehler

static void on_wp_byte(uint8_t b, void *)
{
    sml_wp.feed(&b, 1);
}

// ---------------------------------------------------------------- RGB-LED

#define LED_CYCLE_MS 15000   // ein Umlauf durch das Spektrum
#define LED_MAX      48      // Helligkeit, 255 ist sehr hell

// Langsamer Farbverlauf: Farbton laeuft im Kreis, Saettigung und Helligkeit fest.
static void led_task(void *)
{
    while (true) {
        uint32_t ms = millis() % LED_CYCLE_MS;
        uint32_t pos = ms * 6 * 256 / LED_CYCLE_MS;   // 6 Abschnitte zu je 256 Stufen
        uint8_t up = (uint8_t)((pos & 0xFF) * LED_MAX / 255);
        uint8_t down = LED_MAX - up;
        uint8_t r, g, b;
        switch (pos >> 8) {
        case 0: r = LED_MAX; g = up; b = 0; break;        // rot -> gelb
        case 1: r = down; g = LED_MAX; b = 0; break;      // gelb -> gruen
        case 2: r = 0; g = LED_MAX; b = up; break;        // gruen -> cyan
        case 3: r = 0; g = down; b = LED_MAX; break;      // cyan -> blau
        case 4: r = up; g = 0; b = LED_MAX; break;        // blau -> magenta
        default: r = LED_MAX; g = 0; b = down; break;     // magenta -> rot
        }
        rgbLedWrite(RGB_LED, r, g, b);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

// ---------------------------------------------------------------- Bericht

static void report()
{
    logf("Bericht: Haus %lu Tel. (%lu CRC-Fehler), WP %lu Tel. (%lu CRC, %lu Rahmen), CAN %lu, VE.Bus Sync %s, "
         "Heap %u (min %u), Stack net %u can %u",
         (unsigned long)sml_house.telegrams, (unsigned long)sml_house.crc_errors, (unsigned long)sml_wp.telegrams,
         (unsigned long)sml_wp.crc_errors, (unsigned long)rmt_wp.decoder.framing_errors,
         (unsigned long)can_mon.frames, left_mode == LEFT_VEBUS ? (vebus.hasNoSync() ? "nein" : "ja") : "-",
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
         task_net ? uxTaskGetStackHighWaterMark(task_net) : 0, task_can ? uxTaskGetStackHighWaterMark(task_can) : 0);
}

// ---------------------------------------------------------------- WLAN-Ueberwachung

// Arduino versucht nach einem Fehlschlag nur einmal neu und nach AUTH_FAIL gar nicht
// mehr. Bei schwachem Signal blieb der S3 so dauerhaft offline.
static void wifi_watch()
{
#ifdef WIFI_SSID
    static uint32_t since = millis();
    if (WiFi.status() == WL_CONNECTED) {
        since = millis();
        return;
    }
    if (millis() - since >= 20000) {
        since = millis();
        logf("WLAN: seit 20 s nicht verbunden, neuer Versuch");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
#endif
}

// ---------------------------------------------------------------- Netz-Task

// Alles, was nicht zeitkritisch ist, auf Core 0: MQTT, Funk, Webseite, I2C,
// Zaehler am UART und der RS485-Test.
static void net_task(void *)
{
    mqtt.begin();
    web_begin();
    uint32_t last_report = millis();
    uint32_t last_sensor = 0;
    uint32_t last_bme_init = millis();
    uint8_t buf[128];
    while (true) {
        mqtt.loop();
        Hoymiles.loop();
        wifi_watch();
        web_loop();
        size_t n;
        while ((n = Serial2.read(buf, sizeof(buf))) > 0) {
            sml_house.feed(buf, n);
        }
        if (left_mode == LEFT_RS485) {
            rs_left.poll();
        }
        uint32_t now = millis();
        if (now - last_sensor >= SENSOR_MS) {
            last_sensor = now;
            for (auto &s : bme) {
                s.read();
            }
            // Fehlende Sensoren alle 30 s neu suchen
            if ((!bme[0].ok || !bme[1].ok) && now - last_bme_init >= 30000) {
                last_bme_init = now;
                for (int i = 0; i < 2; i++) {
                    if (!bme[i].ok && bme[i].begin(Wire, 0x76 + i)) {
                        logf("BME280 0x%02X: gefunden", 0x76 + i);
                    }
                }
            }
        }
        if (now - last_report >= REPORT_MS) {
            last_report = now;
            report();
        }
        vTaskDelay(1);
    }
}

// Ein Treiber legt seinen Interrupt auf den Kern, der ihn startet. Alles ausser
// VE.Bus startet deshalb hier auf Core 0.
static void start_task(void *)
{
    // USB zuerst: Der Host braucht einen freien Interrupt-Eingang.
    start_usb();

    Serial2.setRxBufferSize(2048);
    Serial2.begin(9600, SERIAL_8N1, MT681_RX, -1);
    logf("MT681 Haus: UART2, GPIO %d, 9600 Baud", MT681_RX);
    rmt_wp.begin(MT681_WP_RX, 9600, on_wp_byte, NULL, CORE_OTHER);
    // Nach dem RMT-Empfang: Die LED nimmt sich einen RMT-Sendekanal.
    xTaskCreatePinnedToCore(led_task, "led", 3072, NULL, 1, NULL, CORE_OTHER);

    Wire.begin(I2C_SDA, I2C_SCL, 100000);
    bme_init();

    if (left_mode == LEFT_RS485) {
        // TX nur, wenn RX vom ADuM getrieben wird und TX offen ist (sonst vertauscht oder nicht verdrahtet)
        const char *ptx = pin_probe(VEBUS_TX);
        const char *prx = pin_probe(VEBUS_RX);
        bool tx_ok = strcmp(prx, "high getrieben") == 0 && strcmp(ptx, "offen") == 0;
        snprintf(rs_left.pins, sizeof(rs_left.pins), "TX GPIO %d %s, RX GPIO %d %s", VEBUS_TX, ptx, VEBUS_RX, prx);
        rs_left.begin(Serial1, VEBUS_RX, tx_ok ? VEBUS_TX : -1, left_baud);
        logf("Linker Kanal: RS485-Test, %lu Baud, %s, Senden %s", (unsigned long)left_baud, rs_left.pins,
             tx_ok ? "frei" : "gesperrt");
    }

    start_can();
    start_radio();

    // Auch ohne Zugangsdaten noetig: Der MQTT-Broker braucht den TCP/IP-Stack.
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
            wifi_bandwidth_t bw = WIFI_BW_HT20;
            uint8_t proto = 0;
            esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
            esp_wifi_get_protocol(WIFI_IF_STA, &proto);
            const uint8_t *m = info.wifi_sta_connected.bssid;
            logf("WLAN: verbunden mit %02X:%02X:%02X:%02X:%02X:%02X, Kanal %u, Breite %s, Protokolle 0x%02X", m[0], m[1],
                 m[2], m[3], m[4], m[5], info.wifi_sta_connected.channel, bw == WIFI_BW_HT40 ? "40 MHz" : "20 MHz",
                 proto);
        } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            logf("WLAN: getrennt, Grund %u (%s), RSSI %d dBm", info.wifi_sta_disconnected.reason,
                 WiFi.disconnectReasonName((wifi_err_reason_t)info.wifi_sta_disconnected.reason),
                 info.wifi_sta_disconnected.rssi);
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            logf("WLAN: IP %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
    });
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("esp32-venus");
    // Ohne Stromsparmodus. Mit Modem-Sleep antwortete der S3 im Mittel nach 100 ms,
    // in der Spitze nach 500 ms; ohne nach 14 ms wie der Router.
    WiFi.setSleep(false);
    // Im Mesh gibt es die SSID mehrfach. Arduino nimmt sonst den ersten Zugangspunkt, den
    // die schnelle Suche findet: Der S3 hing an einem mit -75 dBm, ein anderer hatte -48 dBm.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#ifdef WIFI_SSID
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    logf("WLAN: verbinde mit %s", WIFI_SSID);
#else
    logf("WLAN: keine Zugangsdaten, src/wifi_secrets.h fehlt");
#endif

    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 1, &task_net, CORE_OTHER);
    vTaskDelete(NULL);
}

void setup()
{
    Serial.begin(115200);
    log_init();
    logf("esp32-venus Testfirmware, %s, Chip %s, PSRAM %u Bytes, Flash %u Bytes", BUILD,
         ESP.getChipModel(), ESP.getPsramSize(), ESP.getFlashChipSize());
    settings_load();

    // setup() laeuft auf Core 1. Hier startet deshalb nur VE.Bus, damit sein
    // UART-Interrupt auf diesem Kern liegt. Im RS485-Test bleibt VE.Bus aus.
    if (left_mode == LEFT_VEBUS) {
        vebus.begin(VEBUS_RX, VEBUS_TX, VEBUS_DE, CORE_VEBUS);
        vebus.setTxEnabled(false);
        logf("VE.Bus: nur hoeren, RX GPIO %d", VEBUS_RX);
    }

    xTaskCreatePinnedToCore(start_task, "start", 8192, NULL, 2, NULL, CORE_OTHER);
}

void loop()
{
    // Die Arduino-Loop laeuft auf Core 1, dort soll nur VE.Bus laufen.
    vTaskDelete(NULL);
}
