// test_ch348: Schleifentest ueber alle 8 Ports des CH348 am USB-Host des ESP32-S3.
//
// An jedem Port muss TX mit RX verbunden sein. Jeder Port sendet ein eigenes
// Bytemuster, der Empfang prueft jedes Byte. Jede Minute pausiert das Senden
// kurz, dann muss auf jedem Port genau so viel angekommen sein wie gesendet.
//
// Der native USB-Port ist Host, die Konsole laeuft ueber UART0 (COM-Port).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ch348.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define BAUD         19200
#define FRAME_LEN    50     // 8 Ports je 50 Bytes alle 40 ms: 65 % der Leitung
#define CYCLE_MS     40
#define CHECK_S      60     // so oft pausieren und vergleichen
#define DRAIN_MS     500

typedef struct {
    uint32_t tx;
    volatile uint32_t rx;
    volatile uint32_t errors;
    uint8_t rx_next;
} port_stat_t;

static port_stat_t stats[CH348_NUM_PORTS];

static usb_host_client_handle_t client;
static volatile uint8_t new_dev_addr;
static SemaphoreHandle_t dev_ready;

static uint8_t pattern(uint8_t port, uint32_t n)
{
    return (uint8_t)(n + port * 37);
}

// Fehlt ein Byte oder kommt ein falsches, zaehlt das einmal, dann folgt die
// Pruefung wieder dem Empfangenen. Kaeme etwas vom falschen Port, faellt das am
// Versatz des Musters auf.
static void on_rx(uint8_t port, const uint8_t *data, size_t len)
{
    port_stat_t *s = &stats[port];
    for (size_t i = 0; i < len; i++) {
        if (data[i] != s->rx_next) {
            s->errors = s->errors + 1;
        }
        s->rx_next = data[i] + 1;
        s->rx = s->rx + 1;
    }
}

static void host_lib_task(void *arg)
{
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        new_dev_addr = msg->new_dev.address;
        xSemaphoreGive(dev_ready);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        // Im Test reicht ein Neustart, der Treiber kennt noch kein Abstecken.
        printf("Geraet entfernt, Neustart\n");
        esp_restart();
    }
}

static void client_task(void *arg)
{
    const usb_host_client_config_t config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&config, &client));
    while (true) {
        usb_host_client_handle_events(client, portMAX_DELAY);
    }
}

static void print_check(uint32_t minute)
{
    uint32_t tx = 0, rx = 0, errors = 0;
    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        port_stat_t *s = &stats[p];
        printf("Port %d: gesendet %lu, empfangen %lu, Fehler %lu, fehlt %ld\n", p,
               (unsigned long)s->tx, (unsigned long)s->rx, (unsigned long)s->errors,
               (long)(s->tx - s->rx));
        tx += s->tx;
        rx += s->rx;
        errors += s->errors;
    }
    printf("ERGEBNIS: Minute %lu, gesendet %lu, empfangen %lu, Fehler %lu, ungueltige Bloecke %lu, "
           "Status-Meldungen %lu\n",
           (unsigned long)minute, (unsigned long)tx, (unsigned long)rx, (unsigned long)errors,
           (unsigned long)ch348_bad_blocks(), (unsigned long)ch348_status_msgs());
}

static void test_task(void *arg)
{
    usb_device_handle_t dev;
    while (true) {
        xSemaphoreTake(dev_ready, portMAX_DELAY);

        const usb_device_desc_t *desc;
        usb_device_info_t info;
        ESP_ERROR_CHECK(usb_host_device_open(client, new_dev_addr, &dev));
        ESP_ERROR_CHECK(usb_host_get_device_descriptor(dev, &desc));
        ESP_ERROR_CHECK(usb_host_device_info(dev, &info));
        printf("Geraet %04x:%04x, %s\n", desc->idVendor, desc->idProduct,
               info.speed == USB_SPEED_FULL ? "Full Speed" : "andere Geschwindigkeit");
        if (desc->idVendor == CH348_VID && desc->idProduct == CH348_PID) {
            break;
        }
        printf("Kein CH348\n");
        ESP_ERROR_CHECK(usb_host_device_close(client, dev));
    }

    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        stats[p].rx_next = pattern(p, 0);
    }

    esp_err_t err = ch348_open(client, dev, on_rx);
    if (err != ESP_OK) {
        printf("ERGEBNIS: ch348_open fehlgeschlagen: %s\n", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    const uint8_t *v = ch348_version();
    printf("Version: %02x %02x %02x %02x\n", v[0], v[1], v[2], v[3]);

    for (int p = 0; p < CH348_NUM_PORTS; p++) {
        err = ch348_port_open(p, BAUD);
        if (err != ESP_OK) {
            printf("ERGEBNIS: Port %d einstellen fehlgeschlagen: %s\n", p, esp_err_to_name(err));
            vTaskDelete(NULL);
        }
    }
    printf("Alle Ports auf %d Baud, Schleifentest laeuft\n", BAUD);

    uint32_t minute = 0;
    int64_t next_check = esp_timer_get_time() + CHECK_S * 1000000LL;
    TickType_t wake = xTaskGetTickCount();
    uint8_t frame[FRAME_LEN];
    while (true) {
        for (int p = 0; p < CH348_NUM_PORTS; p++) {
            port_stat_t *s = &stats[p];
            for (int i = 0; i < FRAME_LEN; i++) {
                frame[i] = pattern(p, s->tx + i);
            }
            err = ch348_write(p, frame, FRAME_LEN);
            if (err != ESP_OK) {
                printf("ERGEBNIS: Senden auf Port %d fehlgeschlagen: %s\n", p, esp_err_to_name(err));
                vTaskDelete(NULL);
            }
            s->tx += FRAME_LEN;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CYCLE_MS));

        if (esp_timer_get_time() >= next_check) {
            vTaskDelay(pdMS_TO_TICKS(DRAIN_MS));
            minute++;
            print_check(minute);
            next_check = esp_timer_get_time() + CHECK_S * 1000000LL;
            wake = xTaskGetTickCount();
        }
    }
}

void app_main(void)
{
    printf("test_ch348: warte auf CH348 am nativen USB-Port\n");

    dev_ready = xSemaphoreCreateBinary();

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    xTaskCreate(host_lib_task, "usb_lib", 4096, NULL, 4, NULL);
    xTaskCreate(client_task, "usb_client", 4096, NULL, 3, NULL);
    xTaskCreate(test_task, "test", 4096, NULL, 2, NULL);
}
