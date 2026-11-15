#include "radio.h"

#include <Hoymiles.h>
#include <Preferences.h>
#include <cmt2300a.h>
#include <cmt2300a_defs.h>

#include "log.h"
#include "util.h"

// Erste Belegung der Geraeteliste, solange auf dem Board noch keine gespeichert ist
#if __has_include("inverters.h")
#include "inverters.h"
#endif
#ifndef DTU_FREQ_HZ
#define DTU_FREQ_HZ 865000000UL         // Voreinstellung in OpenDTU
#endif

#define DEV_INTERVAL_DEF 5              // s, wie die Voreinstellung in OpenDTU
#define DEV_BLOB_VERSION 1

RadioTest radio;

struct InvDef {
    const char *name;
    uint64_t serial;
};
#ifdef INVERTERS
static const InvDef INV_DEFS[] = INVERTERS;
static const int INV_N = sizeof(INV_DEFS) / sizeof(INV_DEFS[0]);
#else
static const InvDef *INV_DEFS = nullptr;
static const int INV_N = 0;
#endif

static HoymilesRadio_CMT *cmt;
static Preferences prefs;      // Geraeteliste, DTU-Frequenz und Sendeleistung bleiben ueber einen Neustart

static void save_u8(const char *key, uint8_t v)
{
    if (prefs.getUChar(key, 0xFF) != v) {
        prefs.putUChar(key, v);
    }
}

// CRC wie in der Hoymiles-Bibliothek: Startwert 0, Polynom 0x01, ueber alles ausser dem letzten Byte
static uint8_t crc8(const uint8_t *d, uint8_t n)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc << 1) ^ ((crc & 0x80) ? 0x01 : 0x00);
        }
    }
    return crc;
}

// Empfang starten wie CMT2300A::startListening() der Bibliothek
static void rx_start()
{
    CMT2300A_GoStby();
    CMT2300A_ClearInterruptFlags();
    CMT2300A_EnableReadFifo();
    CMT2300A_ClearRxFifo();
    CMT2300A_GoRx();
}

// DTU-Seriennummer wie OpenDTU: 1999 0000 0000 plus die letzten 6 Dezimalstellen der Chip-ID als BCD
static uint64_t make_dtu_serial()
{
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(mac >> 24) ^ (uint32_t)mac;
    uint64_t s = 0x199900000000ULL;
    for (int i = 0; i < 6; i++) {
        s |= (uint64_t)(id % 10) << (i * 4);
        id /= 10;
    }
    return s;
}

static double ch_mhz(uint8_t c)
{
    return (RADIO_BASE_HZ + c * RADIO_STEP_HZ) / 1e6;
}

// ---------------------------------------------------------------- Geraeteliste

int RadioTest::find_dev(uint32_t id) const
{
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].id == id) {
            return i;
        }
    }
    return -1;
}

void RadioTest::save_devs()
{
    prefs.putUChar("devv", DEV_BLOB_VERSION);
    prefs.putBytes("devs", devs, ndevs * sizeof(Dev));
}

void RadioTest::load_devs()
{
    size_t n = prefs.getUChar("devv", 0) == DEV_BLOB_VERSION ? prefs.getBytesLength("devs") : 0;
    if (n && n % sizeof(Dev) == 0 && n / sizeof(Dev) <= RADIO_DEVS) {
        prefs.getBytes("devs", devs, n);
        ndevs = n / sizeof(Dev);
        for (int i = 0; i < ndevs; i++) {
            devs[i].name[RADIO_NAME_LEN - 1] = 0;
        }
        return;
    }
    // Noch keine Liste: aus inverters.h, Abfrage wie bisher gespeichert
    uint8_t mask = prefs.getUChar("poll", 0);
    ndevs = 0;
    for (int i = 0; i < INV_N && ndevs < RADIO_DEVS; i++) {
        Dev &d = devs[ndevs++];
        memset(&d, 0, sizeof(d));
        d.serial = INV_DEFS[i].serial;
        d.id = (uint32_t)d.serial;
        strlcpy(d.name, INV_DEFS[i].name, sizeof(d.name));
        d.poll = (mask >> i) & 1;
        d.interval = DEV_INTERVAL_DEF;
    }
    save_devs();
}

