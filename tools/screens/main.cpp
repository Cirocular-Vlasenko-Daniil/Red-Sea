// Экраны RED SEA для документации — из настоящего кода отрисовки прошивки.
// REDSEA.ino собирается на компьютере с библиотекой Adafruit_GFX и заглушками вместо
// железа (stubs/), каждая сцена выставляет состояние и вызывает updateDisplay(),
// буфер экрана сохраняется в PBM. Запуск: tools/screens/render.sh
#include <Arduino.h>
#include <Wire.h>
uint32_t g_ms = 100000, g_us = 100000000u, g_rnd = 12345;
int g_pins[64];
TwoWire Wire;
#include "../../REDSEA/REDSEA.ino"

static uint8_t baseState[sizeof(State)];
static void setTime(uint32_t ms) { g_ms = ms; g_us = ms * 1000u; }
static const char* g_outDir = ".";
static void save(const char* name) {
  char path[512]; snprintf(path, sizeof path, "%s/%s.pbm", g_outDir, name);
  FILE* f = fopen(path, "w");
  fprintf(f, "P1\n128 32\n");
  for (int y = 0; y < 32; y++) { for (int x = 0; x < 128; x++) fprintf(f, "%d", display.getPixel(x, y) ? 1 : 0); fprintf(f, "\n"); }
  fclose(f);
}
static void shot(const char* name) {
  state.displayDirty = true; state.bypassTransition = false;
  updateDisplay(); save(name);
}
static void reset(uint32_t ms) {
  memcpy((void*)&state, baseState, sizeof(State));
  buttons[1].lastStable = HIGH; buttons[1].lastChange = 0;
  setTime(ms); g_rnd = 12345;
}
static void step(uint8_t i, bool n, bool c, bool d, bool r, uint8_t note, uint8_t cc, uint8_t w, uint8_t rt) {
  SequencerStep& s = state.steps[i];
  s.active[0] = n; s.active[1] = c; s.active[2] = d; s.active[3] = r;
  s.note = note; s.cc = cc; s.wavesIndex = w; s.retrigIndex = rt;
}
static void freeze() {
  state.bypassMode = BypassMode::FREEZE;
  state.animationPaused = true; state.frozenAnimTime = 61000;
}
static void rollScene(uint8_t idx, uint32_t ms, uint32_t tickInBeat = 5) {
  freeze();
  state.sequencerRunning = true; state.sequencerStartPending = false;
  state.seqOriginTick = 0; state.midiTicks = 24 * 37 + tickInBeat;
  state.rollActive = true; state.rollIndex = idx;
  setTime(ms); state.lastTickMicros = g_us - 9000;
  // два прохода: первый задаёт «предыдущий кадр» для лампы (25 мс назад)
  setTime(ms - 25); state.displayDirty = true; updateDisplay();
  setTime(ms);
}

