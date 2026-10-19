# RED SEA

**A weather-themed algorithmic MIDI sequencer and CC modulator, built on a single ESP32-C3.**

**RED SEA** generates continuously evolving MIDI Control Change modulation and step-sequenced notes, controlled by one of five algorithmic engines. It has a 128×32 OLED display, one encoder, four buttons, and standard DIN MIDI in/out. Built for hardware platform [**ESPidi** by Eugene Carlo](https://github.com/EugeneCarlo/ESPidi)

---

## Overview

***RED SEA*** sits between a MIDI clock source and connected gear. It generates Control Change modulation across four assignable CC parameters and drives a 16-step note/CC sequencer, both controlled by one of five selectable algorithmic engines:

- **FOG** — Morphing LFO engine (sine, triangle, or square). The SHAPE parameter changes the waveform itself rather than just skewing it: on sine it adds harmonics, or in the other direction clips the signal and adds noise; on triangle it shifts continuously toward a rising or falling sawtooth.
- **SUN** — refraction delay engine. When a parameter's value would exceed its range, it triggers a series of echo repeats instead of simply clamping, each one landing at half the previous interval. If a sequencer note fires on the same tick the echo series starts, the echoes also retrigger that note, transposed further with each repeat.
- **RAIN** — stochastic modulation engine. The four parameters can drift out of sync with the shared clock independently, occasionally affect a neighboring parameter, and be interrupted by randomized events that force a parameter to its minimum or maximum.
- **SNOW** — euclidian based random freezer. Mutation triggered by a Euclidean rhythm instead of a fixed interval. Each hit can also freeze or unfreeze any parameter on the device, including the engine's own parameters.
- **TWST** — debris engine driven by the MIDI input. Every incoming note plays immediately and scatters into a cloud of copies, spread in time and pitch. Each impact also knocks one of the four parameters away from its base value, which then settles back. Works without the sequencer running.

## Features

- Five algorithmic engines (FOG / SUN / RAIN / SNOW / TWST), each with four dedicated parameters
- 16-step sequencer (note, CC, a third assignable destination, per-step retrigger) with external or internal clock
- Each CC parameter has its own MIDI channel (or NO — sends nothing); sequencer notes go to every channel marked Seq Trig
- Note length mode: legato or gate
- Performance BYPASS and FREEZE modes, engaged on button press
- Freeze Roll: while FREEZE is held, the encoder repeats the current step from 1/4 down to 1/128
- MIDI Learn: captures the CC number and channel from the next incoming Control Change message
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

See [MANUAL.md](MANUAL.md) for the full pin-out, control gestures, and a page-by-page parameter reference. Русская версия: [README.ru.md](README.ru.md) / [MANUAL.ru.md](MANUAL.ru.md).

## Building & flashing

The firmware is a single Arduino sketch, `REDSEA.ino`, written for the Arduino core on ESP32. Build it with the Arduino IDE (board: ESP32C3 Dev Module) or `arduino-cli`. Dependencies: `Adafruit_GFX`, `Adafruit_SSD1306`, `Preferences` (bundled with the ESP32 core).

```bash
# arduino-cli, from a folder named REDSEA that contains REDSEA.ino
arduino-cli compile --fqbn esp32:esp32:esp32c3 REDSEA
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

`main` — first stable release. `beta` — per-parameter MIDI channels, note length, Freeze Roll and the TWST engine; under testing. What changed since 1.0: [CHANGELOG.md](CHANGELOG.md). Issues and pull requests welcome.

Updating from the first release: the old global MIDI channel is copied to all four parameters on first boot, so the device behaves as before until you change the routing.

## License

Firmware source code is licensed under the [MIT License](LICENSE).

Documentation (this file, the manual, and their Russian translations) is licensed under [CC BY 4.0](LICENSE-DOCS).

## Credits

- Firmware: Daniil Vlasenko
- **ESPidi** hardware platform: Eugene Carlo