void RadioTest::add_to_lib(const Dev &d)
{
    auto iv = Hoymiles.addInverter(d.name, d.serial);
    if (iv) {
        iv->setEnablePolling(false);
        iv->setEnableCommands(false);
    } else {
        logf("Funk: Seriennummer %04lx%08lx, Typ unbekannt", (unsigned long)(d.serial >> 32),
             (unsigned long)d.serial);
    }
}

bool RadioTest::dev_set(uint32_t id, uint64_t serial, const char *name, int8_t poll, uint16_t interval)
{
    DevOp op = {};
    op.id = serial ? (uint32_t)serial : id;
    op.serial = serial;
    op.has_name = name != nullptr;
    if (name) {
        strlcpy(op.name, name, sizeof(op.name));
    }
    op.poll = poll;
    op.interval = interval;
    return ops && xQueueSend(ops, &op, 0) == pdTRUE;
}

bool RadioTest::dev_del(uint32_t id)
{
    DevOp op = {};
    op.del = true;
    op.id = id;
    op.poll = -1;
    return ops && xQueueSend(ops, &op, 0) == pdTRUE;
}

// In der Funk-Task: Liste und Bibliothek aendern, dann speichern
void RadioTest::apply_op(const DevOp &op)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    int i = find_dev(op.id);
    if (op.del) {
        if (i >= 0) {
            if (devs[i].serial) {
                Hoymiles.removeInverterBySerial(devs[i].serial);
            }
            logf("Funk: Geraet %08lx entfernt", (unsigned long)op.id);
            for (int k = i; k < ndevs - 1; k++) {
                devs[k] = devs[k + 1];
                dev_last[k] = dev_last[k + 1];
            }
            ndevs--;
            pending = -1;
            save_devs();
        }
        xSemaphoreGive(mtx);
        apply_poll();
        return;
    }
    if (i < 0) {
        if (ndevs >= RADIO_DEVS) {
            xSemaphoreGive(mtx);
            logf("Funk: Geraeteliste voll");
            return;
        }
        i = ndevs++;
        memset(&devs[i], 0, sizeof(Dev));
        devs[i].id = op.id;
        devs[i].interval = DEV_INTERVAL_DEF;
        dev_last[i] = 0;
    }
    Dev &d = devs[i];
    if (op.has_name) {
        strlcpy(d.name, op.name, sizeof(d.name));
    }
    if (op.serial && op.serial != d.serial) {
        if (d.serial) {
            Hoymiles.removeInverterBySerial(d.serial);
        }
        d.serial = op.serial;
        add_to_lib(d);
    } else if (op.has_name && d.serial) {
        auto iv = Hoymiles.getInverterBySerial(d.serial);
        if (iv) {
            iv->setName(d.name);
        }
    }
    if (op.interval) {
        d.interval = op.interval < 1 ? 1 : op.interval > 3600 ? 3600 : op.interval;
    }
    bool turned_on = false;
    if (op.poll >= 0) {
        uint8_t p = op.poll && d.serial ? 1 : 0;
        turned_on = p && !d.poll;
        d.poll = p;
    }
    save_devs();
    logf("Funk: Geraet %08lx \"%s\", Seriennummer %04lx%08lx, Abfrage %s, alle %u s", (unsigned long)d.id, d.name,
         (unsigned long)(d.serial >> 32), (unsigned long)d.serial, d.poll ? "ein" : "aus", d.interval);
    xSemaphoreGive(mtx);
    if (turned_on) {
        suspended = false;      // Einschalten setzt eine unterbrochene Abfrage fort
    }
    apply_poll();
}

// ---------------------------------------------------------------- Start

