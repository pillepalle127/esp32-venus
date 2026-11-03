// ch348: Treiber fuer den USB-zu-8-UART-Baustein CH348 am USB-Host des ESP32-S3.
//
// Fuer den CH348 gibt es kein oeffentliches Protokoll. Die Befehle stammen aus
// dem Linux-Treiber von WCH (ch9344ser_linux, GPL-2.0):
//
// - Endpunkte in der Reihenfolge des Deskriptors: Daten IN, Daten OUT, Status
//   IN, Befehle OUT.
// - Empfang: Bloecke zu 32 Bytes, [Port, Laenge (bis 30), Daten...].
// - Senden: je USB-Paket [Port, Laenge low, Laenge high, Daten (bis 61)...].
// - Es laeuft immer nur eine Uebertragung je Richtung. Parallele Uebertragungen
//   funktionieren laut Linux-Patch nicht.

#include "ch348.h"

#include <stdbool.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usb/usb_helpers.h"

#define MPS            64   // Full Speed
#define RX_BLOCK       32
#define RX_MAX_DATA    30
#define TX_HEADER      3
#define TX_MAX_DATA    (MPS - TX_HEADER)
#define TIMEOUT_MS     1000

#define CMD_W_R        0xC0
#define CMD_W_BR       0x80
#define CMD_WB_E       0x90
#define CMD_VER        0x96
#define R_C1           0x01
#define R_C2           0x02
#define R_C4           0x04
#define R_INIT         0xA1

static usb_host_client_handle_t client;
static usb_device_handle_t dev;
static ch348_rx_cb_t rx_cb;

static usb_transfer_t *tx_xfer, *cmd_xfer, *rx_xfer, *status_xfer, *ctrl_xfer;
static SemaphoreHandle_t tx_done, cmd_done, ctrl_done;
static SemaphoreHandle_t tx_lock, cmd_lock;

static uint8_t version[4];
static volatile uint32_t bad_blocks;
static volatile uint32_t status_msgs;

static void give_done_cb(usb_transfer_t *x)
{
    xSemaphoreGive((SemaphoreHandle_t)x->context);
}

static void rx_done_cb(usb_transfer_t *x)
{
    if (x->status == USB_TRANSFER_STATUS_COMPLETED) {
        const uint8_t *buf = x->data_buffer;
        for (int i = 0; i + 2 <= x->actual_num_bytes; i += RX_BLOCK) {
            uint8_t port = buf[i];
            uint8_t len = buf[i + 1];
            if (port >= CH348_NUM_PORTS || len > RX_MAX_DATA || i + 2 + len > x->actual_num_bytes) {
                bad_blocks = bad_blocks + 1;
                continue;
            }
            if (len > 0) {
                rx_cb(port, buf + i + 2, len);
            }
        }
    }
    if (x->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        usb_host_transfer_submit(x);
    }
}

static void status_done_cb(usb_transfer_t *x)
{
    if (x->status == USB_TRANSFER_STATUS_COMPLETED && x->actual_num_bytes > 0) {
        status_msgs = status_msgs + 1;
    }
    if (x->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        usb_host_transfer_submit(x);
    }
}

static esp_err_t transfer_new(size_t size, uint8_t ep, usb_transfer_cb_t cb, void *context,
                              usb_transfer_t **out)
{
    esp_err_t err = usb_host_transfer_alloc(size, 0, out);
    if (err != ESP_OK) {
        return err;
    }
    (*out)->device_handle = dev;
    (*out)->bEndpointAddress = ep;
    (*out)->callback = cb;
    (*out)->context = context;
    return ESP_OK;
}

static esp_err_t wait_done(usb_transfer_t *x, SemaphoreHandle_t done)
{
    if (xSemaphoreTake(done, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return x->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_out(usb_transfer_t *x, SemaphoreHandle_t done, size_t len)
{
    x->num_bytes = len;
    esp_err_t err = usb_host_transfer_submit(x);
    if (err != ESP_OK) {
        return err;
    }
    return wait_done(x, done);
}

static esp_err_t cmd_send(const uint8_t *data, size_t len)
{
    xSemaphoreTake(cmd_lock, portMAX_DELAY);
    memcpy(cmd_xfer->data_buffer, data, len);
    esp_err_t err = send_out(cmd_xfer, cmd_done, len);
    xSemaphoreGive(cmd_lock);
    return err;
}

static esp_err_t cmd3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t buf[3] = {a, b, c};
    return cmd_send(buf, sizeof(buf));
}

static esp_err_t read_version(void)
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl_xfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_VENDOR |
                           USB_BM_REQUEST_TYPE_RECIP_DEVICE;
    setup->bRequest = CMD_VER;
    setup->wValue = 0;
    setup->wIndex = 0;
    setup->wLength = sizeof(version);
    ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + sizeof(version);

    esp_err_t err = usb_host_transfer_submit_control(client, ctrl_xfer);
    if (err == ESP_OK) {
        err = wait_done(ctrl_xfer, ctrl_done);
    }
    if (err == ESP_OK) {
        memcpy(version, ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t), sizeof(version));
    }
    return err;
}

