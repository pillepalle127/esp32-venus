// VRM, Schritt V2: Logger und Live-Brücke wie in Venus OS v3.75. Ablauf in docs/vrm.md.
#include "vrm.h"

#include <WiFi.h>
#include <deque>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <math.h>
#include <freertos/idf_additions.h>
#include <mbedtls/platform.h>
#include <mqtt_client.h>
#include <time.h>

#include "log.h"
#include "util.h"
#include "vrm_quelle.h"

#if __has_include("vrm_secrets.h")
#include "vrm_secrets.h"
#define VRM_HAVE_SECRETS 1
#else
#define VRM_HAVE_SECRETS 0
#define VRM_PORTAL_ID ""
#define VRM_AUTH_TOKEN ""
#define VRM_MQTT_PASSWORD ""
#define VRM_CA_PEM ""
#endif

static const char LOG_URL[] = "https://ccgxlogging.victronenergy.com/log/log.php";
static const char BULK_URL[] = "https://ccgxlogging.victronenergy.com/log/logbulk.php";
static const char REG_URL[] = "https://ccgxlogging.victronenergy.com/log/storemqttpassword.php";
static const char CA[] = VRM_CA_PEM;
static const int INTERVALL = 300;           // s, wie /Settings/Vrmlogger/LogInterval auf dem Pi
static const int KWH_FENSTER = 900;         // s, seconds_until_next_kwh_delta_upload()
static const size_t SCHLANGE_MAX = 1536 * 1024;     // Bytes, dann fallen die ältesten Zeilen weg
static const size_t BUENDEL_MAX = 48 * 1024;        // Bytes ungepackt je POST

enum { ANNOUNCE = 0, SENDDATA = 1, CONFIGCHANGE = 2, HOURLYDELTAS = 3, TEST_POST = 6, PRIORITY_CONFIGCHANGE = 8 };

// ANNOUNCE wie vrm_replay.py: cp Bit 8 (Keepalive statt Retain) und Bit 10 (neue
// Konfigurations-Attribute; ohne setzt VRM view_permissions.twoway nicht)
static const char *const ANNOUNCE_DATEN[][2] = {{"v", "v3.75"}, {"mi", "C003"}, {"cp", "1280"}};
static const char *const PRIORITY_DATEN[][2] = {{"vrm", "2"}};
// Einstellungen von ESP_venus, wie der Pi sie meldet (vrm_replay.SYSTEM_KONFIG ohne st)
static const char *const KONST_KONFIG[][2] = {
    {"tz", "Europe/Berlin"}, {"sl", "300"}, {"mql", "1"}, {"al", "2"}, {"si1", "1"}, {"si2", "0"},
    {"shao", "1"}, {"shai", "1"}, {"shd", "0"}, {"sb", "com.victronenergy.battery/291"},
    {"H4M", "1"}, {"bol", "1"}, {"rwgm", "0"},
};
// Leseanfragen der VRM-Seite, die nicht aus der Tabelle kommen (Vorgaben von Venus)
static const char *const EINSTELLUNGEN[][2] = {
    {"settings/0/Settings/System/Units/Temperature", "\"celsius\""},
    {"settings/0/Settings/System/VolumeUnit", "0"},
    {"settings/0/Settings/Gps/SpeedUnit", "\"km/h\""},
    {"system/0/Connected", "1"}, {"vebus/288/Connected", "1"}, {"battery/291/Connected", "1"},
    {"battery/512/Connected", "1"}, {"grid/40/Connected", "1"}, {"solarcharger/288/Connected", "1"},
    {"solarcharger/291/Connected", "1"}, {"solarcharger/292/Connected", "1"},
    {"solarcharger/293/Connected", "1"}, {"solarcharger/294/Connected", "1"},
    {"solarcharger/296/Connected", "1"},
};

static SemaphoreHandle_t mtx;
static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

static volatile bool an = true;
static String zustand = "aus";
static String letzte_antwort = "-";
static uint32_t gesendet_zeilen, gesendet_posts, fehler_posts;
static time_t letzte_senddata, letzte_kwh, letzte_ok;
static String letzte_daten;        // letzte SENDDATA-Zeile ohne Kopf und Token, zum Prüfen

// ---------------------------------------------------------------- Hilfen

// Interner RAM an den Startschritten (README, Abschnitt Speicher)
struct Messpunkt {
    const char *name;
    uint32_t frei, tief, block;
};
static Messpunkt messung[6];
static int messungen;

static void messpunkt(const char *name)
{
    uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t t = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    uint32_t b = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (messungen < 6) {
        messung[messungen++] = {name, f, t, b};
    }
    logf("RAM: %s: intern frei %lu, Tiefstwert %lu, größter Block %lu", name, (unsigned long)f, (unsigned long)t,
         (unsigned long)b);
}

static void setze(String &ziel, const String &s)
{
    lock();
    ziel = s;
    unlock();
}

// urllib.parse.quote_plus
static void url_enc(String &o, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = *s;
        if (isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') {
            o += (char)c;
        } else if (c == ' ') {
            o += '+';
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
}

static void paar(String &o, const char *k, const char *v)
{
    if (o.length()) {
        o += '&';
    }
    url_enc(o, k);
    o += '=';
    url_enc(o, v);
}

// Wie str(round(v, n)) in Python: ohne überflüssige Nullen, mindestens eine Nachkommastelle
static void zahl(char *b, size_t n, double v, int stellen)
{
    snprintf(b, n, "%.*f", stellen, v);
    if (stellen > 0) {
        char *e = b + strlen(b) - 1;
        while (*e == '0' && e[-1] != '.') {
            *e-- = 0;
        }
    }
}

// Text eines JSON-Strings ohne Anführungszeichen (nur \" \\ \/ und ASCII-\u)
static void json_text(const char *roh, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = roh + 1; *p && *p != '"' && k + 1 < n; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'u' && strlen(p) >= 5) {
                out[k++] = (char)strtol(String(p + 1).substring(0, 4).c_str(), NULL, 16);
                p += 4;
                continue;
            }
        }
        out[k++] = *p;
    }
    out[k] = 0;
}

