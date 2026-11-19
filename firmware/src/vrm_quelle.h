// Werte für VRM, Übergang (docs/vrm.md, Schritt 4): ESP_venus hört am MQTT-Broker des Pi
// mit (N/<ID des Pi>/...), nur lesend. Es sendet dort nichts, auch kein Keepalive; das
// macht ioBroker. Später kommen die Werte von ESP_venus selbst, die Tabelle bleibt.
#pragma once

#include <Arduino.h>

#include "vrm_tab_typ.h"

// Merker je Wert, werden beim Empfang gesetzt und vom Nutzer gelöscht
enum : uint8_t {
    VQ_NEU_VC = 1,      // für SENDDATA (onIntervalOnlyWhenChanged)
    VQ_NEU_VK = 2,      // für CONFIGCHANGE
    VQ_NEU_LIVE = 4,    // für die Live-Ansicht
};

// Längster gespeicherter Wert (JSON-Text). vebus/288/Devices/0/Assistants ist eine Liste
// von Bytes, deutlich länger als 48 Zeichen.
static const int VQ_ROH = 256;

void vq_begin();
int vq_anzahl();
const VrmEintrag &vq_eintrag(int i);
int vq_index(const char *thema);                    // -1, wenn nicht in der Tabelle
bool vq_zahl(int i, double &v);                     // false ohne gültigen Zahlenwert
bool vq_roh(int i, char *buf, size_t n);            // JSON-Text des Werts, false ohne Wert
bool vq_merker_nehmen(int i, uint8_t merker);       // true, wenn gesetzt war; löscht ihn
bool vq_verbunden();
uint32_t vq_verbunden_seit_ms();                    // millis() beim Verbinden
void vq_json(String &o);
