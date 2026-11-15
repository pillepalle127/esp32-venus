#include "dpl.h"

#include <Hoymiles.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>

#include "app.h"
#include "log.h"
#include "util.h"

#if __has_include("inverters.h")
#include "inverters.h"
#endif

static DplConfig cfg;
static DplStatus st;
static Preferences prefs;
static volatile int8_t req_enable = -1;
static uint32_t last_step = 0, limit_ms = 0;
static int8_t power_on = -1;            // zuletzt gesendet: 1 ein, 0 aus, -1 unbekannt
static int last_restart_yday = -1;

void dpl_begin()
{
    prefs.begin("dpl", false);
    st.enabled = prefs.getBool("on", false);
}

void dpl_set_enabled(bool on)
{
    req_enable = on ? 1 : 0;
}

// Nacht nach Sonnenauf- und -untergang (NOAA-Naeherung, Zenit 90,833 Grad)
static bool is_night(time_t t, float lat, float lon)
{
    struct tm g;
    gmtime_r(&t, &g);
    float gamma = 2 * M_PI / 365 * (g.tm_yday + (g.tm_hour - 12) / 24.0f);
    float eqtime = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma) -
                              0.014615f * cosf(2 * gamma) - 0.040849f * sinf(2 * gamma));
    float decl = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma) - 0.006758f * cosf(2 * gamma) +
                 0.000907f * sinf(2 * gamma) - 0.002697f * cosf(3 * gamma) + 0.00148f * sinf(3 * gamma);
    float latr = lat * M_PI / 180;
    float cosha = cosf(90.833f * M_PI / 180) / (cosf(latr) * cosf(decl)) - tanf(latr) * tanf(decl);
    if (cosha >= 1) {
        return true;
    }
    if (cosha <= -1) {
        return false;
    }
    float ha = acosf(cosha) * 180 / M_PI;
    float rise = 720 - 4 * (lon + ha) - eqtime;     // Minuten UTC
    float set = 720 - 4 * (lon - ha) - eqtime;
    float m = g.tm_hour * 60 + g.tm_min;
    return m < rise || m > set;
}

void dpl_step(uint32_t now, bool dtu)
{
#ifndef DPL_SERIAL
    st.state = "kein Wechselrichter festgelegt (DPL_SERIAL)";
    return;
#else
    auto iv = Hoymiles.getInverterBySerial(DPL_SERIAL);
    st.available = iv != nullptr;
    if (req_enable >= 0) {
        bool on = req_enable == 1;
        req_enable = -1;
        if (on != st.enabled) {
            st.enabled = on;
            prefs.putBool("on", on);
            logf("Regelung: %s", on ? "ein" : "aus");
        }
        power_on = -1;
        st.limit_w = NAN;
    }
    if (iv) {
        iv->setEnableCommands(st.enabled);
    }
    if (now - last_step < 1000) {
        return;
    }
    last_step = now;

    // Eingaenge, auch bei ausgeschalteter Regelung fuer die Anzeige
    if (sml_wp.has_power && sml_wp.telegrams) {
        st.meter_w = sml_wp.power_w;
        st.meter_age = now - sml_wp.last_ms;
    } else {
        st.meter_w = NAN;
        st.meter_age = -1;
    }
    if (iv) {
        auto s = iv->Statistics();
        uint32_t u = s->getLastUpdate();
        st.inv_age = u ? (long)(now - u) : -1;
        if (u) {
            st.pac = s->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);
            st.udc = s->getChannelFieldValue(TYPE_DC, CH0, FLD_UDC);
            st.ucorr = st.udc + st.pac * cfg.v_load_corr;
        }
        auto r = iv->SystemConfigPara()->getLastLimitCommandSuccess();
        st.last_cmd_ok = r == CMD_OK ? 0 : r == CMD_PENDING ? 1 : 2;
    }
    uint8_t d[8];
    uint32_t age;
    if (can_ok && can_mon.get(0x355, d, age)) {     // SoC vom JK-BMS, Victron-Format
        st.soc = d[0] | d[1] << 8;
        st.soc_age = age;
    } else {
        st.soc = NAN;
        st.soc_age = -1;
    }
    time_t t = time(nullptr);
    bool have_time = t > 1700000000;
    st.night = have_time && is_night(t, cfg.lat, cfg.lon);
    st.limit_age = limit_ms ? (long)(now - limit_ms) : -1;

    if (!iv) {
        st.state = "Wechselrichter nicht in der Geräteliste";
        return;
    }
    if (!st.enabled) {
        st.state = "aus";
        return;
    }
    if (!dtu) {
        st.state = "Abfrage unterbrochen, sendet nichts";
        return;
    }

    bool meter_ok = st.meter_age >= 0 && st.meter_age < 10000;
    bool inv_ok = st.inv_age >= 0 && st.inv_age < 10000;
    bool soc_ok = st.soc_age >= 0 && st.soc_age < 30000;

    // Batterie: mit SoC dessen Schwellen, sonst die Spannung. Die Stoppspannung gilt immer
    // zusaetzlich als Schutz.
    bool start = false, stop = false;
    if (soc_ok) {
        start = st.soc >= cfg.soc_start;
        stop = st.soc <= cfg.soc_stop;
    } else if (inv_ok) {
        start = st.ucorr >= cfg.v_start;
        stop = st.ucorr <= cfg.v_stop;
    }
    if (inv_ok && st.ucorr <= cfg.v_stop) {
        stop = true;
    }
    if (stop) {
        st.discharge = false;
    } else if (start || (cfg.always_at_night && st.night)) {
        st.discharge = true;
    }

    // Taeglicher Neustart des HMS, damit er den Tagesertrag zuruecksetzt. Nicht gleich nach
    // dem Start von ESP_venus, wenn die Stunde schon laeuft.
    if (cfg.restart_hour >= 0 && have_time) {
        struct tm lt;
        localtime_r(&t, &lt);
        if (last_restart_yday < 0) {
            last_restart_yday = lt.tm_hour >= cfg.restart_hour ? lt.tm_yday : -2;
        }
        if (lt.tm_hour == cfg.restart_hour && lt.tm_yday != last_restart_yday) {
            last_restart_yday = lt.tm_yday;
            iv->sendRestartControlRequest();
            st.restarts++;
            power_on = -1;
            st.limit_w = NAN;
            logf("Regelung: täglicher Neustart des HMS");
            return;
        }
    }

    if (!inv_ok) {
        st.state = "keine Daten vom HMS, sendet nichts";
        return;
    }
    if (!meter_ok) {
        st.state = "kein Zählerwert, sendet nichts";
        return;
    }
    if (st.last_cmd_ok == 1) {
        st.state = "wartet auf Antwort zum Limit";
        return;
    }

    // HMS hinter dem Zaehler: Er deckt seinen jetzigen Anteil plus den Bezug ueber dem Ziel
    float want = 0;
    if (st.discharge) {
        want = st.pac + st.meter_w - cfg.target_w;
        if (want > cfg.upper_w) {
            want = cfg.upper_w;
        }
    }
    if (want < cfg.lower_w) {
        if (cfg.allow_standby || !st.discharge) {
            if (power_on != 0) {
                iv->sendPowerControlRequest(false);
                power_on = 0;
                st.power_cmds++;
                st.limit_w = NAN;
                logf("Regelung: HMS aus (%s)", st.discharge ? "Bedarf unter dem kleinsten Limit" : "Batterie");
            }
            st.standby = true;
            st.state = st.discharge ? "Standby, Bedarf zu klein" : "Standby, Batterie unter der Schwelle";
            return;
        }
        want = cfg.lower_w;
    }
    if (power_on != 1) {
        iv->sendPowerControlRequest(true);
        power_on = 1;
        st.power_cmds++;
        logf("Regelung: HMS ein");
    }
    st.standby = false;
    if (isnan(st.limit_w) || fabsf(want - st.limit_w) > cfg.hysteresis_w) {
        iv->sendActivePowerControlRequest(want, PowerLimitControlType::AbsolutNonPersistent);
        st.limit_w = want;
        limit_ms = now;
        st.limits_sent++;
    }
    st.state = "regelt";
