// Eintrag der Tabelle vrm_tabelle.h (erzeugt von tools/vrm_tabelle.py).
#pragma once

#include <stdint.h>
#include <stddef.h>

enum VrmArt : uint8_t {
    VN,     // kein VRM-Code: nur Live-Ansicht oder kWh-Zähler
    VI,     // mit jeder SENDDATA-Zeile (onIntervalAlways, onIntervalAlwaysAndOnEvent)
    VC,     // mit SENDDATA, nur nach Änderung (onIntervalOnlyWhenChanged)
    VK,     // CONFIGCHANGE, bei Änderung und nach ANNOUNCE (configChange)
};

enum VrmUmrechnung : uint8_t {
    UN,     // Wert wie er kommt
    UP,     // Produktkennung hexadezimal (conversions.productid)
    UT,     // fester Text (Firmware-Version, der Text kommt nicht über MQTT)
    UD,     // Sekunden in Tage
    UH,     // Sekunden in Stunden
    UA,     // Liste der Assistenten-IDs (conversions.assistant_code)
};

struct VrmEintrag {
    const char *thema;      // <dienst>/<instanz>/<pfad>, wie unter N/<Portal-ID>/
    const char *code;       // z. B. "IP1[288]", NULL ohne VRM-Code
    VrmArt art;
    VrmUmrechnung um;
    int8_t stellen;         // Nachkommastellen, -1 = wie geliefert
    const char *text;       // bei UT
};