esp_err_t ch348_open(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl,
                     ch348_rx_cb_t cb)
{
    client = client_hdl;
    dev = dev_hdl;
    rx_cb = cb;

    const usb_config_desc_t *cfg;
    esp_err_t err = usb_host_get_active_config_descriptor(dev, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    int offset = 0;
    const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cfg, 0, 0, &offset);
    if (intf == NULL || intf->bNumEndpoints != 4) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t data_in = 0, data_out = 0, status_in = 0, cmd_out = 0;
    for (int i = 0; i < 4; i++) {
        int ep_offset = offset;
        const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(intf, i, cfg->wTotalLength, &ep_offset);
        if (ep == NULL) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        bool in = ep->bEndpointAddress & 0x80;
        if (i < 2) {
            *(in ? &data_in : &data_out) = ep->bEndpointAddress;
        } else {
            *(in ? &status_in : &cmd_out) = ep->bEndpointAddress;
        }
    }
    if (!data_in || !data_out || !status_in || !cmd_out) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    err = usb_host_interface_claim(client, dev, intf->bInterfaceNumber, 0);
    if (err != ESP_OK) {
        return err;
    }

    tx_done = xSemaphoreCreateBinary();
    cmd_done = xSemaphoreCreateBinary();
    ctrl_done = xSemaphoreCreateBinary();
    tx_lock = xSemaphoreCreateMutex();
    cmd_lock = xSemaphoreCreateMutex();

    if ((err = transfer_new(MPS, data_out, give_done_cb, tx_done, &tx_xfer)) != ESP_OK ||
        (err = transfer_new(MPS, cmd_out, give_done_cb, cmd_done, &cmd_xfer)) != ESP_OK ||
        (err = transfer_new(MPS, data_in, rx_done_cb, NULL, &rx_xfer)) != ESP_OK ||
        (err = transfer_new(MPS, status_in, status_done_cb, NULL, &status_xfer)) != ESP_OK ||
        (err = transfer_new(sizeof(usb_setup_packet_t) + sizeof(version), 0, give_done_cb, ctrl_done, &ctrl_xfer)) != ESP_OK) {
        return err;
    }

    err = read_version();
    if (err != ESP_OK) {
        return err;
    }

    rx_xfer->num_bytes = MPS;
    status_xfer->num_bytes = MPS;
    err = usb_host_transfer_submit(rx_xfer);
    if (err == ESP_OK) {
        err = usb_host_transfer_submit(status_xfer);
    }
    return err;
}

static uint8_t port_reg(uint8_t port)
{
    return port < 4 ? 0x10 * port : 0x10 * (port - 4) + 0x08;
}

// Der CH348 erwartet Baudrate und Format verschleiert: um rol Bits rotiert und
// mit xor verknuepft.
static void scramble(uint8_t *b, uint8_t rol, uint8_t xor_val)
{
    for (uint8_t r = 0; r < rol; r++) {
        uint8_t first = b[0];
        for (int i = 0; i < 7; i++) {
            b[i] = (b[i] << 1) | (b[i + 1] >> 7);
        }
        b[7] = (b[7] << 1) | (first >> 7);
    }
    for (int i = 0; i < 8; i++) {
        b[i] ^= xor_val;
    }
}

esp_err_t ch348_port_open(uint8_t port, uint32_t baud)
{
    if (port >= CH348_NUM_PORTS || baud == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t reg = port_reg(port);

    esp_err_t err = cmd3(CMD_W_R, reg + R_C2, 0x87);
    if (err == ESP_OK) {
        err = cmd3(CMD_W_R, reg + R_C4, 0x08);
    }
    if (err == ESP_OK) {
        err = cmd3(CMD_W_BR, reg + R_C4, 0x50);    // normaler UART, ohne Flusskontrolle
    }
    if (err != ESP_OK) {
        return err;
    }

    uint32_t rnd = esp_random();
    uint8_t rol = rnd & 0x0F;
    uint8_t xor_val = (rnd >> 8) & 0xFF;
    uint32_t rx_timeout = (baud >= 921600) ? 5 : 15000000 / baud / 100 + 1;

    uint8_t buf[12] = {
        CMD_WB_E | port,
        R_INIT,
        port | (rol << 4),
        baud >> 24,
        baud >> 16,
        baud >> 8,
        baud,
        0x00,       // 1 Stoppbit
        0x00,       // keine Paritaet
        8,          // Datenbits
        rx_timeout,
        xor_val,
    };
    scramble(buf + 3, rol, xor_val);
    err = cmd_send(buf, sizeof(buf));
    if (err == ESP_OK) {
        err = cmd3(CMD_W_R, reg + R_C1, 0x07);
    }
    if (err == ESP_OK) {
        err = cmd3(CMD_W_BR, reg + R_C4, 0x00);    // uebernimmt die Baudrate
    }
    return err;
}

esp_err_t ch348_write(uint8_t port, const uint8_t *data, size_t len)
{
    if (port >= CH348_NUM_PORTS) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    xSemaphoreTake(tx_lock, portMAX_DELAY);
    while (len > 0 && err == ESP_OK) {
        size_t n = len > TX_MAX_DATA ? TX_MAX_DATA : len;
        uint8_t *buf = tx_xfer->data_buffer;
        buf[0] = port;
        buf[1] = n & 0xFF;
        buf[2] = n >> 8;
        memcpy(buf + TX_HEADER, data, n);
        err = send_out(tx_xfer, tx_done, TX_HEADER + n);
        data += n;
        len -= n;
    }
    xSemaphoreGive(tx_lock);
    return err;
}

const uint8_t *ch348_version(void)
{
    return version;
}

uint32_t ch348_bad_blocks(void)
{
    return bad_blocks;
}

uint32_t ch348_status_msgs(void)
{
    return status_msgs;
}