// Wert eines Tabelleneintrags so, wie der vrmlogger ihn schickt (conversions.py, monitor.py)
static bool wert_text(int i, char *out, size_t n)
{
    const VrmEintrag &e = vq_eintrag(i);
    char roh[VQ_ROH];
    if (!vq_roh(i, roh, sizeof(roh))) {
        return false;
    }
    double v;
    switch (e.um) {
    case UT:
        strlcpy(out, e.text, n);
        return true;
    case UP:
        if (!vq_zahl(i, v)) {
            return false;
        }
        snprintf(out, n, "%llx", (long long)v);
        return true;
    case UA: {
        // [lo, hi, lo, hi, ...] -> "id,id" hex, ohne Nullen
        int b[128], k = 0;
        for (char *p = roh + 1; *p && *p != ']' && k < 128;) {
            char *q;
            b[k++] = strtol(p, &q, 10);
            if (q == p) {
                return false;
            }
            p = q;
            while (*p == ',' || *p == ' ') {
                p++;
            }
        }
        size_t m = 0;
        out[0] = 0;
        for (int j = 0; j + 1 < k; j += 2) {
            int id = b[j] | b[j + 1] << 8;
            if (id) {
                m += snprintf(out + m, n - m, "%s%x", m ? "," : "", id);
            }
        }
        return true;
    }
    case UD:
    case UH:
        if (!vq_zahl(i, v)) {
            return false;
        }
        v /= e.um == UD ? 86400.0 : 3600.0;
        if (e.stellen > 0) {
            zahl(out, n, v, e.stellen);
        } else {
            snprintf(out, n, "%.10g", v);
        }
        return true;
    case UN:
    default:
        if (roh[0] == '"') {
            json_text(roh, out, n);
        } else if (roh[0] == '[' || roh[0] == '{') {
            return false;
        } else if (!strcmp(roh, "true") || !strcmp(roh, "false")) {
            strlcpy(out, roh[0] == 't' ? "1" : "0", n);
        } else if (e.stellen > 0 && vq_zahl(i, v)) {
            // monitor.py rundet nur bei precision > 0
            zahl(out, n, v, e.stellen);
        } else {
            strlcpy(out, roh, n);
        }
        return true;
    }
}

static void kwh_text(char *b, size_t n, double v)
{
    zahl(b, n, v, 6);
}

// ---------------------------------------------------------------- kWh (kwhdeltas.py)

static double *basis;               // Zählerstand am Beginn des Fensters, NAN = keiner
static time_t basis_zeit;

static bool ist_zaehler(const char *t)
{
    static const char *const end[] = {"/Energy/AcIn1ToInverter", "/Energy/AcIn2ToInverter", "/Energy/AcIn1ToAcOut",
                                      "/Energy/AcIn2ToAcOut", "/Energy/InverterToAcIn1", "/Energy/InverterToAcIn2",
                                      "/Energy/AcOutToAcIn1", "/Energy/AcOutToAcIn2", "/Energy/InverterToAcOut",
                                      "/Energy/OutToInverter", "/History/DischargedEnergy", "/History/ChargedEnergy",
                                      "/Yield/User", "/Ac/Energy/Forward", "/Ac/Energy/Reverse", "/Timers/TimeOff",
                                      "/Timers/TimeOnGenerator", "/Timers/TimeOnGrid", "/Timers/TimeOnInverter"};
    // nur auf Geräteebene, nicht /Ac/L1/Energy/... oder /Devices/...
    const char *s = strchr(t, '/');
    s = s ? strchr(s + 1, '/') : NULL;
    if (!s) {
        return false;
    }
    for (auto x : end) {
        if (!strcmp(s, x)) {
            return true;
        }
    }
    return false;
}

static void dienst_inst(const char *t, char *dienst, size_t n, int &inst, const char *&pfad)
{
    const char *a = strchr(t, '/');
    size_t l = min((size_t)(a - t), n - 1);
    memcpy(dienst, t, l);
    dienst[l] = 0;
    inst = atoi(a + 1);
    pfad = strchr(a + 1, '/');
}

static void verteile(double &quelle, double &ziel, double &v)
{
    v = min(quelle, ziel);
    quelle -= v;
    ziel -= v;
}

