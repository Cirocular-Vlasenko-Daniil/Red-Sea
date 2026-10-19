# RED SEA — Manual

A complete reference for every page, parameter, and button gesture.

This firmware was developed for the **ESPidi** hardware platform, designed by **Eugene Carlo**. The pinout and control layout below match that board, but the firmware doesn't require it specifically — anyone can build a compatible device by wiring an ESP32-C3, an SSD1306 128×32 OLED, a rotary encoder, and three buttons to the pins listed in [§1](#1-controls) and the project [README](README.md).

## Contents

1. [Controls](#1-controls)
2. [Pages overview](#2-pages-overview)
3. [MAIN page](#3-main-page)
4. [MIDI page](#4-midi-page)
5. [STORM page](#5-storm-page)
6. [SEQUENCER page](#6-sequencer-page)
7. [The five weather engines](#7-the-five-weather-engines)
8. [Performance modes — BYPASS & FREEZE](#8-performance-modes--bypass--freeze)
9. [MIDI Learn](#9-midi-learn)
10. [Freezing parameters](#10-freezing-parameters)
11. [Settings & persistence](#11-settings--persistence)
12. [Quick reference](#12-quick-reference)

---

## 1. Controls

RED SEA has four buttons and one push-encoder.

| Control | What it is |
|---|---|
| **PLAY** | Engages/exits the performance mode (BYPASS or FREEZE) |
| **TAP** | Sequencer transport + "hold" modifier for other buttons |
| **PAGE** | Cycles pages (or subpages, held with TAP) |
| **ENC_SW** | Encoder push button — select / toggle / freeze / MIDI Learn, depending on click count |
| **Encoder** | Turn to edit the selected value. With TAP held: a coarser step, or the column's second value on MAIN and CC. With PLAY held in FREEZE: Freeze Roll |

Holding **TAP** while pressing another button changes what that button does almost everywhere in the UI — it's the universal "shift" key. Where relevant, that's called out below as *TAP+X*.

### PLAY

- **Press** (TAP not held): if the performance mode is off, engages it immediately — no waiting for release. If it's already on, this press turns it off immediately.
- **Hold past 0.75s, then release**: an alternate way out of the mode — press-and-hold-and-release always turns it off, regardless of how it was entered.
- **Hold and turn the encoder** (FREEZE): **Freeze Roll** — see [§8](#8-performance-modes--bypass--freeze). While PLAY is held, the encoder never edits parameters.
- **PLAY+TAP** (hold TAP, release PLAY): randomizes the current page/parameter in context (see each page below) — a quick, page-scoped "surprise me."
- **TAP+PLAY held together for 3s** (on the SEQUENCER page, step view only): randomizes every step of the sequencer — trigs and their activations. The screen border blinks starting at 1s to warn you.

### TAP

- **Double-click**: start/stop the sequencer.
- **Hold while turning the encoder**: coarser step size on STORM and SEQUENCER. On MAIN it edits the parameter's CC number, on the MAIN subpage — **max**, on MIDI — **Seq Trig**.
- **Hold while pressing PAGE**: cycles the *subpage* instead of the page; held for 3s together with PAGE, randomizes all sequencer steps (same as TAP+PLAY above).

### PAGE

- **Press**: next page (MAIN → MIDI → STORM → SEQUENCER → MAIN →).
- **TAP+PAGE**: subpage of the current page.
- Also cancels a pending MIDI Learn, if one is active — a safety net so you're never stuck waiting.

### ENC_SW (encoder push button)

Click count matters, and it's judged independently for double- and triple-clicks — a slow triple-click just looks like a single click followed by a double-click, not a mis-fire.

- **Single click, TAP held**: cycles which of the 4 columns is selected.
- **Single click, TAP not held**: arms a 3-second hold. Border blinks from 1s; release before 3s and nothing happens. Hold to 3s and the current page/subpage resets to its defaults. Turning the encoder while it's held cancels the reset.
- **Hold and turn**: on the SEQUENCER step view, moves the step cursor (right — forward, wrapping around). Never triggers the reset.
- **Double-click**: on MAIN or STORM (main subpage), toggles **manual freeze** on the selected parameter. On the SEQUENCER step view, toggles that step's active flag for the selected column.
- **Triple-click**: on the MIDI page (channel view), or on the SEQUENCER setup subpage with the CC column selected — arms **MIDI Learn** (see [§9](#9-midi-learn)).
- **Any click while MIDI Learn is waiting**: cancels it.

---

## 2. Pages overview

Press **PAGE** to cycle: **MAIN → MIDI → STORM → SEQUENCER**. Each page (except SEQUENCER) shows a page counter (`n/4`) and a running-state indicator (`>` running / `|` stopped) in the top-right corner. Every page has their own subpage, reached with **TAP+PAGE**.

---

## 3. MAIN page

The four live CC values, P1–P4.

- **Page** — a meter per parameter, showing its current value. Turn the encoder to change the selected one directly. **[Hold TAP]** and turn to set the parameter's **CC number** (0–127) instead — while TAP is held, the columns show CC numbers (`CC74`) in place of values.
- **Subpage** — edit the **min/max range** that parameter is allowed to wander within. Turning the encoder while **[TAP]** is held edits **max**; released, it edits **min**.

Double-click **[ENC_SW]** to manually freeze the selected parameter (frost animation appears on that column, and it stops responding to any modulation engine until unfrozen).

---

## 4. MIDI page

Routes P1–P4: which MIDI channel each one sends on, and where notes go.

- **Page** — MIDI channel per parameter: `NO` or 1–16. The parameter's CC goes out on that channel; `NO` sends nothing at all. **[TAP]**+turn toggles **Seq Trig** for the selected column, shown as a note icon in its corner: notes are sent to every channel with Seq Trig on. Triple-click **[ENC_SW]** to MIDI Learn the selected parameter — its CC number and its channel at once.
- **Subpage** — global settings:
  - **LEN** — note length: `LEG` (a note sounds until the next one) or `GATE` (a note ends halfway to the next hit)
  - **BYP** — which performance mode **[PLAY]** engages: `FRZ` (FREEZE) or `BYP` (BYPASS)
  - **DST** — which target the sequencer's third column (WAV/AMT/WTH) drives
  - **GFX** — background weather animation on/off (pure cosmetics, doesn't touch the engine)

Where everything goes:

| Source | Channels |
|---|---|
| P1–P4 CC | each parameter's own channel |
| Sequencer notes, sequencer CC column | every channel with Seq Trig on (repeated channels count once) |
| SUN arpeggio notes (ARP) | the channel of the echoing parameter, if it has Seq Trig on |
| TWST notes | every channel with Seq Trig on |

A parameter set to `NO` sends nothing, even with Seq Trig on. If no parameter has Seq Trig on, no notes are sent.

With **LEN** set to `GATE`, the "next hit" is whatever comes next for that note: the next step, the step's retrigger, the next Freeze Roll hit, the next TWST grid position, or the next SUN echo.

---

## 5. STORM page

The heart of the algorithmic engine.

- **Page**:
  - **AMT** — overall intensity/chaos (0–127). Scales how hard every weather engine mutates values.
  - **WAV** — base timing interval the engines mutate on, from `1/16` to a whole bar.
  - **WTH** — which weather engine is active: **FOG / SUN / RAIN / SNOW / TWST**.
  - **ARM** — master on/off for the algorithmic randomizer. Turn it off to freeze the engine entirely while still editing everything by hand.
- **Subpage** — the four parameters specific to whichever weather engine is currently selected. See [§7](#7-the-five-weather-engines).

---

## 6. SEQUENCER page

A 16-step sequencer that can drive a note, a CC, one of WAV/AMT/WTH, and a per-step retrigger rate. Notes and the CC column go to the Seq Trig channels ([§4](#4-midi-page)); note length follows **LEN**.

- **Page** (step view) — the 16-step row along the top; the four columns below (NOTE / CC / *destination* / RTRG) edit the step under the cursor. Move the cursor by holding **[ENC_SW]** and turning the encoder. Select a column with **[ENC_SW (TAP held)]**; edit it with the encoder. Double-click **[ENC_SW]** to toggle that column active on the current step.
  - **RTRG** — once a step is active, this makes it re-trigger repeatedly at a fixed subdivision (1/4 down to 1/64) for as long as the step stays active, instead of firing once.
- **Subpage** (setup):
  - **BPM** — internal tempo, used when no external MIDI clock is present. Shows the detected tempo instead (read-only) whenever an external clock is running.
  - **STEPS** — pattern length, 1–16.
  - **CC** — which CC number the sequencer's CC column sends. Triple-click **[ENC_SW]** here to MIDI Learn it.
  - **SCALE** — playback speed multiplier (1, 1/2, 1/4, 1/8).

**Randomizing:** **[PLAY+TAP]** randomizes just the step under the cursor. Holding **[TAP+PLAY]** together for 3 seconds randomizes every step in the pattern — trigs and activations both — with the screen border blinking as a warning before it fires.

---

## 7. The five weather engines

Each engine mutates P1–P4 on the `WAV` timing grid, scaled by `AMT`. Their STORM subpage parameters give each one a distinct personality. TWST is the exception: it reacts to incoming notes instead of the clock grid.

### FOG — continuous LFO

| Param | Range                    | What it does                  |
| ----- | ------------------------ | ----------------------------- |
| TYPE  | Sine / Triangle / Square | Base waveform                 |
| SHAPE | −128…127                 | Morphs the wave (see below)   |
| PHASE | −128…127                 | Phase offset                  |
| GLIDE | 0…127                    | Smoothing on the LFO's motion |

**SHAPE** doesn't just distort — it re-characterizes the wave:
- On **Sine**, positive SHAPE blends in 2nd/3rd/4th harmonics, making the wave progressively richer; negative SHAPE hard-clips it and blends in noise, turning it into distortion/static.
- On **Triangle**, SHAPE smoothly skews the peak toward one edge until, at the extreme, it's a clean rising or falling sawtooth.
- On **Square**, SHAPE adjust Pulse width.

**GLIDE** at 0 means the LFO tracks its target instantly (no lag); higher values add smoothing lag, up to a slow, permanently-in-motion glide — it never fully stalls, even at maximum.

### SUN — reflections & echo

| Param | Range | What it does |
|---|---|---|
| RFLCT | 0–4 | Number of echo reflections |
| ARP | OFF / Same / 5th / Oct / 2Oct | Note arpeggio riding on the echoes |
| DFLCT | 0–127 | Extra chance to trigger an echo series without hitting the clamp |
| BIAS | −63…64 | Shifts the random walk up/down |

When a parameter's random step would push it past its own min/max, it triggers a series of **echoes** instead of just clamping: up to `RFLCT` repeats, each one landing twice as fast as the last (half the previous interval), shown on-screen as a little stack of receding rectangles.

If **ARP** isn't OFF *and* a sequencer note happened to fire on the exact same clock tick that the echo series was armed, every echo in that cascade also re-triggers that note, transposed further each time: `Same` = no transposition, `5th`/`Oct`/`2Oct` = 7/12/24 semitones **per echo**, cumulative — three echoes at `Oct` land the third one three octaves up. The arpeggio plays on the channel of the echoing parameter, and only if that parameter has Seq Trig on.

**DFLCT** adds an independent, unconditional chance (per mutation) to arm the same echo series even when the value didn't actually hit its clamp.

### RAIN — drift, splash, storm

| Param | Range | What it does |
|---|---|---|
| DRIP | 0–127 | Per-parameter clock desync |
| WET | 0–127 | Pull-back-to-home strength |
| SPLSH | 0–127 | Chance to splash a neighboring parameter |
| THNDR | ON/OFF | Random lightning strikes |

Every RAIN mutation drifts, then gets pulled back toward the parameter's last manually-set value — **WET** is how strong that pull is: 0 is a free random walk (like FOG), 127 barely lets it move at all.

**DRIP** desynchronizes the four parameters from each other and from the clock grid: at 0 they all mutate in lockstep on the `WAV` interval; higher values let each one wander independently off-grid, like drops falling at slightly different rates.

**SPLSH** is a chance, whenever a drop lands past its own clamp, that it also nudges one of its immediate neighbors by a small random amount.

**THNDR**, once on, gives every clock tick a small (AMT-scaled) chance of a lightning strike: a random unfrozen parameter is instantly slammed to its minimum or maximum, with a flash and a jagged bolt across the screen. It isn't reverted on a timer — the ordinary WET pull just drags it back down over the following ticks, so the storm "passes" naturally.

### SNOW — Euclidean rhythm freezer

| Param | Range | What it does |
|---|---|---|
| FLAKE | 3–12 | Steps in the Euclidean pattern |
| ROTATE | 0–11 | Rotates the pattern |
| FRZ | 0–127 | Chance to freeze/unfreeze on each hit |
| TIME | −63…64 | Shapes freeze duration |

SNOW replaces the flat timing grid with a Euclidean rhythm: `FLAKE` steps per cycle, distributed as evenly as possible, at the `WAV` interval per step (shown as a snowflake with one ray per step). **ROTATE** shifts which steps in that cycle land the hits.

On every hit, P1–P4 mutate as usual (scaled by AMT) — and independently, **FRZ** rolls a chance (0–127) for *each* of the eight freezable targets (P1–P4, AMT, WAV, WTH, ARM) to flip: unfrozen → frozen, or frozen → unfrozen if it already was. A short flash on the WEATHER column marks each hit.

**TIME** shapes how long a SNOW-triggered freeze lasts: `0` = exactly one pattern step's length; positive values grow that up toward the full pattern's length; negative values randomize the duration instead — the more negative, the wider the spread between targets.

### TWST — debris from incoming notes

| Param | Range | What it does |
|---|---|---|
| DEBR | 0–8 | Copies thrown off by each incoming note |
| RADS | 0 / OCT / 5+8 / 2OCT / CHR | Pitches the copies can land on |
| SCAT | 1/8 / 1/4 / 1/2 / 1 / 2 bars | Window the copies fall into |
| GUST | 0–127 | How hard each impact knocks P1–P4 |

TWST listens to the MIDI input — Note On/Off on any channel. Every incoming note plays immediately on the Seq Trig channels ([§4](#4-midi-page)), and `DEBR` copies follow it, each on a random position inside the `SCAT` window (never two on the same position), at a random pitch from `RADS`, with the same velocity. Releasing the key ends the original note; copies already in the air keep falling. The DEBR column shows the impact point with one dot per copy.

**RADS**: `0` = the same note, `OCT` = ±1 octave, `5+8` = ±fifth or ±octave, `2OCT` = ±1 or ±2 octaves, `CHR` = any semitone within ±1 octave. Octaves and fifths fit any key; `CHR` is for chaos.

**SCAT** sets the window and its grid at once: the window is split into 16 positions (8 at `1/8`), never finer than 1/64.

| SCAT | Grid | Feel |
|---|---|---|
| 1/8 | 1/64 | a tight burst right after the hit, almost a ratchet |
| 1/4 | 1/64 | a fast scatter |
| 1/2 | 1/32 | a wider scatter |
| 1 | 1/16 | separate drops across the bar |
| 2 | 1/8 | sparse fragments |

**GUST**: every impact — the original note and each copy — hits the next of P1–P4 in turn (P1 → P2 → P3 → P4 → P1). The hit parameter jumps away from its base value by up to GUST × AMT, up or down within its min/max, and glides back to the base over the `WAV` time. GUST 0 leaves the CCs alone; manually frozen parameters are skipped. P1–P4 don't mutate on the clock grid in TWST — only impacts move them.

Note length follows **LEN**: in `LEG` a copy lasts until the next grid position, in `GATE` half of that.

TWST runs on time rather than on clock ticks, so it works with the sequencer stopped — the tempo then comes from the internal BPM, or from the external clock when one is present. In FREEZE every hit scatters in the same pattern. With ARM off, the original notes still play, but there are no copies and no GUST. At most 16 copies are in the air at once; extra ones are dropped. Leaving TWST or engaging BYPASS stops everything that's sounding or scheduled.

Incoming notes are used only by TWST; the other engines ignore them, and RED SEA has no MIDI thru.

---

## 8. Performance modes — BYPASS & FREEZE

One button (**PLAY**) engages whichever mode is currently selected on the MIDI page (`BYP` = BYPASS, `FRZ` = FREEZE). It's designed for live use: press to slam it on instantly

### BYPASS

Mutes **all** outgoing MIDI — nothing leaves the device. The algorithmic engine, sequencer, and everything else keep running exactly as normal underneath; only the output is muted. A "BYPASS" banner appears in normal (non-inverted) colors, since nothing is actually paused.

### FREEZE

Freezes **all eight** targets at once (P1–P4, AMT, WAV, WTH, ARM) — inverting whatever was already frozen, so anything you'd frozen by hand stays consistent when you exit. While active, the sequencer stops advancing and instead **loops the step under the cursor**, at the tempo set by `SCALE`. The whole screen inverts and shows a "FROZEN" banner (except on the SEQUENCER page, where the normal header stays visible so you can watch the loop).

### Freeze Roll

While FREEZE is on and you're **holding PLAY**, turning the encoder rolls the looped step: it repeats on a grid from 1/4 to 1/128 — right is faster, left is slower. A window over the page shows `Freeze Roll`, a hit lamp, and the current rate (`1/32`). Below them is a tachometer bar: its length is the speed — each step from 1/4 to 1/128 lengthens it by a sixth — and the needle at its end glides to a new rate. Inside the bar run the note envelopes: each hit comes out of the needle as a tooth and travels left over one beat, so the finer the rate, the denser the teeth; in `GATE` there are gaps between them. The scale `4 8 16 32 64 128` under the bar highlights the current rate.

The lamp blinks with each hit as long as a hit lasts longer than two screen frames. Faster than that a real blink would turn into a strobe, so the lamp switches to a dither that changes with every hit, finer as the rate grows: 2×2 blocks, then single-pixel noise, then a checkerboard that inverts every frame. The screen refreshes at 40 frames per second while the window is open.

- The first click starts from what's playing now — the step length (1/16 × SCALE) or the step's retrigger, whichever is faster — and moves it one notch.
- Hits are timed against the sequencer's grid: after a rate change, the next hit lands on the nearest division of the new grid. 1/64 and 1/128 are exact even though they fall between MIDI clock ticks.
- The rate never goes higher than the MIDI line can carry at the current tempo and number of Seq Trig channels; rates out of reach are dimmed on the scale.
- `EMPTY` in the window means nothing is active on the step.
- The roll stops when you release PLAY, after 2 seconds without turning, or when FREEZE turns off. Nothing is saved — the step's retrigger stays as it was.
- Like the FREEZE loop itself, the roll plays only while the sequencer is running.

The roll is a hold gesture: press PLAY (FREEZE engages), keep holding, turn. If FREEZE is already latched, the next PLAY press turns it off, so press once to exit and then press and hold. While PLAY is held, the encoder doesn't edit parameters, and the PLAY+TAP / TAP+PLAY randomize gestures don't fire after a roll.

### Entering & exiting

- **Press PLAY**: if off, turns on instantly. If already on, turns off instantly — a second press is the fastest way out.
- **Hold past 0.75s and release**: also turns it off — useful for a classic "hold to engage, let go to release" performance gesture in one motion.
- A quick tap that turns it on leaves it **latched** on until you deliberately turn it off by one of the two methods above.

---

## 9. MIDI Learn

Instead of dialing in a CC number and channel by hand, grab them straight from your controller — or from the device you want to control: turn a knob on a drum machine track connected to the MIDI input, and the parameter will send exactly there.

**Triple-click ENC_SW** on:
- the MIDI page (channel view) — learns both the CC number and the channel for the parameter under the cursor, or
- the SEQUENCER setup subpage, with the CC column selected — learns the sequencer's CC number (only the number: the sequencer CC goes to the Seq Trig channels).

A banner reading *"Waiting for midi data"* appears. Move a control on whatever's connected to the MIDI input — the next Control Change message received (on any channel) is captured, assigned, and the banner disappears. Any button press cancels the wait without assigning anything, and leaving the page does the same automatically.

---

## 10. Freezing parameters

There are two independent freeze mechanisms:

- **Manual freeze** — double-click **[ENC_SW]** on MAIN or STORM (main subpage) to freeze/unfreeze the selected parameter by hand. Shows a frost animation on that column.
- **Algorithmic freeze** — SNOW's `FRZ` parameter, and the global FREEZE performance mode, both flip the same underlying freeze flags. A frozen target is protected from *everything*: the weather engine, the sequencer's WAV/AMT/WTH destination, retriggers — all of it skips a frozen target until it's unfrozen again.

Any of P1–P4, AMT, WAV, WTH, or ARM can be frozen independently.

---

## 11. Settings & persistence

Everything — CC assignments, channels and Seq Trig, min/max ranges, weather engine parameters, sequencer pattern, global settings — is written to flash a couple of seconds after you stop changing it, and reloaded on boot. Power off mid-edit and nothing is lost.

Updating from the first release: the old global MIDI channel is copied to all four parameters on first boot, Seq Trig is on for all of them and LEN is `LEG`, so the device behaves as before.

---

## 12. Quick reference

| Gesture                       | Effect                                                      |
| ----------------------------- | ----------------------------------------------------------- |
| PAGE                          | Next page                                                   |
| TAP+PAGE                      | Subpage of current Page                                     |
| TAP double-click              | Start/stop sequencer                                        |
| ENC_SW hold + encoder turn    | Move sequencer step cursor (SEQUENCER)                      |
| ENC_SW (TAP held)             | Cycle selected column                                       |
| ENC_SW (TAP free), hold 3s    | Reset current page/subpage to defaults                      |
| ENC_SW double-click           | Manual freeze (MAIN/STORM) / toggle step column (SEQUENCER) |
| ENC_SW triple-click           | MIDI Learn (MIDI page — number + channel / sequencer CC)    |
| PLAY press                    | Engage/exit BYPASS or FREEZE instantly                      |
| PLAY hold 0.75s + release     | Exit BYPASS/FREEZE                                          |
| PLAY hold + encoder turn      | Freeze Roll (in FREEZE)                                     |
| PLAY+TAP                      | Randomize current page/parameter                            |
| TAP+PLAY or TAP+PAGE, hold 3s | Randomize all sequencer steps                               |
| Encoder turn (TAP held)       | Coarse step (STORM, SEQUENCER) / CC number (MAIN) / max (MAIN subpage) / Seq Trig (MIDI) |
