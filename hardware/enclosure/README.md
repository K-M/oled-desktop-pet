# Enclosure

<!-- MEDIA: render or photo of the printed parts -->
![Enclosure parts](../../docs/media/enclosure.jpg)

| File | Part |
|---|---|
| `base.stl` | Body: NodeMCU, speaker module and power input on the floor; OLED window on the front wall |
| `lid.stl` | Screw-on lid; the touch sensor sits against its inside face |
| `desktop_pet.f3d` | Fusion source |
| `desktop_pet.step` | Neutral CAD format |

## Print settings

| Setting | Value |
|---|---|
| Material | |
| Layer height | |
| Infill | |
| Supports | |
| Orientation | |

## Design notes

- **Touch through the lid.** The QT1010 senses through the plastic, so the pad must sit flat against
  the lid with no air gap. The lid wall at the touch spot is __ mm.
- **Display window.** The window is sized to the panel's active area, not the PCB; the glass seats in a
  pocket behind a lip.
- **Fasteners:**
