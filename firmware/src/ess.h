// ESS-Regler fuer den MultiPlus: vom Netzwert (MT681 Haus) zum Sollwert ueber VE.Bus.
// Sollwert in RAM-Variable 131 des ESS-Assistenten: positiv = Einspeisen (Batterie entlaedt),
// negativ = Laden (Quelle: esp32ess, docs/README.md).
// Ohne "senden" rechnet der Regler nur mit (Schattenbetrieb), der S3 bleibt stumm.
#pragma once

#include <Arduino.h>

enum EssMode : uint8_t { ESS_OFF = 0, ESS_ZERO = 1, ESS_FIXED = 2 };

struct EssConfig {
    uint8_t mode = ESS_ZERO;
    float target_w = 20;        // Ziel am Hauszaehler, Bezug positiv
    float fixed_w = 0;          // Sollwert im Modus "fest"
    float max_dis_w = 2000;     // groesste Einspeisung
    float max_ch_w = 2000;      // groesste Ladeleistung aus dem Netz
    float soc_min = 20;         // darunter keine Entladung (SoC vom JK-BMS)
    float gain = 0.5;           // Anteil der Abweichung, der je Schritt nachgefuehrt wird
    float deadband_w = 10;      // kleinere Abweichungen nicht nachfuehren
    float track_w = 500;        // Sollwert hoechstens so weit neben der gemessenen Einspeisung, 0 aus
    float jump_w = 0;           // groessere Abweichung: Sprung auf MultiPlus-Leistung plus Abweichung, 0 aus
                                // (mit 150 W Dauerschwingung, 17.11.: Rueckmeldung bis 1 s alt, MP2 braucht 1-2 s)
};

struct EssStatus {
    bool send = false;          // Sollwert wird gesendet (gespeichert)
    const char *state = "aus";
    float grid_w = NAN;         // Hauszaehler, Bezug positiv
    long grid_age = -1;
    float soc = NAN, bat_v = NAN, bat_i = NAN;
    float cvl = NAN, ccl = NAN, dcl = NAN, dvl = NAN;
    long bms_age = -1;
    float lim_dis_w = 0, lim_ch_w = 0;  // aktuelle Grenzen nach BMS, SoC und Einstellung
    float sp_w = 0;             // berechneter Sollwert
    uint32_t sent = 0, acked = 0;
    long sent_age = -1;
    float mp_v = NAN, mp_inv = NAN, mp_mains = NAN, mp_out = NAN;   // RAM 4, 14, 15, 16, nur beim Senden
    float ac_out_w = 0;         // AC-out-Last, mit der die Grenzen verschoben werden
    const char *sw_now = "unbekannt";   // Schalter laut Statusframe des MultiPlus
    int sw_cmd = -1;            // zuletzt gesendeter Schalterbefehl
    long mp_age = -1;
};

void ess_begin();
// Aenderungen aus der Webseite; leere Werte (NAN, -1) bleiben unveraendert
void ess_set(int send, int mode, float target, float fixed, float soc_min, float max_dis, float max_ch);
void ess_switch(const char *m);    // "ein", "laden", "wr", "aus"
void ess_json(String &out);
