#include "sysinfo.h"

#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "log.h"
#include "util.h"

#define CONFIRM_MS  60000       // so lange muss neue Firmware mit WLAN laufen
#define NO_WIFI_MS  300000      // ohne WLAN so lange, dann zurueck zur vorherigen

// Arduino erklaert neue Firmware sonst gleich beim Start fuer gueltig. Mit true
// bleibt sie in der Pruefung, bis sys_firmware_check() sie bestaetigt.
extern "C" bool verifyRollbackLater()
{
    return true;
}

static bool coredump_present;

// Die ersten 16 Stellen der ELF-SHA-256, mit der sich die Firmware zuordnen laesst.
// Datum und Zeit in der App-Beschreibung stammen aus der vorgefertigten Bibliothek, nicht vom Build.
static void elf_id(const esp_app_desc_t &d, char *out)
{
    for (int i = 0; i < 8; i++) {
        snprintf(out + 2 * i, 3, "%02x", d.app_elf_sha256[i]);
    }
}
static const char *fw_state = "–";

const char *sys_reset_reason()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Einschalten";
    case ESP_RST_EXT: return "Reset-Pin";
    case ESP_RST_SW: return "Software";
    case ESP_RST_PANIC: return "Absturz";
    case ESP_RST_INT_WDT: return "Watchdog (Interrupt)";
    case ESP_RST_TASK_WDT: return "Watchdog (Task)";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung";
    case ESP_RST_DEEPSLEEP: return "Tiefschlaf";
    default: return "unbekannt";
    }
}

static const char *state_text(esp_ota_img_states_t st)
{
    switch (st) {
    case ESP_OTA_IMG_NEW: return "neu";
    case ESP_OTA_IMG_PENDING_VERIFY: return "in Pruefung";
    case ESP_OTA_IMG_VALID: return "bestaetigt";
    case ESP_OTA_IMG_INVALID: return "ungueltig";
    case ESP_OTA_IMG_ABORTED: return "abgebrochen";
    default: return "ohne Zustand";
    }
}

void sys_boot_report()
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[17];
    elf_id(*esp_app_get_description(), sha);    // esp_app_get_elf_sha256() liefert nur 9 Stellen
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK) {
        fw_state = state_text(st);
    } else {
        fw_state = "ohne Zustand (per USB geflasht)";
    }
    extern const char BUILD[];
    logf("Start: Grund %s, Firmware vom %s auf %s, ELF %s, %s", sys_reset_reason(), BUILD, run ? run->label : "?",
         sha, fw_state);

    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (bad) {
        esp_app_desc_t bd;
        if (esp_ota_get_partition_description(bad, &bd) == ESP_OK) {
            char id[17];
            elf_id(bd, id);
            logf("Rueckfall: Firmware ELF %s auf %s lief nicht an, der Bootloader hat die vorherige gestartet", id,
                 bad->label);
        } else {
            logf("Rueckfall: Firmware auf %s lief nicht an", bad->label);
        }
    }

    coredump_present = esp_core_dump_image_check() == ESP_OK;
    if (coredump_present) {
        esp_core_dump_summary_t *s = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
        if (s && esp_core_dump_get_summary(s) == ESP_OK) {
            char bt[16 * 11 + 1];
            size_t n = 0;
            bt[0] = 0;
            for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16 && n + 12 < sizeof(bt); i++) {
                n += snprintf(bt + n, sizeof(bt) - n, " 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
            }
            logf("Absturz im Flash: Task %s, PC 0x%08lx, ELF %.16s", s->exc_task, (unsigned long)s->exc_pc,
                 (const char *)s->app_elf_sha256);
            logf("Absturz Backtrace:%s%s", bt, s->exc_bt_info.corrupted ? " (beschaedigt)" : "");
        } else {
            logf("Absturz im Flash, Zusammenfassung nicht lesbar");
        }
        free(s);
    }
}

void sys_firmware_check(bool wifi_ok)
{
    static bool done = false;
    static uint32_t wifi_since = 0;
    if (done) {
        return;
    }
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK ||
        st != ESP_OTA_IMG_PENDING_VERIFY) {
        done = true;
        return;
    }
    uint32_t now = millis();
    if (wifi_ok) {
        if (wifi_since == 0) {
            wifi_since = now ? now : 1;
            logf("Firmware: neu, wird nach %u s mit WLAN bestaetigt. Ein Neustart vorher startet die vorherige.",
                 CONFIRM_MS / 1000);
        }
        if (now - wifi_since >= CONFIRM_MS) {
            esp_ota_mark_app_valid_cancel_rollback();
            fw_state = "bestaetigt";
            logf("Firmware: bestaetigt");
            done = true;
        }
    } else {
        wifi_since = 0;
        if (now >= NO_WIFI_MS) {
            logf("Firmware: %u min ohne WLAN, zurueck zur vorherigen", NO_WIFI_MS / 60000);
            delay(500);
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}

void sys_json(String &o)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    char sha[17];
    elf_id(*esp_app_get_description(), sha);    // esp_app_get_elf_sha256() liefert nur 9 Stellen
    char b[160];
    snprintf(b, sizeof(b), ",\"part\":\"%s\",\"fwstate\":\"%s\",\"elf\":\"%s\",\"coredump\":%s,\"other\":",
             run ? run->label : "?", fw_state, sha, coredump_present ? "true" : "false");
    o += b;
    esp_app_desc_t od;
    if (other && esp_ota_get_partition_description(other, &od) == ESP_OK) {
        char id[17];
        elf_id(od, id);
        snprintf(b, sizeof(b), "\"%s, ELF %s\"", other->label, id);
        o += b;
    } else {
        o += "null";
    }
}

bool sys_boot_other(String &msg)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t od;
    if (!other || esp_ota_get_partition_description(other, &od) != ESP_OK) {
        msg = "keine vorherige Firmware";
        return false;
    }
    esp_err_t e = esp_ota_set_boot_partition(other);
    if (e != ESP_OK) {
        msg = esp_err_to_name(e);
        return false;
    }
    char id[17];
    elf_id(od, id);
    logf("Firmware: naechster Start mit ELF %s auf %s", id, other->label);
    msg = "ok";
    return true;
}

bool sys_coredump(uint32_t &addr, uint32_t &size)
{
    size_t a = 0, s = 0;
    if (esp_core_dump_image_get(&a, &s) != ESP_OK) {
        return false;
    }
    addr = a;
    size = s;
    return true;
}

void sys_coredump_erase()
{
    esp_core_dump_image_erase();
    coredump_present = false;
    logf("Absturz im Flash geloescht");
}
