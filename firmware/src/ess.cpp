#include "ess.h"

#include <Preferences.h>
#include <math.h>

#include "app.h"
#include "log.h"
#include "util.h"

static EssConfig cfg;
static EssStatus st;
static Preferences prefs;
static SemaphoreHandle_t mtx;
static uint32_t last_tel = 0, last_step = 0, sent_ms = 0, ram_ms = 0, mp_ms = 0;

static bool tx_on = false;
static volatile int sw_req = -1;        // angeforderter Schalterzustand (VEBusSwitchState), -1 keiner
static uint32_t sw_until = 0;

static const char *sw_name(int v)
{
    return v == VEBUS_SWITCH_STATE_ON ? "ein" : v == VEBUS_SWITCH_STATE_CHARGER_ONLY ? "nur Laden"
         : v == VEBUS_SWITCH_STATE_INVERTER_ONLY ? "nur Wechselrichter" : v == VEBUS_SWITCH_STATE_OFF ? "aus" : "–";
}

static void addf(String &o, const char *key, double v, int dec)
{
    char b[48];
    if (isfinite(v)) {
        snprintf(b, sizeof(b), ",\"%s\":%.*f", key, dec, v);
    } else {
        snprintf(b, sizeof(b), ",\"%s\":null", key);
    }
    o += b;
}

static const char *mode_name(uint8_t m)
{
    return m == ESS_ZERO ? "null" : m == ESS_FIXED ? "fest" : "aus";
}

