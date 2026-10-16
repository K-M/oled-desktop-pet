# Bill of materials

| Qty | Part | Specification | Notes |
|---|---|---|---|
| 1 | ESP8266 NodeMCU | ESP-12E, CP2102 or CH340 USB | Any NodeMCU 1.0-style board |
| 1 | OLED display | 1.3", 128×64, SH1106, 4-wire SPI, 7-pin | An SSD1309 module also works with a one-line constructor change |
| 1 | Capacitive touch sensor | Adafruit AT42QT1010 breakout (#1374) | Active-high, momentary |
| 1 | Speaker module | LM386 amplifier with on-board speaker and volume trimmer | 4–5.5 V supply |
| 1 | Resistor | 10 kΩ, 1/4 W | Audio series resistor |
| 1 | Resistor | 1 kΩ, 1/4 W | Audio divider to ground |
| 1 | Capacitor | 22 nF ceramic (code 223) | Audio low-pass (C1); alternatives in WIRING.md |
| 1 | Capacitor | 100 µF, ≥ 10 V electrolytic | Amplifier supply decoupling (C2) |
| — | Hook-up wire | Dupont jumpers or soldered | |
| 1 | USB-C breakout | Generic USB-C receptacle breakout with 5.1 kΩ CC pull-downs | Power input: VBUS → 5 V rail, GND |
| 1 | Enclosure | 3D-printed body + lid, PLA | See `enclosure/` |
| 4 | Screws | M4 × 60 mm | Lid to body |
| — | Screws | M2.5 × 8 mm | All electronics (touch sensor and display bezel use 4 each) |
| 1 | USB cable | USB-C | Power |
| 1 | USB cable | Micro-USB | Flashing / serial monitor only |
