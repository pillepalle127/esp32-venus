#include "bme280.h"

void Bme280::parse_calib(const uint8_t *c, const uint8_t *h)
{
    cal.T1 = (uint16_t)(c[1] << 8 | c[0]);
    cal.T2 = (int16_t)(c[3] << 8 | c[2]);
    cal.T3 = (int16_t)(c[5] << 8 | c[4]);
    cal.P1 = (uint16_t)(c[7] << 8 | c[6]);
    cal.P2 = (int16_t)(c[9] << 8 | c[8]);
    cal.P3 = (int16_t)(c[11] << 8 | c[10]);
    cal.P4 = (int16_t)(c[13] << 8 | c[12]);
    cal.P5 = (int16_t)(c[15] << 8 | c[14]);
    cal.P6 = (int16_t)(c[17] << 8 | c[16]);
    cal.P7 = (int16_t)(c[19] << 8 | c[18]);
    cal.P8 = (int16_t)(c[21] << 8 | c[20]);
    cal.P9 = (int16_t)(c[23] << 8 | c[22]);
    cal.H1 = c[25];                                   // 0xA1; 0xA0 ist unbenutzt
    cal.H2 = (int16_t)(h[1] << 8 | h[0]);             // 0xE1, 0xE2
    cal.H3 = h[2];                                    // 0xE3
    cal.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));   // 0xE4, 0xE5[3:0]
    cal.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));     // 0xE6, 0xE5[7:4]
    cal.H6 = (int8_t)h[6];                            // 0xE7
}

// Datenblatt BME280, Abschnitt 4.2.3: Ganzzahl-Formeln
void Bme280::compensate(int32_t adc_T, int32_t adc_P, int32_t adc_H)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)cal.T1 << 1))) * ((int32_t)cal.T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)cal.T1)) * ((adc_T >> 4) - ((int32_t)cal.T1))) >> 12) *
                    ((int32_t)cal.T3)) >> 14;
    int32_t t_fine = var1 + var2;
    temp_c = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    int64_t p1 = ((int64_t)t_fine) - 128000;
    int64_t p2 = p1 * p1 * (int64_t)cal.P6;
    p2 = p2 + ((p1 * (int64_t)cal.P5) << 17);
    p2 = p2 + (((int64_t)cal.P4) << 35);
    p1 = ((p1 * p1 * (int64_t)cal.P3) >> 8) + ((p1 * (int64_t)cal.P2) << 12);
    p1 = (((((int64_t)1) << 47) + p1)) * ((int64_t)cal.P1) >> 33;
    if (p1 != 0) {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p2) * 3125) / p1;
        int64_t v1 = (((int64_t)cal.P9) * (p >> 13) * (p >> 13)) >> 25;
        int64_t v2 = (((int64_t)cal.P8) * p) >> 19;
        p = ((p + v1 + v2) >> 8) + (((int64_t)cal.P7) << 4);
        press_hpa = (float)((double)p / 25600.0);
    } else {
        press_hpa = 0;
    }

    if (chip_id != 0x60) {
        hum_pct = 0;
        return;
    }
    int32_t v = (t_fine - ((int32_t)76800));
    v = (((((adc_H << 14) - (((int32_t)cal.H4) << 20) - (((int32_t)cal.H5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)cal.H6)) >> 10) * (((v * ((int32_t)cal.H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)cal.H2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)cal.H1)) >> 4));
    v = (v < 0 ? 0 : v);
    v = (v > 419430400 ? 419430400 : v);
    hum_pct = (float)(uint32_t)(v >> 12) / 1024.0f;
}

#ifdef ARDUINO
bool Bme280::read_regs(uint8_t reg, uint8_t *buf, uint8_t n)
{
    wire->beginTransmission(addr);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) {
        return false;
    }
    if (wire->requestFrom(addr, n) != n) {
        return false;
    }
    for (uint8_t i = 0; i < n; i++) {
        buf[i] = wire->read();
    }
    return true;
}

bool Bme280::write_reg(uint8_t reg, uint8_t val)
{
    wire->beginTransmission(addr);
    wire->write(reg);
    wire->write(val);
    return wire->endTransmission() == 0;
}

bool Bme280::begin(TwoWire &w, uint8_t a)
{
    wire = &w;
    addr = a;
    ok = false;
    uint8_t id = 0;
    if (!read_regs(0xD0, &id, 1) || (id != 0x60 && id != 0x58)) {
        chip_id = id;
        return false;
    }
    chip_id = id;
    write_reg(0xE0, 0xB6);    // Soft-Reset
    delay(5);
    uint8_t c1[26], c2[7] = {};
    if (!read_regs(0x88, c1, 26) || (chip_id == 0x60 && !read_regs(0xE1, c2, 7))) {
        return false;
    }
    parse_calib(c1, c2);
    // Feuchte x1, Standby 1 s ohne Filter, Temperatur und Druck x1, Normalbetrieb.
    // ctrl_hum gilt erst nach dem Schreiben von ctrl_meas.
    ok = write_reg(0xF2, 0x01) && write_reg(0xF5, 0xA0) && write_reg(0xF4, 0x27);
    return ok;
}

bool Bme280::read()
{
    uint8_t d[8];
    if (!ok) {
        return false;          // nicht gefunden, kein Lesefehler
    }
    if (!read_regs(0xF7, d, 8)) {
        errors++;
        ok = false;
        return false;
    }
    int32_t adc_p = (int32_t)((uint32_t)d[0] << 12 | (uint32_t)d[1] << 4 | d[2] >> 4);
    int32_t adc_t = (int32_t)((uint32_t)d[3] << 12 | (uint32_t)d[4] << 4 | d[5] >> 4);
    int32_t adc_h = (int32_t)((uint32_t)d[6] << 8 | d[7]);
    if (adc_t == 0x80000) {    // noch keine Messung
        return false;
    }
    compensate(adc_t, adc_p, adc_h);
    return true;
}
#endif