static void save()
{
    prefs.putUChar("mode", cfg.mode);
    prefs.putFloat("target", cfg.target_w);
    prefs.putFloat("fixed", cfg.fixed_w);
    prefs.putFloat("socmin", cfg.soc_min);
    prefs.putFloat("maxdis", cfg.max_dis_w);
    prefs.putFloat("maxch", cfg.max_ch_w);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Liest die BMS-Werte im Victron-Format aus dem CAN-Mitschnitt
static void read_bms(uint32_t now)
{
    uint8_t d[8];
    uint32_t a351 = 0, a355 = 0, a356 = 0;
    bool ok = can_ok && can_mon.get(0x351, d, a351);
    if (ok) {
        st.cvl = (uint16_t)(d[0] | d[1] << 8) / 10.0f;
        st.ccl = (int16_t)(d[2] | d[3] << 8) / 10.0f;
        st.dcl = (int16_t)(d[4] | d[5] << 8) / 10.0f;
        st.dvl = (uint16_t)(d[6] | d[7] << 8) / 10.0f;
    }
    ok = ok && can_mon.get(0x355, d, a355);
    if (ok) {
        st.soc = (uint16_t)(d[0] | d[1] << 8);
    }
    ok = ok && can_mon.get(0x356, d, a356);
    if (ok) {
        st.bat_v = (int16_t)(d[0] | d[1] << 8) / 100.0f;
        st.bat_i = (int16_t)(d[2] | d[3] << 8) / 10.0f;
    }
    if (ok) {
        uint32_t a = a351 > a355 ? a351 : a355;
        st.bms_age = a > a356 ? a : a356;
    } else {
        st.bms_age = -1;
        st.soc = st.bat_v = st.bat_i = st.cvl = st.ccl = st.dcl = st.dvl = NAN;
    }
}

static void step(uint32_t now)
{
    bool tel_new = sml_house.telegrams != last_tel;
    // Neuer Zaehlerwert, sonst spaetestens jede Sekunde (Sollwert auffrischen, Grenzen pruefen)
    if (!tel_new && now - last_step < 1000) {
        return;
    }
    if (now - last_step < 500) {
        return;
    }
    last_step = now;
    last_tel = sml_house.telegrams;

    if (sml_house.has_power && sml_house.telegrams) {
        st.grid_w = sml_house.power_w;
        st.grid_age = now - sml_house.last_ms;
    } else {
        st.grid_w = NAN;
        st.grid_age = -1;
    }
    read_bms(now);

    bool grid_ok = st.grid_age >= 0 && st.grid_age < 5000;
    bool bms_ok = st.bms_age >= 0 && st.bms_age < 10000;
    bool vebus_ok = left_mode == LEFT_VEBUS && !vebus.hasNoSync();

    // Grenzen: Einstellung, BMS-Strom mal Spannung, Mindest-SoC, Spannungsgrenzen
    float dis = cfg.max_dis_w, ch = cfg.max_ch_w;
    const char *why = nullptr;
    if (!bms_ok) {
        dis = ch = 0;
        why = "keine BMS-Daten (CAN)";
    } else {
        dis = fminf(dis, fmaxf(st.dcl, 0) * st.bat_v);
        ch = fminf(ch, fmaxf(st.ccl, 0) * st.bat_v);
        if (st.soc <= cfg.soc_min) {
            dis = 0;
        }
        if (st.bat_v <= st.dvl) {
            dis = 0;
        }
        if (st.bat_v >= st.cvl) {
            ch = 0;
        }
    }
    st.lim_dis_w = dis;
    st.lim_ch_w = ch;

    // Rueckmeldung des MultiPlus (RAM lesen, nur beim Senden): Wechselrichter- und Ausgangsleistung.
    // Batterie entlaedt mit Sollwert (AC-in, + einspeisen) plus AC-out-Last. Damit gelten die
    // Batteriegrenzen fuer den Sollwert verschoben um die AC-out-Last.
    bool mp_fresh = mp_ms && now - mp_ms < 3000 && isfinite(st.mp_out) && isfinite(st.mp_inv);
    float out = mp_fresh ? st.mp_out : 0;
    float hi = dis - out, lo = -ch - out;
    const char *neutral = mp_fresh ? nullptr : " (AC-out unbekannt, 0 W)";

    float sp = st.sp_w;
    if (!vebus_ok) {
        why = left_mode == LEFT_VEBUS ? "kein VE.Bus-Sync" : "linker Kanal nicht auf VE.Bus";
        sp = 0;
    } else if (cfg.mode == ESS_OFF) {
        sp = 0;
    } else if (cfg.mode == ESS_FIXED) {
        sp = cfg.fixed_w;
    } else if (!grid_ok) {
        why = "kein Wert vom Hauszaehler, Batterie ruht";
        sp = -out;      // AC-in deckt AC-out, die Batterie ruht
    } else {
        float err = st.grid_w - cfg.target_w;      // Bezug ueber dem Ziel: mehr einspeisen
        if (cfg.jump_w > 0 && fabsf(err) > cfg.jump_w && mp_fresh && st.send) {
            // Grosse Abweichung: direkt auf die jetzige Einspeisung an AC-in plus die Abweichung.
            // mp_inv ist negativ, wenn der MultiPlus aus der Batterie liefert.
            sp = -st.mp_inv - out + err;
        } else if (fabsf(err) > cfg.deadband_w) {
            sp += cfg.gain * err;
        }
    }
    // Gegen Aufwickeln: Der MP2 folgt nur mit rund 300-400 W/s (17.11.). Der Sollwert bleibt
    // deshalb hoechstens cfg.track_w neben dem, was er gerade an AC-in tatsaechlich liefert.
    if (cfg.mode == ESS_ZERO && grid_ok && mp_fresh && st.send && cfg.track_w > 0) {
        float feed = -st.mp_inv - out;
        sp = clampf(sp, feed - cfg.track_w, feed + cfg.track_w);
    }
    st.sp_w = roundf(clampf(sp, lo, hi));
    st.ac_out_w = out;

    if (cfg.mode == ESS_OFF) {
        st.state = "aus";
    } else if (why) {
        st.state = why;
    } else if (dis == 0 && cfg.mode == ESS_ZERO && st.grid_w > cfg.target_w + cfg.deadband_w) {
        st.state = mp_fresh ? "Entladen gesperrt (SoC, Spannung oder BMS)" : "Entladen gesperrt, AC-out unbekannt";
    } else if (st.sp_w >= hi && dis > 0 && cfg.mode == ESS_ZERO) {
        st.state = "an der Entladegrenze";
    } else if (st.sp_w <= lo && ch > 0 && cfg.mode == ESS_ZERO) {
        st.state = "an der Ladegrenze";
    } else {
        st.state = st.send ? "regelt" : "rechnet nur (Schatten)";
    }
    (void)neutral;

    // Schalter Ein / Nur Laden / Nur Wechselrichter / Aus: einmal senden, dafuer kurz TX frei
    if (sw_req >= 0 && vebus_ok) {
        if (!tx_on) {
            vebus.setTxEnabled(true);
            tx_on = true;
        }
        vebus.setSwitchState((VEBusSwitchState)sw_req);
        logf("ESS: Schalter MultiPlus auf %s gesendet", sw_name(sw_req));
        st.sw_cmd = sw_req;
        sw_req = -1;
        sw_until = now + 1500;
    }

    // Senden: nur mit Sync und nicht im Modus aus
    bool want_tx = vebus_ok && ((st.send && cfg.mode != ESS_OFF) || (int32_t)(now - sw_until) < 0);
    if (want_tx != tx_on) {
        tx_on = want_tx;
        if (left_mode == LEFT_VEBUS) {
            vebus.setTxEnabled(want_tx);
        }
        logf("ESS: Senden %s", want_tx ? "ein" : "aus");
    }
    if (want_tx && st.send && cfg.mode != ESS_OFF) {
        if (vebus.isAcked()) {
            st.acked++;
            vebus.clearAcked();
        }
        vebus.setESSPower((int16_t)st.sp_w);
        st.sent++;
        sent_ms = now;
        if (now - ram_ms >= 1000) {
            ram_ms = now;
            static const uint8_t ids[] = {VEBUS_RAM_UBAT, VEBUS_RAM_INVERTER_POWER, VEBUS_RAM_MAINS_POWER,
                                          VEBUS_RAM_OUTPUT_POWER};
            vebus.readRAMVars(ids, sizeof(ids));
        }
        if (vebus.hasRAMVarResponse()) {
            vebus.clearRAMVarResponse();
            for (uint8_t k = 0; k < vebus.getRAMVarCount(); k++) {
                int16_t v = vebus.getRAMVarValue(k);
                switch (vebus.getRAMVarId(k)) {
                case VEBUS_RAM_UBAT: st.mp_v = v / 100.0f; break;
                case VEBUS_RAM_INVERTER_POWER: st.mp_inv = v; break;
                case VEBUS_RAM_MAINS_POWER: st.mp_mains = v; break;
                case VEBUS_RAM_OUTPUT_POWER: st.mp_out = v; break;
                }
            }
            mp_ms = now;
        }
    }
    if (vebus_ok) {
        uint8_t r = vebus.getSwitchRegister();
        bool c = r & VEBUS_SWITCH_CHARGE, iv = r & VEBUS_SWITCH_INVERT;
        st.sw_now = c && iv ? "ein" : c ? "nur Laden" : iv ? "nur Wechselrichter" : "aus";
    } else {
        st.sw_now = "unbekannt";
    }
    st.sent_age = sent_ms ? (long)(now - sent_ms) : -1;
    st.mp_age = mp_ms ? (long)(now - mp_ms) : -1;
}

static void task(void *)
{
    while (true) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        step(millis());
        xSemaphoreGive(mtx);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ess_begin()
{
    mtx = xSemaphoreCreateMutex();
    prefs.begin("ess", false);
    cfg.mode = prefs.getUChar("mode", cfg.mode);
    cfg.target_w = prefs.getFloat("target", cfg.target_w);
    cfg.fixed_w = prefs.getFloat("fixed", cfg.fixed_w);
    cfg.soc_min = prefs.getFloat("socmin", cfg.soc_min);
    cfg.max_dis_w = prefs.getFloat("maxdis", cfg.max_dis_w);
    cfg.max_ch_w = prefs.getFloat("maxch", cfg.max_ch_w);
    st.send = prefs.getBool("send", false);
    logf("ESS: Modus %s, Ziel %.0f W, fest %.0f W, Mindest-SoC %.0f %%, max. %.0f W einspeisen, %.0f W laden, "
         "Senden %s",
         mode_name(cfg.mode), cfg.target_w, cfg.fixed_w, cfg.soc_min, cfg.max_dis_w, cfg.max_ch_w,
         st.send ? "ein (gespeichert)" : "aus");
    xTaskCreatePinnedToCore(task, "ess", 4096, NULL, 2, NULL, 0);     // Core 0, VE.Bus hat Core 1
}

void ess_set(int send, int mode, float target, float fixed, float soc_min, float max_dis, float max_ch)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool changed = false;
    if (mode >= ESS_OFF && mode <= ESS_FIXED && mode != cfg.mode) {
        cfg.mode = mode;
        st.sp_w = 0;            // Regler startet bei 0 W
        changed = true;
    }
    if (!isnan(target)) {
        cfg.target_w = clampf(target, -500, 500);
        changed = true;
    }
    if (!isnan(fixed)) {
        cfg.fixed_w = clampf(fixed, -5000, 5000);
        changed = true;
    }
    if (!isnan(soc_min)) {
        cfg.soc_min = clampf(soc_min, 5, 100);
        changed = true;
    }
    if (!isnan(max_dis)) {
        cfg.max_dis_w = clampf(max_dis, 0, 5000);
        changed = true;
    }
    if (!isnan(max_ch)) {
        cfg.max_ch_w = clampf(max_ch, 0, 5000);
        changed = true;
    }
    if (changed) {
        save();
        logf("ESS: Modus %s, Ziel %.0f W, fest %.0f W, Mindest-SoC %.0f %%, max. %.0f W einspeisen, %.0f W laden",
             mode_name(cfg.mode), cfg.target_w, cfg.fixed_w, cfg.soc_min, cfg.max_dis_w, cfg.max_ch_w);
    }
    if (send >= 0 && (send == 1) != st.send) {
        st.send = send == 1;
        if (st.send) {
            st.sp_w = 0;        // beim Einschalten bei 0 W beginnen
            st.sent = st.acked = 0;
        }
        prefs.putBool("send", st.send);
        logf("ESS: Sollwert senden %s (von der Seite)", st.send ? "ein" : "aus");
    }
    last_step = 0;
    xSemaphoreGive(mtx);
}

void ess_switch(const char *m)
{
    int v = !strcmp(m, "ein") ? VEBUS_SWITCH_STATE_ON : !strcmp(m, "laden") ? VEBUS_SWITCH_STATE_CHARGER_ONLY
          : !strcmp(m, "wr") ? VEBUS_SWITCH_STATE_INVERTER_ONLY : !strcmp(m, "aus") ? VEBUS_SWITCH_STATE_OFF : -1;
    if (v >= 0) {
        sw_req = v;
    }
}

void ess_json(String &o)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    char b[160];
    snprintf(b, sizeof(b), "{\"send\":%s,\"tx\":%s,\"mode\":\"%s\",\"state\":", st.send ? "true" : "false",
             tx_on ? "true" : "false", mode_name(cfg.mode));
    o += b;
    json_str(o, st.state);
    addf(o, "grid", st.grid_w, 0);
    snprintf(b, sizeof(b), ",\"grid_age\":%ld,\"bms_age\":%ld,\"sent\":%lu,\"acked\":%lu,\"sent_age\":%ld,\"mp_age\":%ld",
             st.grid_age, st.bms_age, (unsigned long)st.sent, (unsigned long)st.acked, st.sent_age, st.mp_age);
    o += b;
    addf(o, "soc", st.soc, 0);
    addf(o, "v", st.bat_v, 2);
    addf(o, "i", st.bat_i, 1);
    addf(o, "cvl", st.cvl, 1);
    addf(o, "ccl", st.ccl, 1);
    addf(o, "dcl", st.dcl, 1);
    addf(o, "dvl", st.dvl, 1);
    addf(o, "lim_dis", st.lim_dis_w, 0);
    addf(o, "lim_ch", st.lim_ch_w, 0);
    addf(o, "sp", st.sp_w, 0);
    addf(o, "mp_v", st.mp_v, 2);
    addf(o, "mp_inv", st.mp_inv, 0);
    addf(o, "mp_mains", st.mp_mains, 0);
    addf(o, "mp_out", st.mp_out, 0);
    addf(o, "ac_out", st.ac_out_w, 0);
    o += ",\"sw\":";
    json_str(o, st.sw_now);
    o += ",\"sw_cmd\":";
    json_str(o, sw_name(st.sw_cmd));
    snprintf(b, sizeof(b), ",\"cfg\":{\"target\":%.0f,\"fixed\":%.0f,\"soc_min\":%.0f,\"max_dis\":%.0f,\"max_ch\":%.0f}}",
             cfg.target_w, cfg.fixed_w, cfg.soc_min, cfg.max_dis_w, cfg.max_ch_w);
    o += b;
    xSemaphoreGive(mtx);
}
