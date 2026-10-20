# Changelog

Changes in RED SEA compared to version 1.0 (released 2026-08-17). Русская версия: [CHANGELOG.ru.md](CHANGELOG.ru.md).

## 1.1 — beta

Branch `beta`, under testing. The [manual](MANUAL.md) describes this version.

### New

- **Per-parameter MIDI channels.** Each of P1–P4 sends its CC on its own channel, set on the MIDI page: `NO` or 1–16. `NO` sends nothing at all.

  ![MIDI page — channels](docs/screens/midi_channels.png)

- **Seq Trig.** A flag per parameter (TAP + turn on the MIDI page, shown as a note icon). Sequencer notes, the sequencer's CC column, SUN arpeggio notes and TWST notes go to every channel with Seq Trig on; repeated channels count once.
- **Note length — LEN.** `LEG`: a note sounds until the next one, as in 1.0. `GATE`: a note ends halfway to the next hit. Applies to sequencer notes and retriggers, Freeze Roll, SUN arpeggio and TWST.
- **MIDI Learn learns the channel.** On the MIDI page it takes both the CC number and the channel of the incoming message: turn a knob on a drum machine track, and the parameter sends exactly there.
- **Freeze Roll.** While FREEZE is held with PLAY, turning the encoder repeats the looped step from 1/4 to 1/128. The window shows a tachometer bar whose length is the rate, the envelope of every hit, a scale and a hit lamp. 1/64 and 1/128 are timed exactly even though they fall between MIDI clock ticks; rates the MIDI line can't carry at the current tempo are dimmed. Holding the encoder during a roll is a **kill**: the hits go silent while the grid keeps running, and come back exactly on the beat on release.

  ![Freeze Roll kill](docs/screens/roll_kill.png)

  ![Freeze Roll at 1/16](docs/screens/roll_16.png)

- **Swing.** TAP + turn on the SCALE column of the sequencer setup: 50–75 %, every second step is pushed later, up to half a step. Timed precisely rather than to clock ticks; GATE follows the swung intervals.

  ![SEQUENCER setup with swing](docs/screens/seq_setup_swing.png)

- **TWST** — a fifth weather engine driven by the MIDI input. Every incoming note plays immediately and scatters into copies (DEBR) across a window (SCAT) and a pitch set (RADS); each impact knocks one of P1–P4 away from its base value, which then glides back (GUST). Works with the sequencer stopped.

  ![TWST subpage](docs/screens/storm_twst.png)

### Changed

- The CC page is now called **MIDI**.
- CC numbers are set on **MAIN with TAP + turn**; while TAP is held, the columns show `CC74` instead of values. The ×12 coarse step with TAP is gone from MAIN; it stays on STORM and SEQUENCER.

  ![MAIN with TAP held](docs/screens/main_tap_cc.png)

- The global **MIDI CH** setting is gone; **LEN** takes its column on the MIDI subpage.
- **Sequencer step cursor**: hold the encoder and turn (right — forward). The old gesture, holding the encoder and pressing TAP, is removed — it let the 3-second page reset fire.
- Turning the encoder while it's held cancels the 3-second reset, on every page.
- While PLAY is held, the encoder never edits parameters.
- SUN, RAIN and SNOW send a parameter's CC only when its value changes (FOG already did). With AMT at 0 the weather stays silent and no longer overwrites other CCs every WAV step.
- Randomizing a sequencer step puts its note within an octave of the current note, instead of anywhere in 0–127. Randomizing the sequencer setup no longer touches the tempo.
- MAIN subpage (min/max): the value being edited is underlined — min, or max while TAP is held.

  ![MAIN subpage with TAP held](docs/screens/main_clamp_max.png)

- The screen refreshes at 40 frames per second while the Freeze Roll window is open.
- The sailboat moves to the right of a long page header instead of covering it.

### Fixed

- SUN arpeggio: a new echo series could leave the previous arpeggio note hanging.
- MAIN subpage: the edit marker was drawn in the fill color of the selected column — invisible, and it erased parts of the digits.
- Long headers (`NOTE LEN`, `SEQ DEST`, `RFLCT`, `ROTATE`, `DEBRIS`) were partly covered by the sailboat.

### Timing and fixes since 1.0 — PR #3 by [@zemuro](https://github.com/zemuro) (Ruslan Mazavin)

Already in `main`.

- **Timing.** Clock ticks, MIDI input and output run in a dedicated task, woken every 0.5 ms and exactly at the next internal tick. Tick lateness dropped from up to 23 ms to under 0.6 ms, and the internal tempo no longer sags over time (it used to run 25% slow at 120 BPM after a few minutes).
- **Display.** The frame is sent over I²C in its own task, so drawing no longer holds up the clock and MIDI.
- **Animations** use a fast sine for large arguments — frames no longer get slower after a few minutes of running.
- **Clock out** at the internal tempo: Start on a TAP double-click, Clock on every tick, Stop on stop. Nothing is sent out when following an external clock (there is no MIDI thru).
- **Sequencer start.** Step 1 plays first after Start. An external clock without Start no longer doubles the tempo when the sequencer is started with a TAP double-click; under an external clock the sequencer starts on the next beat.
- **Note Off.** Version 1.0 never sent Note Off. Now a note sounds until the next one; Stop sends Note Off and All Notes Off (CC 123); entering BYPASS releases sounding notes. No Note Off is sent after BYPASS for notes that never left the device.
- **Settings.** A tempo above 255 BPM is stored correctly (it used to wrap around to 0, 1, 2…). All four CC numbers are saved — before, only the one under the cursor was, so numbers changed by randomize, reset or MIDI Learn could be lost on power-off. Settings are written 2 s after the last change, right after a clock tick, so the write doesn't delay ticks.
- **FOG**: no random CC right before the LFO value; the LFO sends a CC only when the value changes.
- **Start** moves RAIN drops, SUN echoes and SNOW freezes to the new zero of the tick counter — RAIN used to stay silent after Stop → Start for as long as the previous run had lasted.
- **MIDI Learn**: any button cancels the wait (TAP and PLAY didn't).
- **Encoder**: no lost clicks on fast turns.
- **FREEZE**: the animation resumes from where it paused.

### Documentation

- Russian README and manual, documentation license `LICENSE-DOCS` (CC BY 4.0).
- Screenshots rendered from the firmware's own drawing code; `tools/screens/render.sh` rebuilds them.
- Build instructions: the firmware is the `REDSEA.ino` sketch, built with the Arduino IDE or `arduino-cli`.

### Updating from 1.0

Flash the new firmware — settings are kept. On first boot the old global MIDI channel is copied to all four parameters, Seq Trig is on for all of them and LEN is `LEG`, so the device behaves as before until you change the routing. TWST parameters start at 0.