// getdeltas(refreshbaseline) für diese Anlage: VE.Bus an AC-Eingang 1 = Netz (si1 1, si2 0),
// kein DC-System (shd 0), kein Generator, kein Multi-RS, kein Inverter, keine Lichtmaschine.
static void kwh_deltas(String &o, bool erneuern)
{
    const int n = vq_anzahl();
    // aktive Batterie (system/0/ActiveBatteryService), nur deren Zähler wie in kwhdeltas.py
    int bat = -1;
    {
        char roh[VQ_ROH];
        int i = vq_index("system/0/ActiveBatteryService");
        if (i >= 0 && vq_roh(i, roh, sizeof(roh))) {
            const char *p = strstr(roh, "battery/");
            if (p) {
                bat = atoi(p + 8);
            }
        }
    }
    double ve[10] = {};       // Reihenfolge wie ist_zaehler()
    double grid_in = 0, grid_out = 0, pvac_grid = 0, pvac_out = 0, pvdc = 0;
    double t_off = 0, t_gen = 0, t_grid = 0, t_inv = 0;
    int grid_zahl = 0;
    String roh_d;
    for (int i = 0; i < n; i++) {
        const char *t = vq_eintrag(i).thema;
        if (!ist_zaehler(t)) {
            continue;
        }
        char dienst[16];
        int inst;
        const char *pfad;
        dienst_inst(t, dienst, sizeof(dienst), inst, pfad);
        if (!strcmp(dienst, "battery") && inst != bat) {
            continue;
        }
        double v;
        bool da = vq_zahl(i, v);
        double d = 0;
        bool hat = false;
        if (da && !isnan(basis[i])) {
            d = max(v - basis[i], 0.0);
            hat = true;
        } else if (da) {
            basis[i] = v;       // erster Wert: ab hier zählen (DbusDeltas.add_service)
        }
        if (erneuern) {
            basis[i] = da ? v : NAN;
        }
        if (!hat) {
            continue;
        }
        char b[24];
        kwh_text(b, sizeof(b), d);
        char k[24];
        if (!strcmp(dienst, "vebus")) {
            static const char *const vp[] = {"AcIn1ToInverter", "AcIn2ToInverter", "AcIn1ToAcOut", "AcIn2ToAcOut",
                                             "InverterToAcIn1", "InverterToAcIn2", "AcOutToAcIn1", "AcOutToAcIn2",
                                             "InverterToAcOut", "OutToInverter"};
            for (int j = 0; j < 10; j++) {
                if (!strcmp(pfad + 8, vp[j])) {
                    ve[j] += d;
                }
            }
        } else if (!strcmp(dienst, "battery")) {
            snprintf(k, sizeof(k), strstr(pfad, "Discharged") ? "dH21[%d]" : "dH22[%d]", inst);
            paar(roh_d, k, b);
        } else if (!strcmp(dienst, "solarcharger")) {
            pvdc += d;
            snprintf(k, sizeof(k), "dYU[%d]", inst);
            paar(roh_d, k, b);
        } else if (!strcmp(dienst, "grid")) {
            bool vor = strstr(pfad, "Forward");
            if (vor) {
                grid_in += d;
                grid_zahl++;
            } else {
                grid_out += d;
            }
            snprintf(k, sizeof(k), vor ? "dgb[%d]" : "dgs[%d]", inst);
            paar(roh_d, k, b);
        } else if (!strcmp(dienst, "pvinverter")) {
            char pos[48];
            snprintf(pos, sizeof(pos), "pvinverter/%d/Position", inst);
            double p = 0;
            int pi = vq_index(pos);
            if (pi >= 0) {
                vq_zahl(pi, p);
            }
            (p == 1 ? pvac_out : pvac_grid) += d;       // 0 und 2 am Eingang, beide Netz
            snprintf(k, sizeof(k), "dpE[%d]", inst);
            paar(roh_d, k, b);
        } else if (!strcmp(dienst, "system")) {
            if (strstr(pfad, "TimeOff")) {
                t_off += d;
            } else if (strstr(pfad, "Generator")) {
                t_gen += d;
            } else if (strstr(pfad, "Grid")) {
                t_grid += d;
            } else {
                t_inv += d;
            }
        }
    }
    time_t alt = basis_zeit;
    if (erneuern) {
        basis_zeit = time(NULL);
    }
    if (o.length() == 0 && alt == 0) {
        return;     // nur Basis setzen
    }

    // _convertvebus_acin1and2_to_gensetandmains: beide Eingänge zählen als Netz
    double acgrid_to_dc = ve[0] + ve[1], acgrid_to_acout = ve[2] + ve[3];
    double dc_to_acgrid = ve[4] + ve[5], acout_to_acgrid = ve[6] + ve[7];
    double dc_to_acout = ve[8], acout_to_dc = ve[9];
    double grid_to_consumption = 0, pv_to_consumption = 0, bat_to_consumption = 0, pv_to_bat = 0, pv_to_grid = 0;
    double grid_to_bat = 0, bat_to_grid = 0;

    double dc_to_ac = dc_to_acgrid + dc_to_acout;
    double pvdc_to_bat = max(0.0, pvdc - dc_to_ac);
    double bat_in = max(0.0, dc_to_ac - pvdc);
    double bat_out = acgrid_to_dc + acout_to_dc;
    pvdc -= pvdc_to_bat;

    if (grid_zahl == 0) {
        grid_in = max(0.0, acgrid_to_dc + acgrid_to_acout - pvac_grid);
        grid_out = dc_to_acgrid + acout_to_acgrid;
    }
    double pvac_in_to_dc, pvac_in_to_grid, pvac_in_to_acout;
    verteile(pvac_grid, acgrid_to_dc, pvac_in_to_dc);
    verteile(pvac_grid, grid_out, pvac_in_to_grid);
    verteile(pvac_grid, acgrid_to_acout, pvac_in_to_acout);
    pv_to_consumption += pvac_grid + pvac_in_to_acout;
    pv_to_grid += pvac_in_to_grid;

    double grid_to_dc, grid_to_acout;
    verteile(grid_in, acgrid_to_dc, grid_to_dc);
    verteile(grid_in, acgrid_to_acout, grid_to_acout);
    grid_to_consumption += grid_in + grid_to_acout;

    double pvac_out_to_dc, pvac_out_to_acgrid;
    verteile(pvac_out, acout_to_dc, pvac_out_to_dc);
    verteile(pvac_out, acout_to_acgrid, pvac_out_to_acgrid);
    pv_to_consumption += pvac_out;

    double x;
    verteile(grid_to_dc, bat_out, x);
    grid_to_bat += x;
    grid_to_consumption += grid_to_dc;

    verteile(pvac_in_to_dc, bat_out, x);
    pv_to_consumption += pvac_in_to_dc;
    pv_to_bat += x;

    verteile(pvac_out_to_dc, bat_out, x);
    pv_to_consumption += pvac_out_to_dc;
    pv_to_bat += x;

    double bat_to_acout, bat_to_acin;
    verteile(bat_in, dc_to_acout, bat_to_acout);
    verteile(bat_in, dc_to_acgrid, bat_to_acin);
    bat_to_consumption += bat_to_acout;

    double pvdc_to_acout, pvdc_to_acin;
    verteile(pvdc, dc_to_acout, pvdc_to_acout);
    verteile(pvdc, dc_to_acgrid, pvdc_to_acin);
    pv_to_consumption += pvdc_to_acout;
    pv_to_bat += pvdc_to_bat;

    verteile(pvdc_to_acin, grid_out, x);
    pv_to_grid += x;
    pv_to_consumption += pvdc_to_acin;

    verteile(pvac_out_to_acgrid, grid_out, x);
    pv_to_consumption += pvac_out_to_acgrid;
    pv_to_grid += x;

    verteile(bat_to_acin, grid_out, x);
    bat_to_grid += x;
    bat_to_consumption += bat_to_acin;

    const struct {
        const char *k;
        double v;
    } erg[] = {{"Gc", grid_to_consumption}, {"Gb", grid_to_bat}, {"gc", 0}, {"gb", 0},
               {"Pb", pv_to_bat}, {"Pc", pv_to_consumption}, {"Pg", pv_to_grid}, {"Bc", bat_to_consumption},
               {"Bg", bat_to_grid}, {"To", t_off}, {"Tgs", t_gen}, {"Tg", t_grid}, {"Ti", t_inv}, {"Tga", 0}};
    for (auto &r : erg) {
        char b[24];
        kwh_text(b, sizeof(b), r.v);
        paar(o, r.k, b);
    }
    if (roh_d.length()) {
        o += '&';
        o += roh_d;
    }
    // Et: Beginn des Fensters in UTC, auf 15 min abgerundet
    time_t et = alt ? alt : time(NULL);
    struct tm u;
    gmtime_r(&et, &u);
    u.tm_min -= u.tm_min % 15;
    char b[16];
    snprintf(b, sizeof(b), "%04d%02d%02d%02d%02d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min);
    paar(o, "Et", b);
}

// ---------------------------------------------------------------- Warteschlange

struct Auftrag {
    time_t ts;
    uint8_t befehl;
    char *daten;        // urlencodiert, ohne d/IMEI/c, im PSRAM
    size_t len;
};

static std::deque<Auftrag> schlange;
static size_t schlange_bytes;
static uint32_t verworfen_zeilen;

static void einreihen(uint8_t befehl, const String &daten)
{
    Auftrag a;
    a.ts = time(NULL);
    a.befehl = befehl;
    a.len = daten.length();
    a.daten = (char *)heap_caps_malloc(a.len + 1, MALLOC_CAP_SPIRAM);
    if (!a.daten) {
        logf("VRM: kein Speicher für Zeile (%u Bytes)", (unsigned)a.len);
        return;
    }
    memcpy(a.daten, daten.c_str(), a.len + 1);
    lock();
    schlange.push_back(a);
    schlange_bytes += a.len;
    while (schlange_bytes > SCHLANGE_MAX && schlange.size() > 1) {
        Auftrag &v = schlange.front();
        schlange_bytes -= v.len;
        heap_caps_free(v.daten);
        schlange.pop_front();
        verworfen_zeilen++;
    }
    unlock();
}

static uint32_t nonce_n;
static char nonce_vor[5];

// vrm_encode() + vrm_encode_with_token() + remove_extraneous_characters()
static void zeile(String &o, const Auftrag &a, time_t jetzt)
{
    String z;
    z.reserve(a.len + 160);
    z = "d=2&IMEI=" VRM_PORTAL_ID "&c=";
    z += a.befehl;
    z += '&';
    z += a.daten;
    z += "&t=";
    z += INTERVALL;
    if (jetzt > a.ts) {
        z += "&TO=";
        z += (long)(jetzt - a.ts);
    }
    z += "&VRMAUTHTOKEN=" VRM_AUTH_TOKEN "&nonce=";
    z += nonce_vor;
    z += ':';
    z += ++nonce_n;
    z.replace("=0.0&", "=0&");
    o += z;
    o += '\n';
}

// ---------------------------------------------------------------- HTTP

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffff;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xedb88320 & -(c & 1));
        }
    }
    return ~c;
}

