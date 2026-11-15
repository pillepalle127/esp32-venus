// Regelung des WP-HMS nach dem Vorbild des Dynamic Power Limiter (DPL) von OpenDTU-onBattery.
// Werte aus der Config der OpenDTU WP. Der HMS haengt hinter dem WP-Zaehler (MT681 WP) und
// speist aus der Batterie so viel ein, dass am Zaehler das Ziel (25 W Bezug) bleibt.
// Laeuft in der Funk-Task, weil nur dort Befehle an die Hoymiles-Bibliothek gehen duerfen.
#pragma once

#include <Arduino.h>

struct DplConfig {
    float target_w = 25;            // Ziel am WP-Zaehler, Bezug positiv
    float hysteresis_w = 3;         // neues Limit nur bei mehr Abweichung
    float lower_w = 66;             // kleinstes Limit, darunter Standby
    float upper_w = 2100;
    bool allow_standby = true;
    float soc_start = 30, soc_stop = 25;    // %
    float v_start = 53, v_stop = 48;        // V, gilt ohne SoC
    float v_load_corr = 0.002;      // V je W: Spannung unter Last auf Leerlauf hochrechnen
    bool always_at_night = true;    // nachts reicht es, ueber der Stoppschwelle zu sein
    int restart_hour = 4;           // taeglicher Neustart des HMS, -1 aus
    float lat = 51.1657, lon = 10.4515;     // fuer Sonnenauf- und -untergang
};

struct DplStatus {
    bool enabled = false;           // Knopf auf der Seite, gespeichert
    bool available = false;         // DPL_SERIAL gesetzt und Wechselrichter angelegt
    const char *state = "aus";
    float meter_w = NAN;
    long meter_age = -1;
    float pac = NAN, udc = NAN, ucorr = NAN, soc = NAN;
    long inv_age = -1, soc_age = -1;
    bool night = false;
    bool discharge = false;         // Batterie darf entladen
    bool standby = false;           // HMS per Befehl ausgeschaltet
    float limit_w = NAN;            // zuletzt gesendetes Limit
    long limit_age = -1;
    uint32_t limits_sent = 0, power_cmds = 0, restarts = 0;
    int last_cmd_ok = -1;           // Ergebnis des letzten Limit-Befehls: 0 ok, 1 laeuft, 2 Fehler
};

void dpl_begin();
void dpl_set_enabled(bool on);      // aus der Webseite
void dpl_step(uint32_t now, bool dtu);  // aus der Funk-Task; sendet nur im DTU-Betrieb (dtu true)
void dpl_json(String &out);