int main(int argc, char** argv) {
  if (argc > 1) g_outDir = argv[1];
  for (int i = 0; i < 64; i++) g_pins[i] = HIGH;
  loadSettings();
  // Общая сцена
  uint8_t vals[4] = {96, 40, 70, 22}, ccs[4] = {74, 71, 10, 1};
  for (int i = 0; i < 4; i++) { state.params[i].value = state.params[i].baseValue = vals[i]; state.params[i].cc = ccs[i]; }
  state.params[1].min = 20; state.params[1].max = 100;
  state.params[0].ch = 1;  state.params[0].seqTrig = true;
  state.params[1].ch = 2;  state.params[1].seqTrig = false;
  state.params[2].ch = 10; state.params[2].seqTrig = true;
  state.params[3].ch = 0;  state.params[3].seqTrig = false;
  state.chaos = 45; state.waveIntervalIndex = 2; state.weatherMode = WeatherMode::SUN; state.randomizerEnabled = true;
  state.sequencerBPM = 120; state.sequencerSteps = 16; state.sequencerCC = 74; state.sequencerScaleIndex = 0;
  for (int i = 0; i < 16; i++) step(i, false, false, false, false, 60, 64, 2, 0);
  step(0, 1, 1, 0, 0, 48, 100, 2, 0); step(2, 0, 1, 0, 0, 48, 40, 2, 0); step(4, 1, 0, 1, 0, 55, 64, 1, 0);
  step(6, 1, 1, 0, 1, 60, 90, 2, 2); step(8, 1, 0, 0, 0, 48, 64, 2, 0); step(10, 0, 1, 1, 0, 52, 20, 3, 0);
  step(12, 1, 1, 0, 0, 63, 110, 2, 0); step(14, 1, 0, 0, 1, 67, 64, 2, 3);
  state.sequencerRunning = true; state.sequencerPlayhead = 6; state.sequencerDisplayStep = 6; state.sequencerCursor = 4;
  state.twstDebr = 5; state.twstRads = 2; state.twstScat = 1; state.twstGust = 80;
  state.lfoType = 0; state.lfoShape = 40; state.lfoPhase = 0; state.lfoGlide = 20;
  state.sunRflct = 3; state.sunArp = 3; state.sunDflct = 30; state.sunBias = 10;
  state.rainDrip = 64; state.rainWet = 100; state.rainSplsh = 40; state.rainThunder = true;
  state.snowFlake = 7; state.snowRotation = 2; state.snowFrz = 50; state.snowTime = 0;
  memcpy(baseState, (const void*)&state, sizeof(State));

  // MAIN
  reset(100000); state.currentPage = Page::MAIN; state.selectedParam = 0; shot("main");
  reset(100000); state.currentPage = Page::MAIN; state.selectedParam = 0;
  buttons[1].lastStable = LOW; buttons[1].lastChange = 0; shot("main_tap_cc");
  reset(100000); state.currentPage = Page::MAIN; state.subPageMain = 1; state.selectedParam = 1; shot("main_clamp");
  reset(100000); state.currentPage = Page::MAIN; state.subPageMain = 1; state.selectedParam = 1;
  buttons[1].lastStable = LOW; shot("main_clamp_max");
  // MIDI
  reset(100000); state.currentPage = Page::CC; state.selectedParam = 2; shot("midi_channels");
  reset(100000); state.currentPage = Page::CC; state.subPageCC = 1; state.gateMode = true; state.selectedParam = 0; shot("midi_global");
  reset(100000); state.currentPage = Page::CC; state.selectedParam = 0; state.midiLearnActive = true; shot("midi_learn");
  // STORM
  reset(100000); state.currentPage = Page::STORM; state.weatherMode = WeatherMode::TWST; state.selectedParam = 2;
  state.twstHitTime = g_ms - 120; shot("storm");
  const char* wn[5] = {"storm_fog", "storm_sun", "storm_rain", "storm_snow", "storm_twst"};
  for (int w = 0; w < 5; w++) {
    reset(100000); state.currentPage = Page::STORM; state.subPageStorm = 1; state.weatherMode = (WeatherMode)w;
    state.selectedParam = 0; if (w == 4) state.twstHitTime = g_ms - 120;
    shot(wn[w]);
  }
  // SEQUENCER
  reset(100000); state.currentPage = Page::SEQUENCER; state.selectedParam = 0; shot("seq_steps");
  reset(100000); state.currentPage = Page::SEQUENCER; state.subPageSequencer = 1; state.selectedParam = 0; state.sequencerSwing = 62; shot("seq_setup");
  reset(100000); state.currentPage = Page::SEQUENCER; state.subPageSequencer = 1; state.selectedParam = 3; state.sequencerSwing = 62;
  buttons[1].lastStable = LOW; shot("seq_setup_swing");
  // FREEZE и Freeze Roll
  reset(100000); state.currentPage = Page::MAIN; freeze(); shot("frozen");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 6; rollScene(2, 100010, 6); shot("roll_16");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 6; state.gateMode = true; rollScene(4, 100013); shot("roll_64_gate");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 6; rollScene(5, 100017); shot("roll_128");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 6; state.sequencerBPM = 300;
  for (int i = 0; i < 4; i++) { state.params[i].ch = i + 1; state.params[i].seqTrig = true; }
  rollScene(5, 100011); shot("roll_limit");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 1; rollScene(2, 100010); shot("roll_empty");
  reset(100000); state.currentPage = Page::MAIN; state.sequencerCursor = 6; rollScene(3, 100010, 6);
  state.rollKill = true; state.rollKillMask = 0x3; shot("roll_kill");
  puts("done");
  return 0;
}
