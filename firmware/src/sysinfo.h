// Betrieb nur ueber WLAN: neue Firmware erst bestaetigen, wenn sie mit WLAN laeuft,
// sonst startet der Bootloader wieder die vorherige. Absturzinfo (Coredump im Flash)
// ins Log und zum Herunterladen.
#pragma once

#include <Arduino.h>

const char *sys_reset_reason();
void sys_boot_report();                 // nach log_init: Grund, Firmware, Absturz, Rueckfall
void sys_firmware_check(bool wifi_ok);  // regelmaessig aus der Netz-Task
void sys_json(String &o);               // Felder fuer /api/status, beginnt mit Komma
bool sys_boot_other(String &msg);       // vorherige Firmware fuer den naechsten Start waehlen
bool sys_coredump(uint32_t &addr, uint32_t &size);   // Lage des Coredumps im Flash
void sys_coredump_erase();
