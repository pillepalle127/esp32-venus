// Empfangs-UART ueber RMT, fuer den zweiten MT681 (die drei UARTs sind belegt).
// Das RMT misst die Pulslaengen, der Decoder macht daraus Bytes (8N1, LSB zuerst).
#pragma once

#include <stddef.h>
#include <stdint.h>

// Wandelt Pegelabschnitte in Bytes. Ruhepegel high.
class UartBitDecoder {
public:
    typedef void (*byte_cb_t)(uint8_t b, void *ctx);

    void begin(uint32_t baud, byte_cb_t cb, void *ctx);
    // Ein Abschnitt mit konstantem Pegel und seiner Dauer in Mikrosekunden
    void run(uint8_t level, uint32_t us);
    // Ende einer Aufnahme: Leitung ist wieder in Ruhe
    void idle();

    uint32_t bytes = 0;
    uint32_t framing_errors = 0;

private:
    uint32_t bit_x100 = 0;    // Bitdauer in 1/100 us
    uint64_t t = 0;           // Zeit seit Beginn der Aufnahme, 1/100 us
    uint64_t next = 0;        // naechster Abtastpunkt
    byte_cb_t cb = nullptr;
    void *ctx = nullptr;
    bool in_frame = false;
    uint8_t count = 0;
    uint8_t value = 0;
};

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/rmt_rx.h>

class RmtUart {
public:
    // Startet Empfang und eine eigene Task auf `core`. `cb` laeuft in dieser Task.
    bool begin(int gpio, uint32_t baud, UartBitDecoder::byte_cb_t cb, void *ctx, int core);

    volatile uint32_t captures = 0;    // abgeschlossene Aufnahmen
    volatile uint32_t symbols = 0;
    volatile uint32_t restart_errors = 0;
    bool running = false;
    UartBitDecoder decoder;

private:
    typedef struct {
        rmt_symbol_word_t *sym;
        uint32_t n;
    } capture_t;

    static bool on_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *e, void *ctx);
    static void task(void *arg);
    void arm();

    rmt_channel_handle_t chan = nullptr;
    rmt_symbol_word_t *buf = nullptr;
    size_t buf_symbols = 0;
    QueueHandle_t queue = nullptr;
};
#endif