// zlib-Datenstrom mit ungepackten Blöcken (RFC 1950/1951, BTYPE 00)
static uint8_t *zlib_roh(const uint8_t *p, size_t n, size_t &aus)
{
    size_t bloecke = n / 65535 + 1;
    uint8_t *z = (uint8_t *)heap_caps_malloc(n + bloecke * 5 + 6, MALLOC_CAP_SPIRAM);
    if (!z) {
        return NULL;
    }
    size_t k = 0;
    z[k++] = 0x78;
    z[k++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    size_t rest = n;
    const uint8_t *q = p;
    do {
        uint16_t l = rest > 65535 ? 65535 : rest;
        z[k++] = rest <= 65535 ? 1 : 0;
        z[k++] = l & 0xff;
        z[k++] = l >> 8;
        z[k++] = ~l & 0xff;
        z[k++] = (uint16_t)~l >> 8;
        memcpy(z + k, q, l);
        k += l;
        q += l;
        rest -= l;
    } while (rest);
    uint32_t ad = b << 16 | a;
    z[k++] = ad >> 24;
    z[k++] = ad >> 16;
    z[k++] = ad >> 8;
    z[k++] = ad;
    aus = k;
    return z;
}

struct Antwort {
    int code = 0;
    String text;
    int retry_after = -1;
    String fehler;
};

static esp_err_t http_ereignis(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Retry-After")) {
        ((Antwort *)e->user_data)->retry_after = atoi(e->header_value);
    }
    return ESP_OK;
}