#endif
}

static void num(String &o, const char *key, float v, int dec)
{
    char b[40];
    if (isfinite(v)) {
        snprintf(b, sizeof(b), ",\"%s\":%.*f", key, dec, v);
    } else {
        snprintf(b, sizeof(b), ",\"%s\":null", key);
    }
    o += b;
}

void dpl_json(String &o)
{
    char b[200];
    snprintf(b, sizeof(b),
             "{\"on\":%s,\"avail\":%s,\"night\":%s,\"discharge\":%s,\"standby\":%s,\"meter_age\":%ld,\"inv_age\":%ld,"
             "\"soc_age\":%ld,\"limit_age\":%ld,\"limits\":%lu,\"power_cmds\":%lu,\"restarts\":%lu,\"cmd\":%d,\"state\":",
             st.enabled ? "true" : "false", st.available ? "true" : "false", st.night ? "true" : "false",
             st.discharge ? "true" : "false", st.standby ? "true" : "false", st.meter_age, st.inv_age, st.soc_age,
             st.limit_age, (unsigned long)st.limits_sent, (unsigned long)st.power_cmds, (unsigned long)st.restarts,
             st.last_cmd_ok);
    o += b;
    json_str(o, st.state);
    num(o, "meter", st.meter_w, 0);
    num(o, "pac", st.pac, 1);
    num(o, "udc", st.udc, 2);
    num(o, "ucorr", st.ucorr, 2);
    num(o, "soc", st.soc, 0);
    num(o, "limit", st.limit_w, 0);
    snprintf(b, sizeof(b),
             ",\"cfg\":{\"target\":%.0f,\"hyst\":%.0f,\"lower\":%.0f,\"upper\":%.0f,\"standby\":%s,\"soc_start\":%.0f,"
             "\"soc_stop\":%.0f,\"v_start\":%.1f,\"v_stop\":%.1f,\"corr\":%.3f,\"night\":%s,\"restart\":%d}}",
             cfg.target_w, cfg.hysteresis_w, cfg.lower_w, cfg.upper_w, cfg.allow_standby ? "true" : "false",
             cfg.soc_start, cfg.soc_stop, cfg.v_start, cfg.v_stop, cfg.v_load_corr, cfg.always_at_night ? "true" : "false",
             cfg.restart_hour);
    o += b;
}
