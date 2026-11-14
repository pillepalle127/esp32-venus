// BME280 (und BMP280 ohne Feuchte) ueber I2C, Kompensation nach Bosch-Datenblatt.
#pragma once

#include <stdint.h>

#ifdef ARDUINO
#include <Wire.h>
#endif

class Bme280 {
public:
    struct Calib {
        uint16_t T1;
        int16_t T2, T3;
        uint16_t P1;
        int16_t P2, P3, P4, P5, P6, P7, P8, P9;
        uint8_t H1, H3;
        int16_t H2, H4, H5;
        int8_t H6;
    };

#ifdef ARDUINO
    // Prueft die Chip-ID, liest die Kalibrierung und startet den Normalbetrieb.
    bool begin(TwoWire &wire, uint8_t addr);
    // Liest die letzte Messung. false, wenn der Sensor nicht antwortet.
    bool read();
#endif

    // Kompensation aus Rohwerten; setzt temp_c, hum_pct, press_hpa
    void compensate(int32_t adc_t, int32_t adc_p, int32_t adc_h);
    // Kalibrierung aus den Registerbloecken 0x88..0xA1 (26 Bytes) und 0xE1..0xE7 (7 Bytes)
    void parse_calib(const uint8_t *c1, const uint8_t *c2);

    uint8_t addr = 0;
    uint8_t chip_id = 0;      // 0x60 BME280, 0x58 BMP280
    bool ok = false;
    uint32_t errors = 0;
    float temp_c = 0, hum_pct = 0, press_hpa = 0;
    Calib cal = {};

private:
#ifdef ARDUINO
    bool read_regs(uint8_t reg, uint8_t *buf, uint8_t n);
    bool write_reg(uint8_t reg, uint8_t val);
    TwoWire *wire = nullptr;
#endif
};