static Antwort post(const char *url, const uint8_t *body, size_t n, const char *typ, const char *agent)
{
    Antwort r;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 15000;
    cfg.cert_pem = CA;      // eigene CCGX-CA von Victron
    cfg.user_agent = agent;
    cfg.event_handler = http_ereignis;
    cfg.user_data = &r;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (typ) {
        esp_http_client_set_header(h, "Content-Type", typ);
    }
    esp_err_t err = esp_http_client_open(h, n);
    if (err != ESP_OK) {
        r.fehler = String("keine Verbindung: ") + esp_err_to_name(err);
    } else {
        size_t k = 0;
        while (k < n) {
            int w = esp_http_client_write(h, (const char *)body + k, n - k);
            if (w <= 0) {
                break;
            }
            k += w;
        }
        if (k < n) {
            r.fehler = "Senden abgebrochen";
        } else {
            esp_http_client_fetch_headers(h);
            r.code = esp_http_client_get_status_code(h);
            char buf[64] = {};
            int m = esp_http_client_read(h, buf, sizeof(buf) - 1);
            if (m > 0) {
                r.text = String(buf);
                r.text.trim();
            }
        }
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return r;
}

static uint32_t warten_bis_ms;
static uint32_t fehler_folge;

// Sendet den Anfang der Warteschlange als ein Bündel an logbulk.php
static void schlange_senden()
{
    if ((int32_t)(millis() - warten_bis_ms) < 0) {
        return;
    }
    lock();
    size_t anzahl = 0, bytes = 0;
    while (anzahl < schlange.size() && (anzahl == 0 || bytes + schlange[anzahl].len < BUENDEL_MAX)) {
        bytes += schlange[anzahl].len;
        anzahl++;
    }
    unlock();
    if (!anzahl) {
        return;
    }
    time_t jetzt = time(NULL);
    String roh;
    roh.reserve(bytes + anzahl * 200);
    lock();
    for (size_t i = 0; i < anzahl; i++) {
        zeile(roh, schlange[i], jetzt);
    }
    unlock();
    uint32_t crc = crc32((const uint8_t *)roh.c_str(), roh.length());
    size_t zn;
    uint8_t *z = zlib_roh((const uint8_t *)roh.c_str(), roh.length(), zn);
    if (!z) {
        return;
    }
    char url[128];
    snprintf(url, sizeof(url), "%s?crc32=%lu&id=%s", BULK_URL, (unsigned long)crc, VRM_PORTAL_ID);
    Antwort a = post(url, z, zn, NULL, "VE/CCGX/2");
    heap_caps_free(z);
    bool ok = a.code == 200 && a.text == "vrm: OK";
    String s = a.fehler.length() ? a.fehler : "HTTP " + String(a.code) + " \"" + a.text + "\"";
    if (a.retry_after >= 0) {
        s += ", Retry-After " + String(a.retry_after);
    }
    s += ", " + String(anzahl) + " Zeilen";
    setze(letzte_antwort, s);
    if (ok) {
        lock();
        for (size_t i = 0; i < anzahl && !schlange.empty(); i++) {
            schlange_bytes -= schlange.front().len;
            heap_caps_free(schlange.front().daten);
            schlange.pop_front();
        }
        gesendet_zeilen += anzahl;
        gesendet_posts++;
        letzte_ok = jetzt;
        unlock();
        fehler_folge = 0;
    } else {
        fehler_posts++;
        fehler_folge++;
        logf("VRM: senden fehlgeschlagen: %s", s.c_str());
        // wie http_endpoint.py mindestens 10 s, bei Dauerfehler länger
        warten_bis_ms = millis() + min(10000UL * fehler_folge, 300000UL);
    }
    if (a.retry_after >= 0) {
        warten_bis_ms = millis() + min(a.retry_after > 0 ? a.retry_after : 300, 900) * 1000UL;
    }
}

// ---------------------------------------------------------------- Zeilen bauen

static void konfig_alle(String &o)
{
    for (auto &k : KONST_KONFIG) {
        paar(o, k[0], k[1]);
    }
    char b[VQ_ROH];
    for (int i = 0; i < vq_anzahl(); i++) {
        vq_merker_nehmen(i, VQ_NEU_VK);
        if (vq_eintrag(i).art == VK && wert_text(i, b, sizeof(b))) {
            paar(o, vq_eintrag(i).code, b);
        }
    }
}

static void senddata(bool alle_vc)
{
    String o;
    o.reserve(10000);
    char b[VQ_ROH];
    int werte = 0;
    for (int i = 0; i < vq_anzahl(); i++) {
        const VrmEintrag &e = vq_eintrag(i);
        bool neu = vq_merker_nehmen(i, VQ_NEU_VC);
        if (e.art == VI || (e.art == VC && (neu || alle_vc))) {
            if (wert_text(i, b, sizeof(b))) {
                paar(o, e.code, b);
                werte++;
            }
        }
    }
    kwh_deltas(o, false);
    einreihen(SENDDATA, o);
    lock();
    letzte_daten = o;
    unlock();
    letzte_senddata = time(NULL);
    logf("VRM: SENDDATA mit %d Werten eingereiht", werte);
}

// ---------------------------------------------------------------- Brücke (Live-Ansicht)

static esp_mqtt_client_handle_t bruecke;
static volatile bool br_verbunden;
static volatile bool br_alles;              // Keepalive ohne suppress-republish: alles senden
static String br_echo;                      // full-publish-completed-echo
static volatile uint32_t br_anfrage_ms;     // letzte Nachricht von der VRM-Seite
static uint32_t br_r, br_w, br_keepalive, br_publiziert;
static String br_zustand = "aus";
#define PFX_N "N/" VRM_PORTAL_ID "/"
#define PFX_R "R/" VRM_PORTAL_ID "/"

static void br_pub(const char *pfad, const char *roh, bool retain = false)
{
    char t[128], p[VQ_ROH + 16];
    snprintf(t, sizeof(t), PFX_N "%s", pfad);
    snprintf(p, sizeof(p), "{\"value\":%s}", roh);
    esp_mqtt_client_publish(bruecke, t, p, 0, 0, retain);
    br_publiziert++;
}

static void br_ereignis(void *, esp_event_base_t, int32_t id, void *data)
{
    auto *e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        br_verbunden = true;
        setze(br_zustand, "verbunden");
        logf("VRM-Brücke: verbunden");
        esp_mqtt_client_subscribe(e->client, "R/" VRM_PORTAL_ID "/#", 0);
        esp_mqtt_client_subscribe(e->client, "W/" VRM_PORTAL_ID "/#", 0);
        esp_mqtt_client_publish(e->client, PFX_N "system/0/Serial", "{\"value\":\"" VRM_PORTAL_ID "\"}", 0, 0, 1);
        break;
    case MQTT_EVENT_DATA: {
        if (e->topic_len == 0) {
            break;
        }
        String t(e->topic, e->topic_len);
        String p(e->data, min(e->data_len, 300));
        br_anfrage_ms = millis();
        if (t.startsWith("W/")) {
            br_w++;
            t.replace(VRM_PORTAL_ID, "<ID>");
            logf("VRM-Brücke: Schreibwunsch NICHT ausgeführt: %s %s", t.c_str(), p.c_str());
            break;
        }
        if (!t.startsWith(PFX_R)) {
            break;
        }
        String pfad = t.substring(strlen(PFX_R));
        if (pfad == "keepalive") {
            br_keepalive++;
            if (p.indexOf("suppress-republish") < 0) {
                int k = p.indexOf("full-publish-completed-echo");
                lock();
                br_echo = "";
                if (k >= 0) {
                    int a = p.indexOf(':', k);
                    int b = p.indexOf('}', a);
                    if (a > 0 && b > a) {
                        br_echo = p.substring(a + 1, b);
                        br_echo.trim();
                    }
                }
                unlock();
                br_alles = true;
            }
            break;
        }
        br_r++;
        if (pfad == "system/0/Serial") {
            esp_mqtt_client_publish(e->client, PFX_N "system/0/Serial", "{\"value\":\"" VRM_PORTAL_ID "\"}", 0, 0, 0);
            break;
        }
        for (auto &k : EINSTELLUNGEN) {
            if (pfad == k[0]) {
                br_pub(k[0], k[1]);
                return;
            }
        }
        int i = vq_index(pfad.c_str());
        char roh[VQ_ROH];
        if (i >= 0 && vq_roh(i, roh, sizeof(roh))) {
            br_pub(pfad.c_str(), roh);
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        if (br_verbunden) {
            logf("VRM-Brücke: getrennt");
        }
        br_verbunden = false;
        setze(br_zustand, "getrennt");
        break;
    case MQTT_EVENT_ERROR: {
        auto *eh = e->error_handle;
        String s = "Fehler";
        if (eh->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            s += ", Broker lehnt ab, Code " + String((int)eh->connect_return_code);
        } else if (eh->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            s += ", TLS/TCP: esp_tls " + String(eh->esp_tls_last_esp_err) + ", errno " +
                 String(eh->esp_transport_sock_errno);
        }
        setze(br_zustand, s);
        break;
    }
    default:
        break;
    }
}

// Wie _get_vrm_broker_url() in mosquitto_bridge_registrator.py
static String broker_host()
{
    unsigned sum = 0;
    for (const char *p = VRM_PORTAL_ID; *p; p++) {
        sum += (unsigned char)tolower(*p);
    }
    return "mqtt" + String(sum % 128) + ".victronenergy.com";
}

static bool passwort_angemeldet;

static void passwort_anmelden()
{
    // Wie mosquitto_bridge_registrator.py: Benutzer ccgxapikey_<ID>, Passwort aus vrm_secrets.h
    String body;
    paar(body, "identifier", "ccgxapikey_" VRM_PORTAL_ID);
    paar(body, "mqttPassword", VRM_MQTT_PASSWORD);
    Antwort a = post(REG_URL, (const uint8_t *)body.c_str(), body.length(), "application/x-www-form-urlencoded",
                     "dbus-mqtt");
    passwort_angemeldet = a.code == 200;
    logf("VRM-Brücke: Passwort anmelden: %s",
         a.fehler.length() ? a.fehler.c_str() : ("HTTP " + String(a.code)).c_str());
}

static void bruecke_start()
{
    static String uri;
    static char cid[24];
    uri = "mqtts://" + broker_host() + ":443";
    snprintf(cid, sizeof(cid), "GXdbus_%08lx%04lx", (unsigned long)esp_random(), (unsigned long)(esp_random() & 0xffff));
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri.c_str();
    cfg.broker.verification.certificate = CA;
    cfg.credentials.username = "ccgxapikey_" VRM_PORTAL_ID;
    cfg.credentials.client_id = cid;
    cfg.credentials.authentication.password = VRM_MQTT_PASSWORD;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 30000;
    // Gemessen 19.11.2026: Stapel 2,8 kB benutzt, TLS-Aufbau eingeschlossen (mbedtls im PSRAM)
    cfg.buffer.size = 1024;
    cfg.buffer.out_size = 1024;
    cfg.task.stack_size = 5632;
    bruecke = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(bruecke, MQTT_EVENT_ANY, br_ereignis, NULL);
    esp_mqtt_client_start(bruecke);
    setze(br_zustand, "verbinde mit " + broker_host() + " als " + String(cid));
}

static void bruecke_stop()
{
    if (bruecke) {
        esp_mqtt_client_stop(bruecke);
        esp_mqtt_client_destroy(bruecke);
        bruecke = NULL;
    }
    br_verbunden = false;
    setze(br_zustand, "aus");
}

// Veröffentlicht Live-Werte: alle 3 s heartbeat, geänderte Werte, solange die VRM-Seite
// offen ist (Anfrage in den letzten 70 s), alles nach einem Keepalive
static void live_task(void *)
{
    uint32_t herz = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!bruecke || !br_verbunden) {
            continue;
        }
        bool aktiv = br_anfrage_ms && millis() - br_anfrage_ms < 70000;
        char roh[VQ_ROH];
        if (br_alles) {
            br_alles = false;
            for (int i = 0; i < vq_anzahl(); i++) {
                vq_merker_nehmen(i, VQ_NEU_LIVE);
                if (vq_roh(i, roh, sizeof(roh))) {
                    br_pub(vq_eintrag(i).thema, roh);
                }
            }
            for (auto &k : EINSTELLUNGEN) {
                br_pub(k[0], k[1]);
            }
            esp_mqtt_client_publish(bruecke, PFX_N "system/0/Serial", "{\"value\":\"" VRM_PORTAL_ID "\"}", 0, 0, 1);
            char p[96];
            lock();
            if (br_echo.length()) {
                snprintf(p, sizeof(p), "{\"value\":%ld,\"full-publish-completed-echo\":%s}", (long)time(NULL),
                         br_echo.c_str());
            } else {
                snprintf(p, sizeof(p), "{\"value\":%ld}", (long)time(NULL));
            }
            unlock();
            esp_mqtt_client_publish(bruecke, PFX_N "full_publish_completed", p, 0, 0, 0);
            logf("VRM-Brücke: alles gesendet (Keepalive)");
        } else {
            for (int i = 0; i < vq_anzahl(); i++) {
                if (vq_merker_nehmen(i, VQ_NEU_LIVE) && aktiv && vq_roh(i, roh, sizeof(roh))) {
                    br_pub(vq_eintrag(i).thema, roh);
                }
            }
        }
        if (millis() - herz >= 3000) {
            herz = millis();
            snprintf(roh, sizeof(roh), "%ld", (long)time(NULL));
            br_pub("heartbeat", roh);
        }
    }
}

