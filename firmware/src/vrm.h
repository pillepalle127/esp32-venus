// VRM, Schritt V2 (docs/vrm.md): ESP_venus meldet selbst an VRM, wie der vrmlogger
// und die MQTT-Brücke in Venus OS v3.75.
// - Logger: ANNOUNCE, PRIORITY_CONFIGCHANGE, CONFIGCHANGE, alle 300 s SENDDATA, alle 15 min
//   kWh (HOURLYDELTAS), gebündelt an logbulk.php. Was nicht ankommt, bleibt in der
//   Warteschlange (nur im RAM).
// - Brücke: MQTT 3.1.1 zu mqtt<n>.victronenergy.com:443 für die Live-Ansicht. Beantwortet
//   Leseanfragen (R), führt Schreibwünsche (W) nie aus.
// Werte: vrm_quelle.h. Zugangsdaten: vrm_secrets.h (nicht im Repo).
#pragma once

#include <Arduino.h>

void vrm_begin();               // startet Quelle, Logger und Brücke (braucht WLAN)
void vrm_senden(bool an);       // Logger und Brücke an oder aus (nicht gespeichert, Start: an)
void vrm_test_post();           // Verbindungstest (TEST_POST) im Hintergrund
void vrm_json(String &o);       // Zustand für /api/status, als JSON-Objekt
void vrm_letzte_zeile(String &o);  // letzte SENDDATA-Zeile, ein Wert je Zeile, ohne Token