bool RadioTest::begin(int sdio, int sclk, int csb, int fcsb, int core)
{
    mtx = xSemaphoreCreateMutex();
    ops = xQueueCreate(8, sizeof(DevOp));
    prefs.begin("radio", false);
    dtu_ch = prefs.getUChar("dtu", (DTU_FREQ_HZ - RADIO_BASE_HZ) / RADIO_STEP_HZ);
    if (dtu_ch < RADIO_CH_MIN || dtu_ch > RADIO_CH_MAX) {
        dtu_ch = 20;
    }
    pa_dbm = (int8_t)prefs.getChar("pa", pa_dbm);
    Hoymiles.init();
    Hoymiles.initCMT(sdio, sclk, csb, fcsb, -1, -1);    // ohne GPIO2/GPIO3: Paketeingang per SPI abfragen
    cmt = Hoymiles.getRadioCmt();
    found = cmt->isConnected();
    if (!found) {
        return false;
    }
    cmt->setCountryMode(CountryModeId_t::MODE_EU);
    cmt->setInverterTargetFrequency(RADIO_BASE_HZ + dtu_ch * RADIO_STEP_HZ);
    cmt->setPALevel(pa_dbm);
    dtu_serial = make_dtu_serial();
    cmt->setDtuSerial(dtu_serial);
    // Ohne eigenen Abstand der Bibliothek: Sie fragt jedes freigegebene Geraet sofort ab.
    // Den Abstand je Geraet regelt schedule(), es gibt immer nur ein faelliges Geraet frei.
    Hoymiles.setPollInterval(0);
    load_devs();
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].serial) {
            add_to_lib(devs[i]);
        }
    }
    cmt->onPacket = [](const uint8_t *d, uint8_t len, int8_t rssi, uint8_t c, bool tx) {
        radio.record(d, len, rssi, c, tx);
    };
    tune(dtu_ch);
    xTaskCreatePinnedToCore(task, "radio", 6144, this, 2, NULL, core);
    return true;
}

void RadioTest::listen(uint8_t c)
{
    if (c >= RADIO_CH_MIN && c <= RADIO_CH_MAX) {
        req_ch = c;
    }
}

void RadioTest::scan()
{
    req_scan = true;
}

void RadioTest::resume()
{
    req_resume = true;
}

void RadioTest::clear()
{
    req_clear = true;
}

void RadioTest::set_dtu_channel(uint8_t c)
{
    if (c >= RADIO_CH_MIN && c <= RADIO_CH_MAX) {
        req_dtu_ch = c;
    }
}

void RadioTest::set_pa(int8_t dbm)
{
    if (dbm >= -10 && dbm <= 20) {
        req_pa = dbm;
    }
}

void RadioTest::task(void *arg)
{
    static_cast<RadioTest *>(arg)->run();
}

// ---------------------------------------------------------------- Empfang

void RadioTest::tune(uint8_t c)
{
    ch = c;
    CMT2300A_GoStby();
    CMT2300A_SetFrequencyChannel(c);
    rx_start();
    // Die Versorgung misst der Chip beim Wechsel in den Empfang
    lbd = CMT2300A_ReadReg(CMT2300A_CUS_LBD_RESULT);
    dwell_start = millis();
    last_lbd = dwell_start;
}

// Mittelwert des Kanals fuer diesen Durchgang festhalten
void RadioTest::close_dwell()
{
    Chan &c = chans[ch - RADIO_CH_MIN];
    if (c.n) {
        c.avg = (int16_t)(c.sum / (int32_t)c.n);
        c.has_avg = true;
    }
    c.sum = 0;
    c.n = 0;
}

void RadioTest::receive()
{
    int8_t rssi = (int8_t)CMT2300A_GetRssiDBm();
    uint8_t d[RADIO_PKT_MAX];
    uint8_t len = 0;
    CMT2300A_ReadFifo(&len, 1);         // erstes Byte im FIFO ist die Laenge
    if (len > RADIO_PKT_MAX) {
        len = RADIO_PKT_MAX;
    }
    CMT2300A_ReadFifo(d, len);
    CMT2300A_ClearInterruptFlags();
    CMT2300A_ClearRxFifo();
    record(d, len, rssi, ch, false);
}