// ---------------------------------------------------------------- Ablauf

static volatile bool post_busy;
static String post_result = "noch nicht gelaufen";

static void vrm_task(void *)
{
    bool angemeldet = false;
    time_t naechste_senddata = 0;
    long viertel = -1;
    uint32_t konfig_seit = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (post_busy) {
            // Verbindungstest
            String body = "d=2&IMEI=" VRM_PORTAL_ID "&c=6&t=" + String(INTERVALL) + "&VRMAUTHTOKEN=" VRM_AUTH_TOKEN;
            Antwort a = post(LOG_URL, (const uint8_t *)body.c_str(), body.length(), "application/x-www-form-urlencoded",
                             "VE/CCGX/2");
            bool ok = a.code == 200 && a.text == "vrm: OK";
            String s = a.fehler.length() ? a.fehler
                                         : String(ok ? "in Ordnung" : "abgelehnt") + ", HTTP " + a.code + ", \"" +
                                               a.text + "\"";
            setze(post_result, s);
            logf("VRM: Verbindungstest: %s", s.c_str());
            post_busy = false;
        }
        time_t jetzt = time(NULL);
        if (!an) {
            if (angemeldet) {
                bruecke_stop();
                angemeldet = false;
            }
            setze(zustand, "aus (Webseite)");
            continue;
        }
        if (WiFi.status() != WL_CONNECTED) {
            setze(zustand, "wartet auf WLAN");
            continue;
        }
        if (jetzt < 1700000000) {
            setze(zustand, "wartet auf Uhrzeit (NTP)");
            continue;
        }
        if (!angemeldet) {
            // Erst Werte sammeln: 60 s nach dem Verbinden mit der Quelle, damit Geräte und
            // Konfiguration schon in der ersten CONFIGCHANGE-Zeile stehen
            if (!vq_verbunden() || millis() - vq_verbunden_seit_ms() < 60000) {
                setze(zustand, "wartet auf Werte der Quelle");
                continue;
            }
            messpunkt("Quelle verbunden");
            if (!passwort_angemeldet) {
                passwort_anmelden();
                messpunkt("Passwort angemeldet (1. HTTPS)");
            }
            String o;
            for (auto &k : ANNOUNCE_DATEN) {
                paar(o, k[0], k[1]);
            }
            einreihen(ANNOUNCE, o);
            o = "";
            for (auto &k : PRIORITY_DATEN) {
                paar(o, k[0], k[1]);
            }
            einreihen(PRIORITY_CONFIGCHANGE, o);
            o = "";
            konfig_alle(o);
            einreihen(CONFIGCHANGE, o);
            o = "";
            kwh_deltas(o, true);        // Basis setzen
            senddata(true);
            naechste_senddata = jetzt + INTERVALL;
            viertel = jetzt / KWH_FENSTER;
            angemeldet = true;
            logf("VRM: ANNOUNCE, Konfiguration und erste Werte eingereiht");
            if (!bruecke) {
                bruecke_start();
            }
            vTaskDelay(pdMS_TO_TICKS(15000));     // Brücke und erstes Bündel abwarten
            schlange_senden();
            messpunkt("Brücke und Logger laufen");
        }
        setze(zustand, vq_verbunden() ? "sendet" : "sendet, Quelle getrennt");

        // Konfiguration geändert: wie SendConfig 5 s sammeln, dann eine Zeile
        bool vk = false;
        for (int i = 0; i < vq_anzahl() && !vk; i++) {
            if (vq_eintrag(i).art == VK && vq_merker_nehmen(i, VQ_NEU_VK)) {
                vk = true;
            }
        }
        if (vk && !konfig_seit) {
            konfig_seit = millis() | 1;
        }
        if (konfig_seit && millis() - konfig_seit >= 5000) {
            konfig_seit = 0;
            String o;
            konfig_alle(o);
            einreihen(CONFIGCHANGE, o);
            logf("VRM: Konfiguration geändert, CONFIGCHANGE eingereiht");
        }

        if (jetzt / KWH_FENSTER != viertel) {
            viertel = jetzt / KWH_FENSTER;
            String o;
            kwh_deltas(o, true);
            einreihen(HOURLYDELTAS, o);
            letzte_kwh = jetzt;
            logf("VRM: kWh (HOURLYDELTAS) eingereiht");
        }
        if (jetzt >= naechste_senddata) {
            naechste_senddata = jetzt + INTERVALL;
            // Ohne Quelle keine eingefrorenen Werte schicken
            if (vq_verbunden()) {
                senddata(false);
            } else {
                logf("VRM: Quelle getrennt, SENDDATA ausgelassen");
            }
        }
        schlange_senden();
    }
}

