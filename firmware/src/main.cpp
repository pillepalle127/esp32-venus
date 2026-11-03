// Gesamtbuild: Alle Bibliotheken laufen in einer Firmware (PLAN.md, Phase 1,
// Punkt 3). Noch keine Funktion fuer die Anlage. VE.Bus sendet nichts.
//
// Die Aufteilung auf die Kerne ist schon die spaetere: Core 1 nur VE.Bus,
// Core 0 alles andere. Als Last laeuft der Schleifentest ueber den CH348.
// Fehlende Hardware (CMT2300A, Transceiver) stoert den Start nicht.

#include <Arduino.h>
#include <Hoymiles.h>
#include <PicoMQTT.h>
#include <Preferences.h>
#include <SpiManager.h>
#include <VEBus.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ch348.h>
#include <driver/twai.h>
#include <usb/usb_host.h>

#include "pins.h"

// Zugangsdaten stehen nicht im Code: wifi_secrets.h.example kopieren und anpassen.
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#define CORE_VEBUS   1
#define CORE_OTHER   0
#define REPORT_MS    30000

#define LOOP_BAUD    19200
#define LOOP_FRAME   50
#define LOOP_MS      40
#define LOOP_DRAIN   500

static VEBus vebus;
static PicoMQTT::Server mqtt;

static usb_host_client_handle_t usb_client;
static volatile uint8_t usb_new_addr;
static SemaphoreHandle_t usb_ready;

static uint32_t loop_tx[CH348_NUM_PORTS];
static volatile uint32_t loop_rx[CH348_NUM_PORTS];
static volatile uint32_t loop_errors;
static volatile uint32_t loop_port_errors[CH348_NUM_PORTS];
static uint8_t loop_next[CH348_NUM_PORTS];
static volatile bool ch348_running;

static volatile uint32_t mt681_bytes;
static volatile uint32_t can_frames;

static TaskHandle_t task_loop, task_net, task_can;

static void on_ch348_rx(uint8_t port, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (data[i] != loop_next[port]) {
            loop_errors = loop_errors + 1;
            loop_port_errors[port] = loop_port_errors[port] + 1;
        }
        loop_next[port] = data[i] + 1;
    }
    loop_rx[port] = loop_rx[port] + len;
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
        Serial.println("USB-Geraet entfernt, Neustart");
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

// Schleifentest wie in test_ch348: 8 Ports, je 50 Bytes alle 40 ms.
static void ch348_loop_task(void *)
{
    xSemaphoreTake(usb_ready, portMAX_DELAY);
    usb_device_handle_t dev;
    ESP_ERROR_CHECK(usb_host_device_open(usb_client, usb_new_addr, &dev));

    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        loop_next[p] = p * 37;
    }
    esp_err_t err = ch348_open(usb_client, dev, on_ch348_rx);
    for (int p = 0; err == ESP_OK && p < CH348_NUM_PORTS; p++) {
        err = ch348_port_open(p, LOOP_BAUD);
    }
    if (err != ESP_OK) {
        Serial.printf("CH348: %s\n", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    ch348_running = true;

    uint8_t frame[LOOP_FRAME];
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        for (int p = 0; p < CH348_NUM_PORTS; p++) {
            for (int i = 0; i < LOOP_FRAME; i++) {
                frame[i] = (uint8_t)(loop_tx[p] + i + p * 37);
            }
            err = ch348_write(p, frame, LOOP_FRAME);
            if (err != ESP_OK) {
                Serial.printf("CH348 senden, Port %d: %s\n", p, esp_err_to_name(err));
                vTaskDelete(NULL);
            }
            loop_tx[p] += LOOP_FRAME;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(LOOP_MS));
    }
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
    usb_ready = xSemaphoreCreateBinary();
    usb_host_config_t config = {};
    config.skip_phy_setup = false;
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    config.enum_filter_cb = usb_enum_filter;
    ESP_ERROR_CHECK(usb_host_install(&config));

    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 4, NULL, CORE_OTHER);
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 4096, NULL, 3, NULL, CORE_OTHER);
    xTaskCreatePinnedToCore(ch348_loop_task, "ch348_loop", 4096, NULL, 2, &task_loop, CORE_OTHER);
}

static void can_task(void *)
{
    while (true) {
        twai_message_t msg;
        if (twai_receive(&msg, portMAX_DELAY) == ESP_OK) {
            can_frames = can_frames + 1;
        }
    }
}

