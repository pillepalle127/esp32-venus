// ch348: Treiber fuer den USB-zu-8-UART-Baustein CH348 am USB-Host des ESP32-S3.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb/usb_host.h"

#define CH348_NUM_PORTS 8
#define CH348_VID       0x1a86
#define CH348_PID       0x55d9

#ifdef __cplusplus
extern "C" {
#endif

// Laeuft in der Task, die usb_host_client_handle_events() aufruft. Muss kurz bleiben.
typedef void (*ch348_rx_cb_t)(uint8_t port, const uint8_t *data, size_t len);

// Belegt das Interface und startet den Empfang. Die Client-Task muss dabei
// weiterlaufen, deshalb nicht aus ihr heraus aufrufen. Das gilt auch fuer die
// anderen Funktionen.
esp_err_t ch348_open(usb_host_client_handle_t client, usb_device_handle_t dev, ch348_rx_cb_t rx_cb);

// Stellt einen Port auf 8N1 mit der angegebenen Baudrate ein.
esp_err_t ch348_port_open(uint8_t port, uint32_t baud);

// Blockiert, bis alle Daten an den CH348 uebergeben sind.
esp_err_t ch348_write(uint8_t port, const uint8_t *data, size_t len);

// Antwort auf die Versionsabfrage (4 Bytes), Bloecke mit ungueltigem Kopf,
// Meldungen auf dem Status-Endpunkt.
const uint8_t *ch348_version(void);
uint32_t ch348_bad_blocks(void);
uint32_t ch348_status_msgs(void);

#ifdef __cplusplus
}
#endif
