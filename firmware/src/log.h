// Protokoll: jede Zeile geht an die Konsole (UART0) und in einen Ringpuffer,
// den die Webseite liest. Meldungen der ESP-IDF-Treiber landen ebenfalls dort.
// Der Puffer uebersteht Neustart, Absturz und Watchdog, nicht aber Stromausfall.
#pragma once

#include <Arduino.h>

void log_init();
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Haengt alle Zeilen ab Nummer `from` als JSON-Liste an `out` an und liefert die
// Nummer der naechsten Zeile.
uint32_t log_json(uint32_t from, String &out);

// Alle Zeilen im Puffer als Text, eine je Zeile
void log_text(String &out);
