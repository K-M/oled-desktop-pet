# Wiring


## Pin map (NodeMCU 1.0 / ESP-12E)

| NodeMCU | GPIO | Connects to | Notes |
|---|---|---|---|
| D5 | 14 | OLED SCK (D0) | Hardware SPI clock |
| D7 | 13 | OLED MOSI (D1/SDA) | Hardware SPI data |
| D8 | 15 | OLED CS | Boot-strap pin; the NodeMCU's on-board pull-down holds it low at boot |
| D2 | 4 | OLED DC | |
| D1 | 5 | OLED RES | |
| D6 | 12 | Touch OUT | Also SPI MISO; reclaimed as an input after the display starts |
| RX | 3 | Audio filter input | I2S data out; Serial becomes transmit-only |
| 3V3 | — | OLED VCC, Touch VDD | |
| VIN | — | Speaker module VCC | 5 V from USB |
| GND | — | All grounds | Common ground is required |

Free pins: D0 (GPIO16). Avoid D3/D4 (GPIO0/GPIO2): they must be high at boot.

## Audio filter

![Audio filter schematic](audio_filter_schematic.png)

Vector version: [`audio_filter_schematic.svg`](audio_filter_schematic.svg). As built: R1 = 10 kΩ, R2 = 1 kΩ,
C1 = 22 nF, C2 = 100 µF.

The resistors attenuate 3.3 V to ~0.3 V peak-to-peak (÷11) for the LM386 and, with C, form a low-pass
filter that removes the delta-sigma carrier. The cutoff is f = 1 / (2π · 909 Ω · C).

| C | Cutoff | Result |
|---|---|---|
| 22 nF (or 2 × 10 nF in parallel) | 8.0–8.8 kHz | Reference design |
| 10 nF | 17.5 kHz | Fine |
| 47 nF | 3.7 kHz | Voice slightly muffled |
| 100 nF with 2.2 kΩ / 220 Ω instead | 7.2 kHz | Equivalent to the reference |

Fit a **100 µF** electrolytic across the speaker module's VCC and GND to keep amplifier current spikes off
the display supply.

## Volume

1. The module's trimmer, if fitted.
2. `MASTER_VOLUME` in `PetConfig.h` (0–0.7; above ~0.7 the 2nd-order modulator can become unstable).
3. A smaller resistor to ground: 470 Ω ≈ −6.5 dB, 220 Ω ≈ −13 dB.
