# RED SEA — Manual

A complete reference for every page, parameter, and button gesture.

This firmware was developed for the **ESPidi** hardware platform, designed by **Eugene Carlo**. The pinout and control layout below match that board, but the firmware doesn't require it specifically — anyone can build a compatible device by wiring an ESP32-C3, an SSD1306 128×32 OLED, a rotary encoder, and three buttons to the pins listed in [§1](#1-controls) and the project [README](README.md).

## Contents

1. [Controls](#1-controls)
2. [Pages overview](#2-pages-overview)
3. [MAIN page](#3-main-page)
4. [CC page](#4-cc-page)
5. [STORM page](#5-storm-page)
6. [SEQUENCER page](#6-sequencer-page)
7. [The four weather engines](#7-the-four-weather-engines)
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
| **Encoder** | Turn to edit the selected value; hold TAP while turning for a coarser step |

Holding **TAP** while pressing another button changes what that button does almost everywhere in the UI — it's the universal "shift" key. Where relevant, that's called out below as *TAP+X*.

### PLAY

- **Press** (TAP not held): if the performance mode is off, engages it immediately — no waiting for release. If it's already on, this press turns it off immediately.
- **Hold past 0.75s, then release**: an alternate way out of the mode — press-and-hold-and-release always turns it off, regardless of how it was entered.
- **PLAY+TAP** (hold TAP, release PLAY): randomizes the current page/parameter in context (see each page below) — a quick, page-scoped "surprise me."
- **TAP+PLAY held together for 3s** (on the SEQUENCER page, step view only): randomizes every step of the sequencer — trigs and their activations. The screen border blinks starting at 1s to warn you.

### TAP

- **Double-click**: start/stop the sequencer.
- **Hold + press ENC_SW**: advances the sequencer step cursor (SEQUENCER page, step view).
- **Hold while turning the encoder**: coarser step size on numeric edits.
- **Hold while pressing PAGE**: cycles the *subpage* instead of the page; held for 3s together with PAGE, randomizes all sequencer steps (same as TAP+PLAY above).

### PAGE

- **Press**: next page (MAIN → CC → STORM → SEQUENCER → MAIN →).
- **TAP+PAGE**: subpage of the current page.
- Also cancels a pending MIDI Learn, if one is active — a safety net so you're never stuck waiting.

### ENC_SW (encoder push button)

Click count matters, and it's judged independently for double- and triple-clicks — a slow triple-click just looks like a single click followed by a double-click, not a mis-fire.

- **Single click, TAP held**: cycles which of the 4 columns is selected.
- **Single click, TAP not held**: arms a 3-second hold. Border blinks from 1s; release before 3s and nothing happens. Hold to 3s and the current page/subpage resets to its defaults.
- **Double-click**: on MAIN or STORM (main subpage), toggles **manual freeze** on the selected parameter. On the SEQUENCER step view, toggles that step's active flag for the selected column.
- **Triple-click**: on the CC page (CC-number view), or on the SEQUENCER setup subpage with the CC column selected — arms **MIDI Learn** (see [§9](#9-midi-learn)).
- **Any click while MIDI Learn is waiting**: cancels it.

---

## 2. Pages overview

Press **PAGE** to cycle: **MAIN → CC → STORM → SEQUENCER**. Each page (except SEQUENCER) shows a page counter (`n/4`) and a running-state indicator (`>` running / `|` stopped) in the top-right corner. Every page has their own subpage, reached with **TAP+PAGE**.

---

## 3. MAIN page

The four live CC values, P1–P4.

- **Page** — a meter per parameter, showing its current value. Turn the encoder to change the selected one directly; **[hold TAP]** for a ×12 step.
- **Subpage** — edit the **min/max range** that parameter is allowed to wander within. Turning the encoder while **[TAP]** is held edits **max**; released, it edits **min**.

Double-click **[ENC_SW]** to manually freeze the selected parameter (frost animation appears on that column, and it stops responding to any modulation engine until unfrozen).

---

## 4. CC page

Assigns which MIDI CC number each of P1–P4 actually sends.

- **Page** — CC number per parameter (0–127). Triple-click **[ENC_SW]** to MIDI Learn the selected one instead of dialing it by hand.
- **Subpage** — global settings:
  - **CH** — MIDI channel (1–16)
  - **BYP** — which performance mode **[PLAY]** engages: `FRZ` (FREEZE) or `BYP` (BYPASS)
  - **DST** — which target the sequencer's third column (WAV/AMT/WTH) drives
  - **GFX** — background weather animation on/off (pure cosmetics, doesn't touch the engine)

---

## 5. STORM page

The heart of the algorithmic engine.

- **Page**:
  - **AMT** — overall intensity/chaos (0–127). Scales how hard every weather engine mutates values.
  - **WAV** — base timing interval the engines mutate on, from `1/16` to a whole bar.
  - **WTH** — which weather engine is active: **FOG / SUN / RAIN / SNOW**.
  - **ARM** — master on/off for the algorithmic randomizer. Turn it off to freeze the engine entirely while still editing everything by hand.
- **Subpage** — the four parameters specific to whichever weather engine is currently selected. See [§7](#7-the-four-weather-engines).

---

## 6. SEQUENCER page

A 16-step sequencer that can drive a note, a CC, one of WAV/AMT/WTH, and a per-step retrigger rate.

- **Page** (step view) — the 16-step row along the top; the four columns below (NOTE / CC / *destination* / RTRG) edit the step under the cursor. Move the cursor with **[TAP+ENC_SW]**. Select a column with **[ENC_SW (TAP held)]**; edit it with the encoder. Double-click **[ENC_SW]** to toggle that column active on the current step.
  - **RTRG** — once a step is active, this makes it re-trigger repeatedly at a fixed subdivision (1/4 down to 1/64) for as long as the step stays active, instead of firing once.
- **Subpage** (setup):
  - **BPM** — internal tempo, used when no external MIDI clock is present. Shows the detected tempo instead (read-only) whenever an external clock is running.
  - **STEPS** — pattern length, 1–16.
  - **CC** — which CC number the sequencer's CC column sends. Triple-click **[ENC_SW]** here to MIDI Learn it.
  - **SCALE** — playback speed multiplier (1, 1/2, 1/4, 1/8).

**Randomizing:** **[PLAY+TAP]** randomizes just the step under the cursor. Holding **[TAP+PLAY]** together for 3 seconds randomizes every step in the pattern — trigs and activations both — with the screen border blinking as a warning before it fires.

---

## 7. The four weather engines

Each engine mutates P1–P4 on the `WAV` timing grid, scaled by `AMT`. Their STORM subpage parameters give each one a distinct personality.

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

If **ARP** isn't OFF *and* a sequencer note happened to fire on the exact same clock tick that the echo series was armed, every echo in that cascade also re-triggers that note, transposed further each time: `Same` = no transposition, `5th`/`Oct`/`2Oct` = 7/12/24 semitones **per echo**, cumulative — three echoes at `Oct` land the third one three octaves up.

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

---

## 8. Performance modes — BYPASS & FREEZE

One button (**PLAY**) engages whichever mode is currently selected on the CC page (`BYP` = BYPASS, `FRZ` = FREEZE). It's designed for live use: press to slam it on instantly

### BYPASS

Mutes **all** outgoing MIDI — nothing leaves the device. The algorithmic engine, sequencer, and everything else keep running exactly as normal underneath; only the output is muted. A "BYPASS" banner appears in normal (non-inverted) colors, since nothing is actually paused.

### FREEZE

Freezes **all eight** targets at once (P1–P4, AMT, WAV, WTH, ARM) — inverting whatever was already frozen, so anything you'd frozen by hand stays consistent when you exit. While active, the sequencer stops advancing and instead **loops the step under the cursor**, at the tempo set by `SCALE`. The whole screen inverts and shows a "FROZEN" banner (except on the SEQUENCER page, where the normal header stays visible so you can watch the loop).

### Entering & exiting

- **Press PLAY**: if off, turns on instantly. If already on, turns off instantly — a second press is the fastest way out.
- **Hold past 0.75s and release**: also turns it off — useful for a classic "hold to engage, let go to release" performance gesture in one motion.
- A quick tap that turns it on leaves it **latched** on until you deliberately turn it off by one of the two methods above.

---

## 9. MIDI Learn

Instead of dialing in a CC number by hand, grab it straight from your controller.

**Triple-click ENC_SW** on:
- the CC page (CC-number view) — learns the number for the parameter under the cursor, or
- the SEQUENCER setup subpage, with the CC column selected — learns the sequencer's CC number.

A banner reading *"Waiting for midi data"* appears. Move a control on whatever's connected to the MIDI input — the next Control Change message received (on any channel) is captured, assigned, and the banner disappears. Any button press cancels the wait without assigning anything, and leaving the page does the same automatically.

---

## 10. Freezing parameters

There are two independent freeze mechanisms:

- **Manual freeze** — double-click **[ENC_SW]** on MAIN or STORM (main subpage) to freeze/unfreeze the selected parameter by hand. Shows a frost animation on that column.
- **Algorithmic freeze** — SNOW's `FRZ` parameter, and the global FREEZE performance mode, both flip the same underlying freeze flags. A frozen target is protected from *everything*: the weather engine, the sequencer's WAV/AMT/WTH destination, retriggers — all of it skips a frozen target until it's unfrozen again.

Any of P1–P4, AMT, WAV, WTH, or ARM can be frozen independently.

---

## 11. Settings & persistence

Everything — CC assignments, min/max ranges, weather engine parameters, sequencer pattern, global settings — is written to flash a couple of seconds after you stop changing it, and reloaded on boot. Power off mid-edit and nothing is lost.

---

## 12. Quick reference

| Gesture                       | Effect                                                      |
| ----------------------------- | ----------------------------------------------------------- |
| PAGE                          | Next page                                                   |
| TAP+PAGE                      | Subpage of current Page                                     |
| TAP double-click              | Start/stop sequencer                                        |
| TAP+ENC_SW                    | Move sequencer step cursor                                  |
| ENC_SW (TAP held)             | Cycle selected column                                       |
| ENC_SW (TAP free), hold 3s    | Reset current page/subpage to defaults                      |
| ENC_SW double-click           | Manual freeze (MAIN/STORM) / toggle step column (SEQUENCER) |
| ENC_SW triple-click           | MIDI Learn (CC page / sequencer CC)                         |
| PLAY press                    | Engage/exit BYPASS or FREEZE instantly                      |
| PLAY hold 0.75s + release     | Exit BYPASS/FREEZE                                          |
| PLAY+TAP                      | Randomize current page/parameter                            |
| TAP+PLAY or TAP+PAGE, hold 3s | Randomize all sequencer steps                               |
| Encoder turn (TAP held)       | Coarse step                                                 |
