// VRM, Schritt V1 (docs/vrm.md, docs/testplan.md Nr. 12): nur Verbindung pruefen.
// - Verbindungstest: TEST_POST (c=6) an ccgxlogging.victronenergy.com, erwartet "vrm: OK".
// - MQTT mithoeren: Anmeldung bei mqtt<n>.victronenergy.com, abonniert R/<ID>/#, sendet nichts,
//   trennt nach der eingestellten Zeit von selbst.
// Zugangsdaten in vrm_secrets.h (nicht im Repo), vom Pi uebernommen.
#pragma once

#include <Arduino.h>

void vrm_test_post();           // startet den Verbindungstest im Hintergrund
void vrm_listen(int seconds);   // MQTT mithoeren, 10 bis 300 s
void vrm_stop();                // MQTT sofort trennen
void vrm_json(String &o);       // Zustand fuer /api/status, als JSON-Objekt