// mbedtls ist in Arduino auf internen RAM eingestellt (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC).
// Davon sind nur rund 50 kB frei; zwei TLS-Verbindungen scheiterten mit
// MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7F00). Deshalb alles aus dem PSRAM, wie es
// CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC täte; nur wenn der voll ist, intern.
static void *tls_calloc(size_t n, size_t g)
{
    size_t b = n * g;
    (void)b;
    void *p = heap_caps_calloc(n, g, MALLOC_CAP_SPIRAM);
    if (!p) {
        p = heap_caps_calloc(n, g, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}

static void tls_free(void *p) { heap_caps_free(p); }

void vrm_begin()
{
    if (mtx) {
        return;
    }
    mtx = xSemaphoreCreateMutex();
    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
#if !VRM_HAVE_SECRETS
    zustand = "src/vrm_secrets.h fehlt";
    logf("VRM: %s", zustand.c_str());
    return;
#endif
    const char *z = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 4; i++) {
        nonce_vor[i] = z[esp_random() % 62];
    }
    basis = (double *)heap_caps_malloc(vq_anzahl() * sizeof(double), MALLOC_CAP_SPIRAM);
    for (int i = 0; i < vq_anzahl(); i++) {
        basis[i] = NAN;
    }
    messpunkt("vor VRM");
    vq_begin();
    // Stapel im PSRAM (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM). Beide Tasks schreiben nie in
    // den Flash und laufen nicht in Interrupts.
    xTaskCreatePinnedToCoreWithCaps(vrm_task, "vrm", 12288, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    xTaskCreatePinnedToCoreWithCaps(live_task, "vrm_live", 6144, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    messpunkt("Tasks gestartet");
}

void vrm_senden(bool ein)
{
    an = ein;
    logf("VRM: senden %s (Webseite)", ein ? "an" : "aus");
}

void vrm_test_post()
{
    if (!mtx || post_busy) {
        return;
    }
    setze(post_result, "läuft …");
    post_busy = true;
}

void vrm_letzte_zeile(String &o)
{
    if (!mtx) {
        return;
    }
    lock();
    o = letzte_daten;
    unlock();
    o.replace("&", "\n");
}

static void zeit(String &o, const char *k, time_t t)
{
    json_int(o, k, t ? (long long)(time(NULL) - t) : -1);
}

void vrm_json(String &o)
{
    o += "{\"secrets\":";
    o += VRM_HAVE_SECRETS ? "true" : "false";
    if (!mtx) {
        o += "}";
        return;
    }
    o += ",\"an\":";
    o += an ? "true" : "false";
    o += ",\"quelle\":";
    vq_json(o);
    lock();
    o += ",\"zustand\":";
    json_str(o, zustand.c_str());
    o += ",\"antwort\":";
    json_str(o, letzte_antwort.c_str());
    o += ",\"post\":";
    json_str(o, post_result.c_str());
    o += ",";
    json_int(o, "schlange", schlange.size());
    o += ",";
    json_int(o, "schlange_kb", schlange_bytes / 1024);
    o += ",";
    json_int(o, "verworfen", verworfen_zeilen);
    o += ",";
    json_int(o, "zeilen", gesendet_zeilen);
    o += ",";
    json_int(o, "posts", gesendet_posts);
    o += ",";
    json_int(o, "fehler", fehler_posts);
    o += ",";
    zeit(o, "senddata_s", letzte_senddata);
    o += ",";
    zeit(o, "kwh_s", letzte_kwh);
    o += ",";
    zeit(o, "ok_s", letzte_ok);
    o += ",\"bruecke\":";
    json_str(o, br_zustand.c_str());
    o += ",\"ram\":[";
    for (int i = 0; i < messungen; i++) {
        char b[160];
        snprintf(b, sizeof(b), "%s{\"name\":\"%s\",\"frei\":%lu,\"tief\":%lu,\"block\":%lu}", i ? "," : "",
                 messung[i].name, (unsigned long)messung[i].frei, (unsigned long)messung[i].tief,
                 (unsigned long)messung[i].block);
        o += b;
    }
    o += "]";
    unlock();
    o += ",\"broker\":";
    json_str(o, broker_host().c_str());
    o += ",";
    json_int(o, "seite_s", br_anfrage_ms ? (long)((millis() - br_anfrage_ms) / 1000) : -1);
    o += ",";
    json_int(o, "r", br_r);
    o += ",";
    json_int(o, "w", br_w);
    o += ",";
    json_int(o, "keepalive", br_keepalive);
    o += ",";
    json_int(o, "publiziert", br_publiziert);
    o += "}";
}