// Paket in Liste und Geraetetabelle eintragen. Beim Mithoeren aus receive(), im
// DTU-Betrieb aus der Hoymiles-Bibliothek (gesendete und empfangene Pakete).
void RadioTest::record(const uint8_t *d, uint8_t len, int8_t rssi, uint8_t c, bool tx)
{
    Pkt p;
    p.ms = millis();
    p.ch = c;
    p.rssi = rssi;
    p.tx = tx;
    p.len = len > RADIO_PKT_MAX ? RADIO_PKT_MAX : len;
    memcpy(p.d, d, p.len);
    p.crc = p.len > 1 && crc8(p.d, p.len - 1) == p.d[p.len - 1];
    bool in_band = c >= RADIO_CH_MIN && c <= RADIO_CH_MAX;

    xSemaphoreTake(mtx, portMAX_DELAY);
    ring[count % RADIO_PKTS] = p;
    count++;
    if (!p.crc) {
        crc_errors++;
    } else {
        packets++;
        if (in_band && !tx) {
            chans[c - RADIO_CH_MIN].pkts++;
        }
        // Byte 0 Befehl (Bit 7: Antwort des Wechselrichters), 1-4 Wechselrichter, 5-8 DTU
        if (p.len >= 10) {
            Peer *e = nullptr;
            for (uint8_t i = 0; i < npeers; i++) {
                if (memcmp(peers[i].inv, &p.d[1], 4) == 0 && memcmp(peers[i].dtu, &p.d[5], 4) == 0) {
                    e = &peers[i];
                    break;
                }
            }
            if (e == nullptr && npeers < RADIO_PEERS) {
                e = &peers[npeers++];
                memset(e, 0, sizeof(*e));
                memcpy(e->inv, &p.d[1], 4);
                memcpy(e->dtu, &p.d[5], 4);
            }
            if (e != nullptr) {
                e->ch = c;
                e->ms = p.ms;
                if (p.d[0] & 0x80) {
                    e->rsp++;
                    e->rssi_rsp = p.rssi;
                } else {
                    e->req++;
                    if (!tx) {
                        e->rssi_req = p.rssi;
                    }
                }
            }
        }
    }
    xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------- Abfrage

// Betriebsart nach der Geraeteliste: DTU, sobald ein Geraet eingeschaltet ist und die
// Abfrage nicht durch Mithoeren oder Kanalscan unterbrochen ist
void RadioTest::apply_poll()
{
    bool any = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < ndevs; i++) {
        if (!devs[i].serial) {
            continue;
        }
        auto iv = Hoymiles.getInverterBySerial(devs[i].serial);
        if (!iv) {
            continue;
        }
        iv->setEnablePolling(false);
        any |= devs[i].poll;
    }
    pending = -1;
    xSemaphoreGive(mtx);
    bool want_dtu = any && !suspended;
    if (want_dtu && mode != M_DTU) {
        close_dwell();
        mode = M_DTU;
        ch = dtu_ch;
        cmt->setInverterTargetFrequency(RADIO_BASE_HZ + dtu_ch * RADIO_STEP_HZ);
        logf("Funk: DTU auf %.2f MHz, %d dBm, Seriennummer %04lX%08lX", ch_mhz(dtu_ch), pa_dbm,
             (unsigned long)(dtu_serial >> 32), (unsigned long)dtu_serial);
    } else if (!want_dtu && mode == M_DTU) {
        mode = M_LISTEN;
        tune(dtu_ch);
        logf("Funk: Abfrage %s, hoere auf %.2f MHz", suspended ? "unterbrochen" : "aus", ch_mhz(dtu_ch));
    }
}

