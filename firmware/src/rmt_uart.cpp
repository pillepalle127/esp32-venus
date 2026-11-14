#include "rmt_uart.h"

void UartBitDecoder::begin(uint32_t baud, byte_cb_t c, void *x)
{
    bit_x100 = 100000000UL / baud;
    cb = c;
    ctx = x;
    in_frame = false;
    t = 0;
}

// Wie ein UART: Die fallende Flanke des Startbits setzt den Takt, dann wird jedes
// Bit in seiner Mitte abgetastet. Abweichungen der Baudrate summieren sich so nur
// ueber ein Zeichen.
void UartBitDecoder::run(uint8_t level, uint32_t us)
{
    uint64_t start = t;
    uint64_t end = t + (uint64_t)us * 100;
    if (!in_frame && level == 0 && (uint64_t)us * 100 >= bit_x100 / 2) {
        in_frame = true;
        next = start + bit_x100 + bit_x100 / 2;   // Mitte des ersten Datenbits
        count = 0;
        value = 0;
    }
    while (in_frame && next < end) {
        if (count < 8) {
            value |= (uint8_t)((level ? 1 : 0) << count);
            count++;
            next += bit_x100;
            continue;
        }
        in_frame = false;              // Stoppbit
        if (level) {
            bytes++;
            if (cb) {
                cb(value, ctx);
            }
        } else {
            framing_errors++;
        }
    }
    t = end;
}

void UartBitDecoder::idle()
{
    // Ruhe ist high: ein offenes Zeichen bekommt sein Stoppbit
    run(1, 12 * bit_x100 / 100 + 1);
    in_frame = false;
    t = 0;
}

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "log.h"

#define RMT_RES_HZ      1000000      // 1 us je Takt
#define RMT_BUF_SYMBOLS 4096         // reicht fuer ein Telegramm mit rund 800 Bytes

bool RmtUart::on_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t *e, void *ctx)
{
    RmtUart *self = (RmtUart *)ctx;
    capture_t c = {e->received_symbols, (uint32_t)e->num_symbols};
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(self->queue, &c, &woken);
    return woken == pdTRUE;
}

void RmtUart::arm()
{
    rmt_receive_config_t cfg = {};
    cfg.signal_range_min_ns = 1000;              // kuerzere Spitzen sind Stoerungen
    cfg.signal_range_max_ns = 20 * 1000 * 1000;  // 20 ms Ruhe beenden eine Aufnahme
    esp_err_t err = rmt_receive(chan, buf, buf_symbols * sizeof(rmt_symbol_word_t), &cfg);
    if (err != ESP_OK) {
        if (restart_errors == 0) {
            logf("RMT: Empfang startet nicht (%s)", esp_err_to_name(err));
        }
        restart_errors = restart_errors + 1;
        running = false;
    }
}

void RmtUart::task(void *arg)
{
    RmtUart *self = (RmtUart *)arg;
    self->arm();
    while (true) {
        capture_t c;
        xQueueReceive(self->queue, &c, portMAX_DELAY);
        uint32_t n = c.n;
        if (n > self->buf_symbols) {
            n = self->buf_symbols;
        }
        for (uint32_t i = 0; i < n; i++) {
            const rmt_symbol_word_t &s = c.sym[i];
            if (s.duration0 == 0) {
                break;
            }
            self->decoder.run(s.level0, s.duration0);
            if (s.duration1 == 0) {
                break;
            }
            self->decoder.run(s.level1, s.duration1);
        }
        self->decoder.idle();
        self->symbols = self->symbols + n;
        self->captures = self->captures + 1;
        self->arm();
    }
}

bool RmtUart::begin(int gpio, uint32_t baud, UartBitDecoder::byte_cb_t cb, void *ctx, int core)
{
    decoder.begin(baud, cb, ctx);
    queue = xQueueCreate(4, sizeof(capture_t));

    // Nur mit DMA passt ein ganzes Telegramm in eine Aufnahme. Ohne DMA fasst der
    // Kanal 48 Symbole, das reicht nicht, deshalb gibt es keinen Ersatzweg.
    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num = (gpio_num_t)gpio;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = RMT_RES_HZ;
    cfg.mem_block_symbols = RMT_BUF_SYMBOLS;   // mit DMA: Groesse des DMA-Bereichs
    cfg.flags.with_dma = 1;
    esp_err_t err = rmt_new_rx_channel(&cfg, &chan);
    if (err != ESP_OK) {
        logf("RMT: Kanal mit DMA fehlt (%s)", esp_err_to_name(err));
        return false;
    }
    buf_symbols = RMT_BUF_SYMBOLS;
    buf = (rmt_symbol_word_t *)heap_caps_aligned_alloc(64, buf_symbols * sizeof(rmt_symbol_word_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = on_done;
    if (buf == nullptr || rmt_rx_register_event_callbacks(chan, &cbs, this) != ESP_OK ||
        rmt_enable(chan) != ESP_OK) {
        logf("RMT: Start fehlgeschlagen");
        return false;
    }
    running = true;
    xTaskCreatePinnedToCore(task, "rmt_uart", 4096, this, 3, NULL, core);
    logf("RMT: Empfang auf GPIO %d, %lu Baud", gpio, (unsigned long)baud);
    return true;
}
#endif