static void start_can()
{
    // Listen-only: Der S3 quittiert und sendet nichts auf dem Bus.
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err == ESP_OK) {
        err = twai_start();
    }
    Serial.printf("CAN: %s\n", esp_err_to_name(err));
    if (err == ESP_OK) {
        xTaskCreatePinnedToCore(can_task, "can", 3072, NULL, 3, &task_can, CORE_OTHER);
    }
}

static void start_radio()
{
    SpiManagerInst.register_bus(SPI3_HOST);
    Hoymiles.init();
    Hoymiles.initCMT(CMT_SDIO, CMT_CLK, CMT_CS, CMT_FCS, CMT_GPIO2, CMT_GPIO3);
    Hoymiles.getRadioCmt()->setCountryMode(CountryModeId_t::MODE_EU);
    Hoymiles.setPollInterval(5);
    Serial.printf("CMT2300A: %s\n", Hoymiles.getRadioCmt()->isConnected() ? "gefunden" : "nicht gefunden");
}

#ifdef VEBUS_TIMING_TEST
static uint32_t load_udp_sent;
static uint32_t load_count;
#endif

// Rechenlast seit dem letzten Aufruf: je Kern und je Task, aus den Laufzeitzaehlern
// von FreeRTOS. Ein Kern ist so weit ausgelastet, wie sein Idle-Task nicht laeuft.
#define LOAD_MAX_TASKS 40

static void report_cpu_load()
{
    static TaskStatus_t now[LOAD_MAX_TASKS];
    static struct { TaskHandle_t handle; configRUN_TIME_COUNTER_TYPE runtime; } last[LOAD_MAX_TASKS];
    static int last_count;
    static configRUN_TIME_COUNTER_TYPE last_total;

    configRUN_TIME_COUNTER_TYPE total = 0;
    int count = uxTaskGetSystemState(now, LOAD_MAX_TASKS, &total);
    configRUN_TIME_COUNTER_TYPE span = total - last_total;
    if (last_total != 0 && span > 0) {
        Serial.print("  Last:");
        for (int i = 0; i < count; i++) {
            configRUN_TIME_COUNTER_TYPE before = 0;
            for (int j = 0; j < last_count; j++) {
                if (last[j].handle == now[i].xHandle) {
                    before = last[j].runtime;
                    break;
                }
            }
            float percent = 100.0f * (float)(now[i].ulRunTimeCounter - before) / (float)span;
            if (percent >= 0.1f) {
                Serial.printf(" %s %.1f", now[i].pcTaskName, percent);
            }
        }
        Serial.println(" (% eines Kerns)");
    }
    last_count = count;
    last_total = total;
    for (int i = 0; i < count; i++) {
        last[i].handle = now[i].xHandle;
        last[i].runtime = now[i].ulRunTimeCounter;
    }
}

static void report()
{
    report_cpu_load();
    uint32_t tx = 0, rx = 0;
    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        tx += loop_tx[p];
        rx += loop_rx[p];
    }
    Serial.printf("t=%lus | CH348 %s tx=%lu rx=%lu fehler=%lu bloecke=%lu | VE.Bus sync=%s | MT681 %lu Bytes | CAN %lu | WLAN %s\n",
                  (unsigned long)(millis() / 1000), ch348_running ? "an" : "aus", (unsigned long)tx,
                  (unsigned long)rx, (unsigned long)loop_errors, (unsigned long)ch348_bad_blocks(),
                  vebus.hasNoSync() ? "nein" : "ja", (unsigned long)mt681_bytes,
                  (unsigned long)can_frames,
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "getrennt");
    Serial.print("  CH348 fehlt je Port:");
    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        Serial.printf(" %ld", (long)(loop_tx[p] - loop_rx[p]));
    }
    Serial.printf(", Fehler je Port:");
    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        Serial.printf(" %lu", (unsigned long)loop_port_errors[p]);
    }
    Serial.println();
    usb_host_lib_info_t usb_info = {};
    usb_host_lib_info(&usb_info);
    Serial.printf("  USB: Geraete %d, Clients %d, VE.Bus Pruefsummenfehler %lu\n", usb_info.num_devices,
                  usb_info.num_clients, (unsigned long)vebus.getChecksumFaults());
#ifdef VEBUS_TIMING_TEST
    Serial.printf("  Last: %lu UDP-Pakete, %lu MQTT-Nachrichten, WLAN %d dBm\n", (unsigned long)load_udp_sent,
                  (unsigned long)load_count, WiFi.RSSI());
#endif
    Serial.printf("  Heap intern frei %u (min %u), PSRAM frei %u, Stack-Reserve loop=%u net=%u can=%u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  task_loop ? uxTaskGetStackHighWaterMark(task_loop) : 0,
                  task_net ? uxTaskGetStackHighWaterMark(task_net) : 0,
                  task_can ? uxTaskGetStackHighWaterMark(task_can) : 0);
}

