// Funk-Test mit dem CMT2300A. Drei Betriebsarten:
// - mithoeren: auf einer Frequenz alle Pakete aufzeichnen, sendet nie
// - Kanalscan: alle Kanaele von 863 bis 870 MHz durchgehen, sendet nie
// - DTU: die Hoymiles-Bibliothek fragt die eingeschalteten Wechselrichter ab, sendet
// Gesendet wird nur fuer Geraete, deren Abfrage auf der Seite eingeschaltet ist.
// Geraeteliste, DTU-Frequenz und Sendeleistung bleiben ueber einen Neustart erhalten.
#pragma once

#include <Arduino.h>

#define RADIO_BASE_HZ   860000000UL     // Band 860 der Bibliothek
#define RADIO_STEP_HZ   250000UL        // Kanalbreite der Hoymiles-Bibliothek
#define RADIO_CH_MIN    12              // 863,00 MHz
#define RADIO_CH_MAX    40              // 870,00 MHz
#define RADIO_CHANNELS  (RADIO_CH_MAX - RADIO_CH_MIN + 1)
#define RADIO_PKTS      24
#define RADIO_PKT_MAX   32
#define RADIO_PEERS     16
#define RADIO_DEVS      12
#define RADIO_NAME_LEN  24
#define RADIO_DWELL_MS  6000            // Verweildauer je Kanal im Kanalscan, laenger als ein Abfrageabstand
#define RADIO_SETTLE_MS 20              // nach dem Umschalten zeigt RSSI noch den alten Kanal
#define RADIO_LBD_MS    10000           // so oft beim Mithoeren die Versorgung neu messen

class RadioTest {
public:
    // true, wenn der Chip antwortet. Startet danach die Funk-Task.
    bool begin(int sdio, int sclk, int csb, int fcsb, int core);
    // Aufrufe aus der Webseite, die Funk-Task fuehrt sie aus
    void listen(uint8_t ch);
    void scan();
    void resume();                  // DTU-Betrieb nach Mithoeren oder Kanalscan fortsetzen
    void clear();
    void set_dtu_channel(uint8_t ch);
    void set_pa(int8_t dbm);
    // Geraet anlegen oder aendern. serial 0: nur Name fuer die Kennung id (fremde DTU,
    // Wechselrichter ohne bekannte Seriennummer). poll: -1 unveraendert, 0 aus, 1 ein.
    // interval: Abfrageabstand in s, 0 unveraendert.
    bool dev_set(uint32_t id, uint64_t serial, const char *name, int8_t poll, uint16_t interval);
    bool dev_del(uint32_t id);
    void json(String &out);
    void record(const uint8_t *d, uint8_t len, int8_t rssi, uint8_t ch, bool tx);

    bool found = false;
    uint32_t packets = 0;           // mit gueltiger CRC

private:
    enum Mode : uint8_t { M_LISTEN, M_SCAN, M_DTU };
    struct Chan {
        int32_t sum;            // laufender Durchgang
        uint32_t n;
        int16_t max;            // Spitze seit dem Leeren
        int16_t avg;            // Mittel des letzten Durchgangs
        bool has_max, has_avg;
        uint32_t pkts;
    };
    struct Pkt {
        uint32_t ms;
        uint8_t ch;
        int8_t rssi;
        bool crc;
        bool tx;
        uint8_t len;
        uint8_t d[RADIO_PKT_MAX];
    };
    struct Peer {
        uint8_t inv[4], dtu[4];
        uint8_t ch;
        uint32_t req, rsp;
        int8_t rssi_req, rssi_rsp;
        uint32_t ms;
    };
    // Gespeichert als Block im NVS, Aufbau nicht ohne neue Version aendern
    struct Dev {
        uint32_t id;            // Kennung im Funk: die letzten 8 Stellen der Seriennummer
        uint64_t serial;        // ganze Seriennummer, 0 wenn unbekannt
        char name[RADIO_NAME_LEN];
        uint8_t poll;
        uint16_t interval;      // Abfrageabstand in s
    };
    struct DevOp {
        bool del;
        uint32_t id;
        uint64_t serial;
        char name[RADIO_NAME_LEN];
        bool has_name;
        int8_t poll;
        uint16_t interval;
    };

    static void task(void *arg);
    void run();
    void tune(uint8_t ch);
    void receive();
    void close_dwell();
    void apply_poll();
    void apply_op(const DevOp &op);
    void save_devs();
    void load_devs();
    void add_to_lib(const Dev &d);
    void schedule(uint32_t now);
    void after_loop(uint32_t now);
    int find_dev(uint32_t id) const;

    SemaphoreHandle_t mtx = nullptr;
    QueueHandle_t ops = nullptr;
    // Wuensche der Webseite
    volatile int16_t req_ch = -1, req_dtu_ch = -1, req_pa = -128;
    volatile bool req_scan = false, req_clear = false, req_resume = false;

    Mode mode = M_LISTEN;
    bool suspended = false;         // Mithoeren/Kanalscan unterbricht die Abfrage
    uint8_t ch = 0;
    uint8_t dtu_ch = 0;
    int8_t pa_dbm = 10;
    uint64_t dtu_serial = 0;
    uint32_t dwell_start = 0, last_lbd = 0, last_check = 0;
    uint8_t lbd = 0;                // Rohwert, VDD = lbd / 255 * 4,8 V (AN147)
    int8_t rssi_now = 0;
    uint32_t crc_errors = 0, rx_restarts = 0, passes = 0;
    Chan chans[RADIO_CHANNELS] = {};
    Pkt ring[RADIO_PKTS];
    uint32_t count = 0;
    Peer peers[RADIO_PEERS];
    uint8_t npeers = 0;
    Dev devs[RADIO_DEVS];
    uint8_t ndevs = 0;
    uint32_t dev_last[RADIO_DEVS] = {};     // letzte Abfrage, nur im RAM
    int8_t pending = -1;                    // freigegebenes Geraet, auf dessen Abfrage gewartet wird
    uint32_t last_rssi = 0;
};

extern RadioTest radio;
