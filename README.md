# RED SEA

**A weather-themed algorithmic MIDI sequencer and CC modulator, built on a single ESP32-C3.**

**RED SEA** generates continuously evolving MIDI Control Change modulation and step-sequenced notes, controlled by one of four algorithmic engines. It has a 128×32 OLED display, one encoder, four buttons, and standard DIN MIDI in/out. Built for hardware platform [**ESPidi** by Eugene Carlo](https://github.com/EugeneCarlo/ESPidi)

---

## Overview

***RED SEA*** sits between a MIDI clock source and connected gear. It generates Control Change modulation across four assignable CC parameters and drives a 16-step note/CC sequencer, both controlled by one of four selectable algorithmic engines:

- **FOG** — Morphing LFO engine (sine, triangle, or square). The SHAPE parameter changes the waveform itself rather than just skewing it: on sine it adds harmonics, or in the other direction clips the signal and adds noise; on triangle it shifts continuously toward a rising or falling sawtooth.
- **SUN** — refraction delay engine. When a parameter's value would exceed its range, it triggers a series of echo repeats instead of simply clamping, each one landing at half the previous interval. If a sequencer note fires on the same tick the echo series starts, the echoes also retrigger that note, transposed further with each repeat.
- **RAIN** — stochastic modulation engine. The four parameters can drift out of sync with the shared clock independently, occasionally affect a neighboring parameter, and be interrupted by randomized events that force a parameter to its minimum or maximum.
- **SNOW** — euclidian based random freezer. Mutation triggered by a Euclidean rhythm instead of a fixed interval. Each hit can also freeze or unfreeze any parameter on the device, including the engine's own parameters.

## Features

- Four algorithmic engines (FOG / SUN / RAIN / SNOW), each with four dedicated parameters
- 16-step sequencer (note, CC, a third assignable destination, per-step retrigger) with external or internal clock
- Performance BYPASS and FREEZE modes, engaged on button press
- MIDI Learn: captures a CC number from the next incoming Control Change message
- Per-parameter freeze, independent of the global performance modes
- Settings are written to flash and persist across power cycles

## Hardware

This firmware was developed for the **ESPidi** hardware platform, designed by Eugene Carlo. It doesn't require that specific board, though — anyone can build a compatible device by wiring an ESP32-C3, an SSD1306 128×32 OLED, a rotary encoder, and three buttons according to the pinout below.

| | |
|---|---|
| MCU | ESP32-C3 |
| Display | SSD1306 128×32 OLED, I²C |
| Input | 1× rotary encoder w/ push button, 3× tactile buttons (PLAY / TAP / PAGE) |
| MIDI | Standard 31250 baud DIN in/out over UART |
| Power | USB-C |

See [MANUAL.md](MANUAL.md) for the full pin-out, control gestures, and a page-by-page parameter reference.

## Building & flashing

The firmware is a single `.cpp` translation unit (not an `.ino` sketch), written for the Arduino core on ESP32. Build with PlatformIO or the Arduino IDE configured for an ESP32-C3 board. Dependencies: `Adafruit_GFX`, `Adafruit_SSD1306`, `Preferences` (bundled with the ESP32 core).

```bash
# PlatformIO
pio run -t upload
```

Wiring reference (see `Pins::` in the source for the authoritative list):

| Signal | GPIO |
|---|---|
| PLAY | 1 |
| TAP | 2 |
| PAGE | 3 |
| Encoder switch | 4 |
| Encoder A / B | 5 / 6 |
| MIDI RX / TX | 7 / 10 |
| OLED SDA / SCL | 8 / 9 |

## Status

Released — first stable version. Issues and pull requests welcome.

## License

Firmware source code is licensed under the [MIT License](LICENSE).

Documentation (this file, the manual, and their Russian translations) is licensed under [CC BY 4.0](LICENSE-DOCS).

## Credits

- Firmware: Daniil Vlasenko
- **ESPidi** hardware platform: Eugene Carlo