#ifdef VEBUS_TIMING_TEST
// Last fuer den Timing-Test: MQTT-Nachrichten, WLAN-Verkehr und regelmaessig
// Schreiben in den Flash, das haelt beide Kerne kurz an. Jeder Sync bekommt einen
// Sollwert. Die UDP-Pakete gehen an den Discard-Port des Routers und werden dort
// verworfen.
#define LOAD_MQTT_MS    10
#define LOAD_FLASH_MS   10000

static Preferences load_prefs;
static WiFiUDP load_udp;
static uint8_t load_udp_data[200];
static uint32_t load_mqtt_last;
static uint32_t load_flash_last;

// Eigener Task, damit die Statusausgabe den Sollwert nicht aufhaelt.
static void timing_setpoint_task(void *)
{
    while (true) {
        vebus.setESSPower((int16_t)(load_count % 1000));
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void timing_load()
{
    uint32_t now = millis();
    if (now - load_mqtt_last >= LOAD_MQTT_MS) {
        load_mqtt_last = now;
        load_count++;
        char topic[48], payload[32];
        snprintf(topic, sizeof(topic), "N/test/solarcharger/%lu/Dc/0/Power", (unsigned long)(load_count % 40));
        snprintf(payload, sizeof(payload), "{\"value\":%lu}", (unsigned long)load_count);
        mqtt.publish(topic, payload);
        if (WiFi.status() == WL_CONNECTED) {
            load_udp.beginPacket(WiFi.gatewayIP(), 9);
            load_udp.write(load_udp_data, sizeof(load_udp_data));
            if (load_udp.endPacket()) {
                load_udp_sent++;
            }
        }
    }
    if (now - load_flash_last >= LOAD_FLASH_MS) {
        load_flash_last = now;
        load_prefs.putUInt("count", load_count);
    }
}
#endif

// Alles, was nicht zeitkritisch ist, auf Core 0.
static void net_task(void *)
{
    mqtt.begin();
#ifdef VEBUS_TIMING_TEST
    load_prefs.begin("timing", false);
    xTaskCreatePinnedToCore(timing_setpoint_task, "setpoint", 2048, NULL, 3, NULL, CORE_OTHER);
#endif
    uint32_t last_report = millis();
    while (true) {
        mqtt.loop();
        Hoymiles.loop();
#ifdef VEBUS_TIMING_TEST
        timing_load();
#endif
        while (Serial2.available()) {
            Serial2.read();
            mt681_bytes = mt681_bytes + 1;
        }
        if (millis() - last_report >= REPORT_MS) {
            last_report = millis();
            report();
        }
        vTaskDelay(1);
    }
}

// Ein Treiber legt seinen Interrupt auf den Kern, der ihn startet. Alles ausser
// VE.Bus startet deshalb hier auf Core 0.
static void start_task(void *)
{
    Serial2.begin(9600, SERIAL_8N1, MT681_RX, -1);

    start_can();
    start_radio();

    // Auch ohne Zugangsdaten noetig: Der MQTT-Broker braucht den TCP/IP-Stack.
    WiFi.mode(WIFI_STA);
#ifdef WIFI_SSID
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("WLAN: verbinde");
#else
    Serial.println("WLAN: keine Zugangsdaten, src/wifi_secrets.h fehlt");
#endif

    start_usb();
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 1, &task_net, CORE_OTHER);
    vTaskDelete(NULL);
}

void setup()
{
    Serial.begin(115200);
    Serial.printf("\nesp32-venus Gesamtbuild, Chip %s, PSRAM %u Bytes, Flash %u Bytes\n",
                  ESP.getChipModel(), ESP.getPsramSize(), ESP.getFlashChipSize());

    // setup() laeuft auf Core 1. Hier startet deshalb nur VE.Bus, damit sein
    // UART-Interrupt auf diesem Kern liegt.
    vebus.begin(VEBUS_RX, VEBUS_TX, VEBUS_DE, CORE_VEBUS);
#ifdef VEBUS_TIMING_TEST
    vebus.setTxEnabled(true);
    Serial.println("TIMING-TEST: VE.Bus sendet, nur mit der Simulation verbinden");
#else
    vebus.setTxEnabled(false);
#endif

    xTaskCreatePinnedToCore(start_task, "start", 8192, NULL, 2, NULL, CORE_OTHER);
}

void loop()
{
    // Die Arduino-Loop laeuft auf Core 1, dort soll nur VE.Bus laufen.
    vTaskDelete(NULL);
}
