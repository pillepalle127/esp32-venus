# Stückliste esp_venus_klein_usbfrei.kicad_pcb

Erzeugt aus der Platinendatei. Gleiche Teile mit gleichem Wert sind zusammengefasst.

| Anzahl | Art | Teil | Wert | Bezeichner | Hinweis |
|---|---|---|---|---|---|
| 4 | Modul | ADuM1201-Modul, 15 × 10 mm |  | U3 (ADuM1201 Pack 1), U4 (ADuM1201 Pack 2), U5 (ADuM1201 VE.Bus), U6 (ADuM1201 CAN) | Stiftleisten 2 × 4 |
| 1 | Modul | CMT2300A, Modul 10298-V1.0 |  | U11 | SMD aufgelötet, Antenne an der IPX-Buchse |
| 1 | Modul | ESP32-S3-Board N16R8 mit 2 × USB-C |  | U1 (ESP32-S3 N16R8) | Stiftleisten 2 × 22 direkt eingelötet |
| 3 | Modul | RS485-Modul MAX13487 „RS485 V2.05“, etwa 18 × 12,6 mm |  | U7 (RS485 Pack 1), U8 (RS485 Pack 2), U9 (RS485 VE.Bus) | Stiftleisten 1 × 5 und 1 × 3 |
| 1 | Modul | SC16IS752 (CJMCU-752), Quarz 1,8432 MHz |  | U2 (SC16IS752) | Stiftleisten 1 × 10 und 2 × 8 |
| 1 | Modul | TJA1050-Modul, 22 × 11 mm |  | U10 (TJA1050) | Stiftleiste 1 × 4 und 2 Stifte |
| 1 | Modul | USB-C-Modul 12 × 13,8 mm (V, D−, D+, G) |  | X8 (USB-C CH348) | Stiftleiste 1 × 4 |
| 1 | Modul | W5500-Modul USR-ES1 (W5500 Lite) |  | U12 (W5500) | Stiftleisten 2 × 6 |
| 4 | Bauteil | DC/DC-Wandler B0505S-1WR3, SIP-4 |  | PS1 (B0505S Pack 1), PS2 (B0505S Pack 2), PS3 (B0505S VE.Bus), PS4 (B0505S CAN) | THT |
| 2 | Bauteil | Diode S1M (SMA) oder 1N4148W (SOD-123) | S1M oder 1N4148W | D1, D2 | SMD |
| 1 | Bauteil | Keramik-C 0805 | 100n | C9 | SMD |
| 1 | Bauteil | Keramik-C 0805 | 100n CMT | C10 | SMD |
| 4 | Bauteil | Keramik-C 0805 | 10u Cout | C2, C4, C6, C8 | SMD |
| 1 | Bauteil | Keramik-C 0805 | 10u Vin | C11 | SMD |
| 4 | Bauteil | Keramik-C 0805 | 4,7u Cin | C1, C3, C5, C7 | SMD |
| 1 | Bauteil | MOSFET, SOT-23 | AO3401A | Q3 | SMD |
| 3 | Bauteil | MOSFET, SOT-23 | AO3402 | Q1, Q2, Q4 | SMD |
| 1 | Bauteil | Spannungsregler LM1117-3.3, SOT-223 | LM1117-3.3 | U13 | SMD, optional, nur mit JP1 auf 2-3 |
| 1 | Bauteil | Tantal-C, Bauform B (3528) | 10u Tantal | C12 | SMD, optional, nur mit U13 |
| 3 | Bauteil | Widerstand 0805 | 100R | R5, R7, R9 | SMD |
| 1 | Bauteil | Widerstand 0805 | 100k | R12 | SMD |
| 4 | Bauteil | Widerstand 0805 | 10k | R6, R8, R10, R11 | SMD |
| 4 | Bauteil | Widerstand 0805 | unbestückt, bei Bedarf 250 Ω | R1, R2, R3, R4 | SMD |
| 1 | Stecker | RJ45-Buchse liegend, Bauform Amphenol 54602 | RJ45 VE.Bus | J1 | THT |
| 3 | Stecker | Stiftleiste 1 × 2, Raster 2,54 mm |  | X1, X6, X7 | THT |
| 4 | Stecker | Stiftleiste 1 × 3, Raster 2,54 mm |  | X5, X11, X12, X13 | THT |
| 3 | Stecker | Stiftleiste 1 × 4, Raster 2,54 mm |  | X2, X3, X4 | THT |
| 1 | Stecker | Stiftleiste 1 × 8, Raster 2,54 mm |  | X10 | THT |
| 1 | Lötbrücke | Lötbrücke, 3 Pads | 3V3: 1-2 S3, 2-3 U13 | JP1 | kein Bauteil, mit Zinn schließen |