// Vor Hoymiles.loop(): Ist die Warteschlange der Bibliothek leer, das am laengsten
// ueberfaellige Geraet freigeben. Immer nur eines, damit sich nichts aufstaut.
void RadioTest::schedule(uint32_t now)
{
    if (pending >= 0 || cmt->getQueueSize() > 0) {
        return;
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    int best = -1;
    uint32_t best_late = 0;
    for (int i = 0; i < ndevs; i++) {
        const Dev &d = devs[i];
        if (!d.serial || !d.poll) {
            continue;
        }
        uint32_t since = dev_last[i] ? now - dev_last[i] : UINT32_MAX / 2;
        uint32_t iv_ms = d.interval * 1000UL;
        if (since >= iv_ms && (best < 0 || since - iv_ms > best_late)) {
            best = i;
            best_late = since - iv_ms;
        }
    }
    if (best >= 0) {
        auto iv = Hoymiles.getInverterBySerial(devs[best].serial);
        if (iv) {
            iv->setEnablePolling(true);
            pending = best;
        }
    }
    xSemaphoreGive(mtx);
}

// Nach Hoymiles.loop(): Hat die Bibliothek fuer das freigegebene Geraet Befehle in die
// Warteschlange gestellt, ist es abgefragt. Wieder sperren bis zur naechsten Faelligkeit.
void RadioTest::after_loop(uint32_t now)
{
    if (pending < 0 || cmt->getQueueSize() == 0) {
        return;
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    auto iv = Hoymiles.getInverterBySerial(devs[pending].serial);
    if (iv) {
        iv->setEnablePolling(false);
    }
    dev_last[pending] = now;
    pending = -1;
    xSemaphoreGive(mtx);
}

void RadioTest::run()
{
    apply_poll();
    while (true) {
        DevOp op;
        while (xQueueReceive(ops, &op, 0) == pdTRUE) {
            apply_op(op);
        }
        if (req_clear) {
            req_clear = false;
            xSemaphoreTake(mtx, portMAX_DELAY);
            memset(chans, 0, sizeof(chans));
            count = 0;
            npeers = 0;
            packets = 0;
            crc_errors = 0;
            passes = 0;
            xSemaphoreGive(mtx);
        }
        if (req_pa != -128) {
            pa_dbm = req_pa;
            req_pa = -128;
            cmt->setPALevel(pa_dbm);
            if (prefs.getChar("pa", 127) != pa_dbm) {
                prefs.putChar("pa", pa_dbm);
            }
            logf("Funk: Sendeleistung %d dBm", pa_dbm);
        }
        if (req_dtu_ch >= 0) {
            dtu_ch = (uint8_t)req_dtu_ch;
            req_dtu_ch = -1;
            cmt->setInverterTargetFrequency(RADIO_BASE_HZ + dtu_ch * RADIO_STEP_HZ);
            if (mode == M_DTU) {
                ch = dtu_ch;
            }
            save_u8("dtu", dtu_ch);
            logf("Funk: DTU-Frequenz %.2f MHz", ch_mhz(dtu_ch));
        }
        if (req_resume) {
            req_resume = false;
            suspended = false;
            apply_poll();
        }
        // Mithoeren und Kanalscan unterbrechen die Abfrage, die Ein/Aus-Stellung bleibt
        if (req_ch >= 0) {
            uint8_t c = (uint8_t)req_ch;
            req_ch = -1;
            suspended = true;
            apply_poll();
            close_dwell();
            mode = M_LISTEN;
            tune(c);
            logf("Funk: hoere auf %.2f MHz", ch_mhz(c));
        }
        if (req_scan) {
            req_scan = false;
            suspended = true;
            apply_poll();
            close_dwell();
            mode = M_SCAN;
            passes = 0;
            tune(RADIO_CH_MIN);
            logf("Funk: Kanalscan %.2f bis %.2f MHz", ch_mhz(RADIO_CH_MIN), ch_mhz(RADIO_CH_MAX));
        }

        uint32_t now = millis();
        if (mode == M_DTU) {
            schedule(now);
            Hoymiles.loop();
            after_loop(now);
            if (now - last_lbd >= 1000) {
                last_lbd = now;
                lbd = CMT2300A_ReadReg(CMT2300A_CUS_LBD_RESULT);
                rssi_now = (int8_t)CMT2300A_GetRssiDBm();
            }
            vTaskDelay(1);
            continue;
        }

        if (mode == M_SCAN && now - dwell_start >= RADIO_DWELL_MS) {
            close_dwell();
            uint8_t next = ch + 1;
            if (next > RADIO_CH_MAX) {
                next = RADIO_CH_MIN;
                passes++;
            }
            tune(next);
        } else if (mode == M_LISTEN && now - last_lbd >= RADIO_LBD_MS &&
                   !(CMT2300A_ReadReg(CMT2300A_CUS_INT_FLAG) & (CMT2300A_MASK_PREAM_OK_FLG | CMT2300A_MASK_SYNC_OK_FLG))) {
            tune(ch);       // kurz Standby und zurueck, damit der Chip die Versorgung neu misst
        }

        // Paket da (PKT_OK), sonst Rauschen messen. Nicht waehrend ein Paket laeuft (Praeambel
        // oder Sync erkannt) und hoechstens alle 10 ms: Beim Mithoeren mit RSSI-Lesen in jeder
        // Millisekunde kamen viele Pakete mit Laenge 0, im DTU-Betrieb ohne das keines.
        uint8_t fl = CMT2300A_ReadReg(CMT2300A_CUS_INT_FLAG);
        if (fl & CMT2300A_MASK_PKT_OK_FLG) {
            receive();
        } else if (!(fl & (CMT2300A_MASK_PREAM_OK_FLG | CMT2300A_MASK_SYNC_OK_FLG)) &&
                   now - dwell_start >= RADIO_SETTLE_MS && now - last_rssi >= 10) {
            last_rssi = now;
            int8_t r = (int8_t)CMT2300A_GetRssiDBm();
            rssi_now = r;
            Chan &c = chans[ch - RADIO_CH_MIN];
            c.sum += r;
            c.n++;
            if (!c.has_max || r > c.max) {
                c.max = r;
                c.has_max = true;
            }
            if (mode == M_LISTEN && c.n >= 200) {
                close_dwell();
            }
        }

        // Der Chip sollte im Empfang bleiben; falls nicht, neu starten und zaehlen
        if (now - last_check >= 100) {
            last_check = now;
            if (CMT2300A_GetChipStatus() != CMT2300A_STA_RX) {
                rx_restarts++;
                rx_start();
            }
        }
        vTaskDelay(1);
    }
}

// ---------------------------------------------------------------- JSON

static void put_id(String &o, const uint8_t *b)
{
    char s[12];
    snprintf(s, sizeof(s), "\"%02x%02x%02x%02x\"", b[0], b[1], b[2], b[3]);
    o += s;
}

static void put_num(String &o, const char *key, float v, int dec)
{
    char b[40];
    if (isfinite(v)) {
        snprintf(b, sizeof(b), ",\"%s\":%.*f", key, dec, v);
    } else {
        snprintf(b, sizeof(b), ",\"%s\":null", key);
    }
    o += b;
}

void RadioTest::json(String &o)
{
    char b[200];
    if (!found) {
        o += "{\"ok\":false}";
        return;
    }
    struct tm tm;
    bool have_time = getLocalTime(&tm, 0);
    xSemaphoreTake(mtx, portMAX_DELAY);
    snprintf(b, sizeof(b),
             "{\"ok\":true,\"mode\":\"%s\",\"suspended\":%s,\"ch\":%u,\"dtu_ch\":%u,\"pa\":%d,\"rssi\":%d,\"vdd\":%.2f,"
             "\"pkts\":%lu,\"crc\":%lu,\"restarts\":%lu,\"passes\":%lu,\"time\":%s,\"now\":%lu,",
             mode == M_DTU ? "dtu" : mode == M_SCAN ? "scan" : "listen", suspended ? "true" : "false", ch, dtu_ch,
             pa_dbm, rssi_now, lbd * 4.8 / 255.0, (unsigned long)packets, (unsigned long)crc_errors,
             (unsigned long)rx_restarts, (unsigned long)passes, have_time ? "true" : "false", (unsigned long)millis());
    o += b;
    snprintf(b, sizeof(b), "\"dtu\":\"%04lx%08lx\",\"chs\":[", (unsigned long)(dtu_serial >> 32),
             (unsigned long)dtu_serial);
    o += b;
    bool first = true;
    for (int i = 0; i < RADIO_CHANNELS; i++) {
        const Chan &c = chans[i];
        if (!c.has_max && !c.pkts) {
            continue;
        }
        int avg = c.has_avg ? c.avg : c.n ? (int)(c.sum / (int32_t)c.n) : c.max;
        snprintf(b, sizeof(b), "%s{\"ch\":%d,\"avg\":%d,\"max\":%d,\"pkts\":%lu}", first ? "" : ",",
                 i + RADIO_CH_MIN, avg, c.max, (unsigned long)c.pkts);
        o += b;
        first = false;
    }
    o += "],\"peers\":[";
    for (uint8_t i = 0; i < npeers; i++) {
        const Peer &e = peers[i];
        o += i ? ",{\"inv\":" : "{\"inv\":";
        put_id(o, e.inv);
        o += ",\"dtu\":";
        put_id(o, e.dtu);
        snprintf(b, sizeof(b), ",\"ch\":%u,\"req\":%lu,\"rsp\":%lu,\"rreq\":%d,\"rrsp\":%d,\"ms\":%lu}", e.ch,
                 (unsigned long)e.req, (unsigned long)e.rsp, e.rssi_req, e.rssi_rsp, (unsigned long)e.ms);
        o += b;
    }
    o += "],\"list\":[";
    uint32_t first_i = count > RADIO_PKTS ? count - RADIO_PKTS : 0;
    for (uint32_t i = count; i > first_i; i--) {
        const Pkt &p = ring[(i - 1) % RADIO_PKTS];
        snprintf(b, sizeof(b), "%s{\"ms\":%lu,\"ch\":%u,\"rssi\":%d,\"crc\":%s,\"tx\":%s,\"d\":",
                 i == count ? "" : ",", (unsigned long)p.ms, p.ch, p.rssi, p.crc ? "true" : "false",
                 p.tx ? "true" : "false");
        o += b;
        json_hex(o, p.d, p.len);
        o += '}';
    }

    // Geraeteliste, bei bekannter Seriennummer mit den Werten aus der Bibliothek
    o += "],\"devs\":[";
    for (int i = 0; i < ndevs; i++) {
        const Dev &d = devs[i];
        if (i) {
            o += ',';
        }
        snprintf(b, sizeof(b), "{\"id\":\"%08lx\",\"poll\":%s,\"iv\":%u,\"name\":", (unsigned long)d.id,
                 d.poll ? "true" : "false", d.interval);
        o += b;
        json_str(o, d.name);
        if (!d.serial) {
            o += ",\"serial\":\"\"}";
            continue;
        }
        snprintf(b, sizeof(b), ",\"serial\":\"%04lx%08lx\"", (unsigned long)(d.serial >> 32), (unsigned long)d.serial);
        o += b;
        auto iv = Hoymiles.getInverterBySerial(d.serial);
        if (!iv) {
            o += ",\"type\":null}";
            continue;
        }
        auto st = iv->Statistics();
        uint32_t upd = st->getLastUpdate();
        // isReachable()/isProducing() der Bibliothek sind nur bei freigegebener Abfrage wahr,
        // schedule() gibt aber nur kurz frei. Deshalb hier selbst: Daten da, Fehler unter der Schwelle.
        bool reach = upd && st->getRxFailureCount() <= iv->getReachableThreshold();
        bool prod = reach && st->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC) > 0;
        snprintf(b, sizeof(b),
                 ",\"type\":\"%s\",\"reach\":%s,\"prod\":%s,\"rssi\":%d,\"age\":%ld,\"tx\":%lu,\"retx\":%lu,"
                 "\"ok\":%lu,\"part\":%lu,\"none\":%lu,\"bad\":%lu",
                 iv->typeName().c_str(), reach ? "true" : "false", prod ? "true" : "false",
                 iv->getLastRssi(), upd ? (long)(millis() - upd) : -1L, (unsigned long)iv->RadioStats.TxRequestData,
                 (unsigned long)iv->RadioStats.TxReRequestFragment, (unsigned long)iv->RadioStats.RxSuccess,
                 (unsigned long)iv->RadioStats.RxFailPartialAnswer, (unsigned long)iv->RadioStats.RxFailNoAnswer,
                 (unsigned long)iv->RadioStats.RxFailCorruptData);
        o += b;
        if (upd) {
            put_num(o, "pac", st->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC), 1);
            put_num(o, "uac", st->getChannelFieldValue(TYPE_AC, CH0, FLD_UAC), 1);
            put_num(o, "f", st->getChannelFieldValue(TYPE_AC, CH0, FLD_F), 2);
            put_num(o, "t", st->getChannelFieldValue(TYPE_INV, CH0, FLD_T), 1);
            // Ertrag fuehrt die Bibliothek als Summe der DC-Eingaenge unter INV
            put_num(o, "yd", st->getChannelFieldValue(TYPE_INV, CH0, FLD_YD), 0);
            put_num(o, "yt", st->getChannelFieldValue(TYPE_INV, CH0, FLD_YT), 3);
            o += ",\"dc\":[";
            bool f1 = true;
            for (auto c : st->getChannelsByType(TYPE_DC)) {
                snprintf(b, sizeof(b), "%s[%.1f,%.1f]", f1 ? "" : ",", st->getChannelFieldValue(TYPE_DC, c, FLD_PDC),
                         st->getChannelFieldValue(TYPE_DC, c, FLD_UDC));
                o += b;
                f1 = false;
            }
            o += ']';
        }
        o += '}';
    }
    xSemaphoreGive(mtx);
    o += "]}";
}
