#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ============================================================
// 1. КОНФИГУРАЦИЯ
// ============================================================
namespace Pins {
  constexpr uint8_t PLAY   = 1;
  constexpr uint8_t TAP    = 2;
  constexpr uint8_t PAGE   = 3;
  constexpr uint8_t ENC_SW = 4;
  constexpr uint8_t ENC_A  = 5;
  constexpr uint8_t ENC_B  = 6;
  constexpr uint8_t MIDI_RX = 7;
  constexpr uint8_t MIDI_TX = 10;
  constexpr uint8_t SDA    = 8;
  constexpr uint8_t SCL    = 9;
  constexpr uint32_t MIDI_BAUD = 31250;
}

namespace Display {
  constexpr uint8_t W = 128;
  constexpr uint8_t H = 32;
  constexpr uint8_t COL_W = 31;
  constexpr uint8_t METER_Y = 12;
  constexpr uint8_t METER_H = 20;
}

Adafruit_SSD1306 display(Display::W, Display::H, &Wire, -1);
HardwareSerial midi(1);
Preferences storage;

// ============================================================
// 2. ТИПЫ И КОНСТАНТЫ
// ============================================================
enum class WeatherMode : uint8_t { FOG, SUN, RAIN, SNOW };
enum class Page : uint8_t { MAIN, CC, STORM, SEQUENCER };
enum class BypassMode : uint8_t { OFF, BYPASS, FREEZE };
enum class SeqDest : uint8_t { WAV, AMT, WTH };

constexpr uint8_t NUM_PARAMS = 4;
constexpr uint8_t SEQUENCER_STEPS = 16;

const uint8_t waveIntervals[5] = {6, 12, 24, 48, 96};
const char* waveIntervalNames[5] = {"1/16", "1/8", "1/4", "1/2", "1"};
const uint8_t retrigIntervals[5] = {24, 12, 6, 3, 1};
const char* retrigNames[5] = {"1/4", "1/8", "1/16", "1/32", "1/64"};
const uint8_t scaleMultipliers[4] = {1, 2, 4, 8};
const char* scaleNames[4] = {"1", "1/2", "1/4", "1/8"};

const char* seqDestNames[3] = {"WAV", "AMT", "WTH"};

// Порядок ОТОБРАЖЕНИЯ колонок на шаге секвенсора: NOTE, DST, CC, RTRG
// (визуально DST и CC поменяны местами относительно порядка данных).
// SEQ_COL_ORDER[визуальная позиция] = индекс данных (0=NOTE, 1=CC,
// 2=DST/WAV, 3=RTRG) — этот индекс данных остаётся неизменным везде,
// где завязана логика (triggerSequencerStep, onClockTick, handleEncoder,
// переключение active[] по двойному клику) — меняется только то, в
// какой позиции на экране рисуется каждая колонка.
const uint8_t SEQ_COL_ORDER[4] = {0, 2, 1, 3};

// ============================================================
// 3. СТРУКТУРЫ ДАННЫХ
// ============================================================
struct Param {
  uint8_t cc;
  uint8_t value;
  uint8_t min;
  uint8_t max;
  uint8_t baseValue;
};

struct SequencerStep {
  bool active[4];
  uint8_t note;
  uint8_t cc;
  uint8_t wavesIndex;
  uint8_t retrigIndex;
};

struct State {
  // MAIN
  Param params[NUM_PARAMS] = {
    {00, 00, 0, 127, 00}, {00, 00, 0, 127, 00},
    {00, 00, 0, 127, 00}, {00, 00, 0, 127, 00}
  };
  bool frozen[NUM_PARAMS] = {false};
  bool frozenActive = false;
  // Бэкап заморозки на время глобального BypassMode::FREEZE — теперь
  // покрывает все 8 целей заморозки (P1-4 + AMT/WAV/WTH/ARM, та же
  // индексация, что и у SNOW-заморозки), а не только P1-4.
  bool frozenBackup[8];

  // STORM
  WeatherMode weatherMode = WeatherMode::SUN;
  uint8_t chaos = 00;
  uint8_t waveIntervalIndex = 2;
  bool randomizerEnabled = true;

  // ----- ПАРАМЕТРЫ ДЛЯ SUN -----
  uint8_t sunRflct = 0;   // 0..4, количество отражений
  // Бывший DSPRS (дисперсия) — теперь арпеджиатор нот для RFLCT:
  // 0=OFF, 1=Same, 2=5th, 3=Oct, 4=2Oct. См. sunArpSemitones[].
  uint8_t sunArp = 0;
  int8_t sunDflct = 00;   // 0..127, доп. шанс взвести RFLCT даже без выхода за CLAMP
  int8_t sunBias = 0;     // -63..64, смещение вверх/вниз

  // Состояние арпеджио RFLCT — взводится при аресте серии отражений,
  // ТОЛЬКО если нота секвенсора реально прозвучала на том же тике
  // (см. lastNoteOnTick ниже). По одному "заряду" на параметр (P1-4).
  bool sunArpActive[NUM_PARAMS] = {false};
  uint8_t sunArpBaseNote[NUM_PARAMS] = {0};
  uint8_t sunArpLastNote[NUM_PARAMS] = {0};
  // Тик, на котором секвенсор реально отправил Note On (обычный шаг
  // ИЛИ внутришаговый ретриггер) — используется для проверки строгого
  // совпадения по тику при аресте серии RFLCT.
  uint32_t lastNoteOnTick = 0;

  // ----- ПАРАМЕТРЫ ДЛЯ RAIN -----
  uint8_t rainDrip = 0;      // 0..127, разброс тайминга капли от клока на каждый параметр
  uint8_t rainWet = 114;     // 0..127, сила стягивания к базовому значению (было зашито 9/10 ≈ 114)
  uint8_t rainSplsh = 0;     // 0..127, шанс капли задеть соседний параметр
  bool rainThunder = false; // вкл/выкл случайные разряды-молнии

  // DRIP: у каждого параметра свой независимый тик следующей капли —
  // при DRIP=0 все четыре бьют строго по клоку синхронно, чем больше
  // DRIP, тем сильнее каждый "расстраивается" от общего интервала.
  uint32_t rainNextTick[NUM_PARAMS] = {0};
  // Вспышка-индикатор удара молнии (для drawRain()).
  bool rainStrikeActive = false;
  uint32_t rainStrikeTime = 0;

  // Состояние серии повторов RFLCT — по одному "заряду" на параметр.
  // sunReflectLevel: 0 = серия неактивна; 1..sunRflct = номер
  // ожидаемого повтора (интервал до него = Waves >> level, т.е.
  // 1/2, 1/4, 1/8, 1/16 текущего Waves — каждый следующий вдвое
  // быстрее предыдущего). Взводится в mutateParam(), когда параметр
  // упирается в свой CLAMP; отрабатывается по реальным тикам клока
  // в processSunReflects().
  uint8_t sunReflectLevel[NUM_PARAMS] = {0};
  uint32_t sunReflectNextTick[NUM_PARAMS] = {0};

  // ----- ПАРАМЕТРЫ ДЛЯ SNOW -----
  uint8_t snowFlake = 8;      // 3..12, число шагов евклидового секвенсора
  uint8_t snowRotation = 0;   // 0..11, поворот паттерна (по модулю snowFlake)
  uint8_t snowFrz = 0;        // 0..127, шанс заморозки/разморозки цели на "хит"
  int8_t snowTime = 0;        // -63..64, длительность заморозки (см. snowFreezeDuration)

  uint8_t snowStepIndex = 0;      // текущая позиция в паттерне (0..snowFlake-1)
  uint32_t snowLastStepTick = 0;  // тик последнего продвижения шага

  // Индикация "хита" евклидового секвенсора — короткая вспышка
  // снежинки за баром WEATHER на странице STORM (только в SNOW).
  bool snowPulseActive = false;
  uint32_t snowPulseTime = 0;

  // AMT/WAV/WEATHER/ARM не имеют штатного per-параметр freeze-флага
  // (frozen[] покрывает только P1-4) — заводим отдельные флаги под
  // остальные цели заморозки SNOW.
  bool frozenAmt = false;
  bool frozenWav = false;
  bool frozenWth = false;
  bool frozenArm = false;

  // Автоматическая разморозка целей, замороженных именно SNOW (FRZ),
  // по достижении заданного тика. Индексация целей: 0-3 = P1-4,
  // 4=AMT, 5=WAV, 6=WTH, 7=ARM.
  bool snowFreezeActive[8] = {false};
  uint32_t snowFreezeUntilTick[8] = {0};

  // LFO (для FOG)
  uint8_t lfoType = 0;
  int8_t lfoShape = 0;
  int8_t lfoPhase = 0;
  uint8_t lfoGlide = 0;
  float lfoPhaseAccum = 0.0f;
  float lfoCurrentValue = 0.0f;
  float lfoTargetValue = 0.0f;

  // SEQUENCER
  SequencerStep steps[SEQUENCER_STEPS];
  uint8_t sequencerSteps = 16;
  uint8_t sequencerScaleIndex = 0;
  uint8_t sequencerCC = 00;
  uint8_t sequencerBPM = 120;
  uint8_t sequencerCursor = 0;
  uint8_t sequencerPlayhead = 0;
  // Шаг, который реально триггерится/подсвечивается на этом тике.
  // Вне FREEZE всегда равен sequencerPlayhead. Во FREEZE равен
  // sequencerCursor — при этом sequencerPlayhead всё равно
  // продолжает молча продвигаться по кругу (см. onClockTick), чтобы
  // после выхода из FREEZE секвенсор оказался ровно там, где был бы,
  // если бы режим вообще не включался, а не "перезапускался".
  uint8_t sequencerDisplayStep = 0;
  uint32_t sequencerLastStepTick = 0;
  bool sequencerRunning = false;

  // Retrigger
  bool retriggerActive[4] = {false};
  bool lastActive[4] = {false};
  uint32_t retrigLastTickGlobal = 0;
  uint8_t lastNote = 72;
  uint8_t lastCC = 20;
  uint8_t lastWavesIndex = 2;
  uint32_t triggerTime[4] = {0};
  uint32_t retriggerTime[4] = {0};
  bool stepFlashActive = false;
  uint32_t stepFlashTime = 0;

  // MIDI
  volatile uint32_t midiTicks = 0;
  volatile uint32_t beatCounter = 0;
  volatile uint32_t barCounter = 0;
  volatile bool midiRunning = false;
  volatile uint32_t lastClockMicros = 0;
  volatile uint32_t clockAccumulator = 0;
  volatile uint8_t clockCount = 0;
  volatile float bpmSmooth = 0.0f;
  volatile float bpm = 0.0f;

  // ----- ГЛОБАЛЬНЫЕ НАСТРОЙКИ -----
  uint8_t midiChannel = 1;
  BypassMode bypassMode = BypassMode::OFF;
  // OFF — не выбираемый режим, это просто "выключено". Пользователь
  // выбирает между BYPASS и FREEZE на подстранице CC.
  BypassMode selectedBypassMode = BypassMode::FREEZE;
  SeqDest seqDest = SeqDest::WAV;
  bool gfxEnabled = true;

  bool bypassTransition = false;
  uint32_t transitionStart = 0;
  bool transitionDirection = false;

  // UI
  Page currentPage = Page::MAIN;
  uint8_t selectedParam = 0;
  uint8_t subPageMain = 0;
  uint8_t subPageCC = 0;
  uint8_t subPageSequencer = 0;
  uint8_t subPageStorm = 0;
  bool editMin = true;

  // Анимация
  bool animationPaused = false;
  uint32_t animationPauseTime = 0;
  uint32_t animationTimeOffset = 0;
  uint32_t frozenAnimTime = 0;
  uint32_t iceEffectTime = 0;

  bool displayDirty = true;

  // Сохранение
  bool needSaveMinMax = false;
  bool needSaveCC = false;
  bool needSaveStorm = false;
  bool needSaveGlobal = false;
  uint32_t lastSaveTime = 0;

  // Долгие нажатия
  uint32_t encLongPressStart = 0;
  bool encLongPressActive = false;
  bool encLongPressFrameVisible = false;
  uint32_t encLongPressLastBlink = 0;
  bool encLongPressTriggered = false;

  uint32_t tapPageLongPressStart = 0;
  bool tapPageLongPressActive = false;
  bool tapPageLongPressTriggered = false;
  bool tapPageLongPressFrameVisible = false;
  uint32_t tapPageLongPressLastBlink = 0;

  // TAP+PLAY 3с на странице SEQUENCER (подстраница шагов) — то же
  // самое randomizeSequencerAll(), что и у TAP+PAGE, просто другая
  // пара кнопок.
  uint32_t tapPlayLongPressStart = 0;
  bool tapPlayLongPressActive = false;
  bool tapPlayLongPressTriggered = false;
  bool tapPlayLongPressFrameVisible = false;
  uint32_t tapPlayLongPressLastBlink = 0;

  // Кнопки
  bool playWasPressed = false;
  uint32_t playPressTime = 0;
  uint32_t encSWLastPress = 0;
  bool encSWDoubleClicked = false;
  // Тройной клик ENC_SW — независимый от double-click счётчик (тот
  // сбрасывает свою метку времени при срабатывании, что мешало бы
  // отследить третий клик). Считает подряд идущие нажатия в пределах
  // DOUBLE_CLICK_TIME друг от друга.
  uint8_t encSWTripleCount = 0;
  uint32_t encSWTripleWindowStart = 0;
  uint32_t lastTapReleaseTime = 0;
  bool tapDoubleClicked = false;

  // ----- MIDI LEARN -----
  // Тройной клик ENC_SW включает ожидание: следующее входящее
  // CC-сообщение (с любого канала) переназначает CC-номер. Работает
  // на двух страницах: CC (подстраница 0, курсор на P1-4) и SEQUENCER
  // (подстраница 1 "setup", курсор на колонке CC) — какая из целей,
  // определяет midiLearnSequencerCC.
  bool midiLearnActive = false;
  uint8_t midiLearnParam = 0;
  bool midiLearnSequencerCC = false;
} state;

bool g_fillBackground = true;

// ============================================================
// FORWARD DECLARATIONS
// Используются до определения (updateButtons/processMIDI ссылаются
// на них раньше раздела 11/14) — нужны для сборки как .cpp.
// ============================================================
void randomizeCurrentPage();
void randomizeSequencerAll();
void resetToDefaults();
void resetSequencerState();
void saveSequencerSettings();

// ============================================================
// 4. КНОПКИ И ДЕБАУНС
// ============================================================
struct Button {
  uint8_t pin;
  bool lastStable;
  bool raw;
  uint32_t lastChange;
  bool processed;
};

Button buttons[4] = {
  {Pins::PLAY, HIGH, HIGH, 0, false},
  {Pins::TAP, HIGH, HIGH, 0, false},
  {Pins::PAGE, HIGH, HIGH, 0, false},
  {Pins::ENC_SW, HIGH, HIGH, 0, false}
};

constexpr uint32_t DEBOUNCE_MS = 35;
// У ENC_SW ход короче, чем у остальных кнопок, и двойной клик по нему
// обычно быстрее физически. При общем 35мс дебаунсе быстрый двойной
// клик рискует "слиться" в один: если кнопка не удержится в отпущенном
// состоянии дольше DEBOUNCE_MS, код вообще не зафиксирует отпускание
// между нажатиями (b.lastChange постоянно сбрасывается дребезгом), и
// два клика читаются как одно долгое нажатие — MANUAL FREEZE (двойной
// клик) в такие моменты просто не срабатывает. Меньший дебаунс именно
// для этой кнопки даёт больше запаса для быстрых кликов.
constexpr uint32_t ENC_SW_DEBOUNCE_MS = 15;
constexpr uint32_t DOUBLE_CLICK_TIME = 300;
constexpr uint32_t PLAY_HOLD_EXIT_TIME = 750;

// ============================================================
// 5. ЭНКОДЕР
// ============================================================
volatile int encoderTicks = 0;
volatile uint8_t lastEncState = 0;

void IRAM_ATTR encoderISR() {
  uint8_t A = digitalRead(Pins::ENC_A);
  uint8_t B = digitalRead(Pins::ENC_B);
  uint8_t stateEnc = (A << 1) | B;
  if (stateEnc != lastEncState) {
    int8_t dir = 0;
    if ((lastEncState == 0 && stateEnc == 1) ||
        (lastEncState == 1 && stateEnc == 3) ||
        (lastEncState == 3 && stateEnc == 2) ||
        (lastEncState == 2 && stateEnc == 0)) dir = 1;
    else if ((lastEncState == 0 && stateEnc == 2) ||
             (lastEncState == 2 && stateEnc == 3) ||
             (lastEncState == 3 && stateEnc == 1) ||
             (lastEncState == 1 && stateEnc == 0)) dir = -1;
    if (dir) encoderTicks += dir;
    lastEncState = stateEnc;
  }
}

// ============================================================
// 6. ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================
inline uint8_t clampU8(int v, uint8_t min, uint8_t max) {
  if (v < min) return min;
  if (v > max) return max;
  return (uint8_t)v;
}
inline int clampInt(int v, int min, int max) {
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

const uint8_t bayer[8][8] = {
  {0,32,8,40,2,34,10,42},
  {48,16,56,24,50,18,58,26},
  {12,44,4,36,14,46,6,38},
  {60,28,52,20,62,30,54,22},
  {3,35,11,43,1,33,9,41},
  {51,19,59,27,49,17,57,25},
  {15,47,7,39,13,45,5,37},
  {63,31,55,23,61,29,53,21}
};

inline uint8_t getNoise(uint8_t x, uint8_t y, uint32_t time) {
  uint32_t h = x * 374761393 + y * 668265263 + time * 1274126177;
  h = (h ^ (h >> 13)) * 1274126177;
  return (h ^ (h >> 16)) & 0xFF;
}

inline uint32_t getAnimTime() {
  if (state.animationPaused) return state.frozenAnimTime;
  return millis() - state.animationTimeOffset;
}

float computeLFO(float phase, uint8_t type, int8_t shape, int8_t phaseOffset) {
  float offset = (phaseOffset / 127.0f) * 2.0f * 3.14159f;
  float p = phase + offset;
  if (p > 2.0f * 3.14159f) p -= 2.0f * 3.14159f;
  if (p < 0) p += 2.0f * 3.14159f;
  float out = 0.0f;
  float shapeNorm = (float)shape / 64.0f;
  if (shapeNorm > 1.0f) shapeNorm = 1.0f;
  if (shapeNorm < -1.0f) shapeNorm = -1.0f;
  switch (type) {
    case 0: {
      // SHAPE в плюс: подмешиваем 2-ю/3-ю/4-ю гармоники — синус
      // обрастает частотами, становится "богаче". Веса гармоник в
      // сумме дают 1.0, поэтому harmonics сама по себе гарантированно
      // в пределах -1..1, и convex-блендинг с чистым синусом (тоже
      // -1..1) остаётся в тех же границах без доп. клампа.
      // SHAPE в минус: чем отрицательнее, тем сильнее жёсткий клиппинг
      // (дисторсия) и подмешанный шум — синус разваливается в шум.
      float sinP = sin(p);
      if (shapeNorm > 0) {
        float t = shapeNorm;
        float harmonics = 0.5f * sin(2.0f * p) + 0.3f * sin(3.0f * p) + 0.2f * sin(4.0f * p);
        out = sinP * (1.0f - t) + harmonics * t;
      } else if (shapeNorm < 0) {
        float amt = -shapeNorm;
        float driven = sinP * (1.0f + amt * 6.0f);
        if (driven > 1.0f) driven = 1.0f;
        if (driven < -1.0f) driven = -1.0f;
        float noise = (float)random(-1000, 1001) / 1000.0f;
        out = driven * (1.0f - amt * 0.6f) + noise * (amt * 0.6f);
      } else {
        out = sinP;
      }
      break;
    }
    case 1: {
      // tri — корректный треугольник (-1..1), общая база и для
      // shapeNorm=0, и как отправная точка морфа в пилу; непрерывен
      // от skew=0 (чистый треугольник) до skew=1 (чистая пила), т.к.
      // обе стороны сходятся к одному tri.
      float t = p / (2.0f * 3.14159f);
      float tri = 4.0f * fabs(t - floor(t + 0.5f)) - 1.0f;
      if (shapeNorm < 0) {
        float skew = -shapeNorm;
        out = tri * (1 - skew) + (2.0f * t - 1.0f) * skew;
      } else if (shapeNorm > 0) {
        float skew = shapeNorm;
        out = tri * (1 - skew) + (1.0f - 2.0f * t) * skew;
      } else out = tri;
      break;
    }
    case 2: {
      float duty = 0.5f + shapeNorm * 0.45f;
      float t = p / (2.0f * 3.14159f);
      float phaseFrac = t - floor(t);
      out = (phaseFrac < duty) ? 1.0f : -1.0f;
      break;
    }
    default: out = 0;
  }
  return out;
}

void midiNoteToString(uint8_t note, char* out) {
  const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int octave = (note / 12) - 1;
  int ni = note % 12;
  sprintf(out, "%s%d", notes[ni], octave);
}

// ============================================================
// 7. MIDI-ФУНКЦИИ
// ============================================================
// BYPASS полностью глушит исходящий MIDI: движок (рандомайзер,
// секвенсор, SNOW и т.д.) продолжает работать как обычно, но наружу
// ничего не уходит — единая точка контроля для всех трёх функций
// отправки, независимо от того, кто их вызвал.
void sendCC(uint8_t cc, uint8_t val) {
  if (state.bypassMode == BypassMode::BYPASS) return;
  uint8_t status = 0xB0 | ((state.midiChannel - 1) & 0x0F);
  midi.write(status); midi.write(cc & 0x7F); midi.write(val & 0x7F);
}
void sendNoteOn(uint8_t note, uint8_t vel) {
  if (state.bypassMode == BypassMode::BYPASS) return;
  uint8_t status = 0x90 | ((state.midiChannel - 1) & 0x0F);
  midi.write(status); midi.write(note & 0x7F); midi.write(vel & 0x7F);
}
void sendNoteOff(uint8_t note) {
  if (state.bypassMode == BypassMode::BYPASS) return;
  uint8_t status = 0x80 | ((state.midiChannel - 1) & 0x0F);
  midi.write(status); midi.write(note & 0x7F); midi.write(0);
}

inline int getRandomAmount() {
  return map(state.chaos, 0, 100, 0, 63);
}

// Транспозиция в полутонах НА ОДИН уровень отражения, по режиму
// sunArp (0=OFF, 1=Same, 2=5th, 3=Oct, 4=2Oct). Итоговая транспозиция
// на уровне N = sunArpSemitones[sunArp] * N (см. armSunReflect и
// processSunReflects) — каждое следующее отражение в каскаде уезжает
// на ту же величину дальше.
const int8_t sunArpSemitones[5] = {0, 0, 7, 12, 24};
const char* sunArpNames[5] = {"OFF", "SAME", "5th", "OCT", "2OCT"};

// Взводит очередной уровень серии RFLCT для параметра idx.
// Интервал до срабатывания = текущий Waves-интервал, делённый на
// 2^level (level=1 -> 1/2 Waves, level=2 -> 1/4 Waves, ...) — каждый
// следующий повтор ударяет вдвое быстрее предыдущего.
void armSunReflect(uint8_t idx, uint8_t level) {
  uint32_t waveInterval = waveIntervals[state.waveIntervalIndex];
  uint32_t interval = waveInterval >> level;
  if (interval < 1) interval = 1;
  state.sunReflectLevel[idx] = level;
  state.sunReflectNextTick[idx] = state.midiTicks + interval;
}

// Гасит зависшую арпеджио-ноту RFLCT (если была) и снимает флаг
// активности для параметра idx — вызывается везде, где каскад
// обрывается или не начинается (не только там, где заканчивается
// естественным путём).
void stopSunArp(uint8_t idx) {
  if (!state.sunArpActive[idx]) return;
  sendNoteOff(state.sunArpLastNote[idx]);
  state.sunArpActive[idx] = false;
}

void mutateParam(uint8_t idx) {
  if (state.frozen[idx] || idx >= NUM_PARAMS) return;
  int amt = getRandomAmount();
  int newVal = state.params[idx].value;
  uint8_t base = state.params[idx].baseValue;

  switch (state.weatherMode) {
    case WeatherMode::SUN: {
      // BIAS смещает распределение
      int bias = state.sunBias;  // -63..64
      int offset = random(-amt, amt + 1) + bias;
      newVal = state.params[idx].value + offset;

      // Триггер RFLCT: значение этого параметра вышло за его CLAMP —
      // гарантированный (100%) повод взвести серию.
      bool hitClamp = (newVal < state.params[idx].min || newVal > state.params[idx].max);
      // DFLCT: независимо от CLAMP, на каждой мутации даёт свой шанс
      // взвести серию (0..127 -> 0..~100%). Не заменяет hitClamp, а
      // добавляет параллельный повод для повторов.
      bool dflctTrigger = ((int)random(0, 128) < state.sunDflct);

      newVal = clampU8(newVal, state.params[idx].min, state.params[idx].max);
      state.params[idx].value = (uint8_t)newVal;
      state.params[idx].baseValue = (uint8_t)newVal;
      sendCC(state.params[idx].cc, state.params[idx].value);

      // ----- ЛОГИКА RFLCT -----
      // Не шлём повтор мгновенно здесь (внутри одного вызова CC
      // подряд неотличимы на слух). Вместо этого взводим 1-й уровень
      // серии для ЭТОГО параметра — он сработает через Waves/2 тиков
      // в processSunReflects(). Если тот повтор снова упрётся в
      // CLAMP, взведётся следующий уровень через вдвое меньший
      // интервал (Waves/4), и так вплоть до sunRflct уровней (0..4).
      if ((hitClamp || dflctTrigger) && state.sunRflct > 0) {
        armSunReflect(idx, 1);
        // Арпеджиатор RFLCT: взводится ТОЛЬКО если нота секвенсора
        // реально прозвучала на этом же тике (строгое совпадение).
        // Сама нота на старте не дублируется — секвенсор её уже
        // сыграл; арпеджио озвучивает именно последующие повторы.
        if (state.sunArp != 0 && state.lastNoteOnTick == state.midiTicks) {
          state.sunArpActive[idx] = true;
          state.sunArpBaseNote[idx] = state.lastNote;
          state.sunArpLastNote[idx] = state.lastNote;
        } else {
          stopSunArp(idx);
        }
      } else {
        state.sunReflectLevel[idx] = 0;
        stopSunArp(idx);
      }
      break;
    }
    case WeatherMode::FOG:
      newVal += random(-amt, amt + 1);
      newVal = clampU8(newVal, state.params[idx].min, state.params[idx].max);
      state.params[idx].value = (uint8_t)newVal;
      sendCC(state.params[idx].cc, state.params[idx].value);
      break;
    case WeatherMode::RAIN: {
      newVal += random(-amt, amt + 1);
      // Каплю "тянет" обратно к базовому (home) значению — WET
      // задаёт силу этого стягивания: 0 = чистое случайное блуждание
      // (как FOG), 127 = почти не сдвигается с места.
      bool hitClamp = (newVal < state.params[idx].min || newVal > state.params[idx].max);
      newVal = (int)(((long)newVal * (127 - state.rainWet) + (long)base * state.rainWet) / 127);
      newVal = clampU8(newVal, state.params[idx].min, state.params[idx].max);
      state.params[idx].value = (uint8_t)newVal;
      sendCC(state.params[idx].cc, state.params[idx].value);

      // SPLSH: капля, "приземлившаяся" за CLAMP, может задеть один из
      // соседних параметров небольшим случайным толчком.
      if (hitClamp && state.rainSplsh > 0 && (int)random(0, 128) < state.rainSplsh) {
        uint8_t neighbor = (random(0, 2) == 0) ? (idx + NUM_PARAMS - 1) % NUM_PARAMS : (idx + 1) % NUM_PARAMS;
        if (!state.frozen[neighbor]) {
          int splashAmt = amt / 2;
          int nv = state.params[neighbor].value + random(-splashAmt, splashAmt + 1);
          nv = clampU8(nv, state.params[neighbor].min, state.params[neighbor].max);
          state.params[neighbor].value = (uint8_t)nv;
          sendCC(state.params[neighbor].cc, state.params[neighbor].value);
        }
      }
      break;
    }
    case WeatherMode::SNOW:
      // Евклидов ритм лишь решает, КОГДА применить изменение
      // (см. processSnowSequencer); сама операция — обычный
      // AMT-масштабированный оффсет, как у FOG.
      newVal += random(-amt, amt + 1);
      newVal = clampU8(newVal, state.params[idx].min, state.params[idx].max);
      state.params[idx].value = (uint8_t)newVal;
      sendCC(state.params[idx].cc, state.params[idx].value);
      break;
  }
}

// THNDR: пока включён, на каждый реальный тик есть небольшой шанс
// "разряда" (растёт вместе с AMT) — случайный незамороженный параметр
// мгновенно выбивается в свой крайний край (min или max). baseValue
// НЕ трогаем: последующие обычные капли (WET) сами утянут значение
// обратно — гроза естественным образом "стихает" через уже
// существующий механизм, без отдельного таймера восстановления.
void processRainThunder(int amt) {
  if (!state.rainThunder) return;
  if ((int)random(0, 4000) >= amt) return;
  uint8_t idx = random(0, NUM_PARAMS);
  if (state.frozen[idx]) return;
  uint8_t extreme = (random(0, 2) == 0) ? state.params[idx].min : state.params[idx].max;
  state.params[idx].value = extreme;
  sendCC(state.params[idx].cc, extreme);
  state.rainStrikeActive = true;
  state.rainStrikeTime = millis();
}

// DRIP: продвигает "капли" RAIN по их собственному, слегка
// расстроенному от общего клока графику. При DRIP=0 у всех четырёх
// параметров rainNextTick шагает ровно на interval, так что они
// остаются синхронны — как обычный modulo-триггер. Чем выше DRIP,
// тем шире случайный разброс (jitter) вокруг interval у каждого
// параметра, и они всё сильнее "расходятся" по времени друг с другом
// и с клоком.
void processRainDrops(uint32_t interval) {
  int maxJitter = (int)(((long)interval / 2) * state.rainDrip / 127);
  for (uint8_t i = 0; i < NUM_PARAMS; i++) {
    if (state.midiTicks < state.rainNextTick[i]) continue;
    mutateParam(i);
    int jitter = (maxJitter > 0) ? random(-maxJitter, maxJitter + 1) : 0;
    long next = (long)state.midiTicks + (long)interval + jitter;
    if (next <= (long)state.midiTicks) next = state.midiTicks + 1;
    state.rainNextTick[i] = (uint32_t)next;
  }
  processRainThunder(getRandomAmount());
}

// Отыгрывает срабатывания серии RFLCT по реальным тикам клока.
// Когда подходит запланированный тик для параметра idx, операция
// мутации повторно применяется к нему (тот же расчёт offset/bias,
// что и в обычной мутации). Если результат снова упёрся в CLAMP и
// лимит sunRflct ещё не исчерпан — взводится следующий уровень с
// вдвое меньшим интервалом (Waves/2 -> Waves/4 -> Waves/8 ->
// Waves/16). Если в диапазон — серия останавливается.
void processSunReflects() {
  if (state.weatherMode != WeatherMode::SUN) {
    // Смена режима погоды гасит все незавершённые серии.
    for (uint8_t idx = 0; idx < NUM_PARAMS; idx++) {
      state.sunReflectLevel[idx] = 0;
      stopSunArp(idx);
    }
    return;
  }
  for (uint8_t idx = 0; idx < NUM_PARAMS; idx++) {
    uint8_t level = state.sunReflectLevel[idx];
    if (level == 0) continue;
    if (state.frozen[idx]) {
      state.sunReflectLevel[idx] = 0;
      stopSunArp(idx);
      continue;
    }
    if (state.midiTicks < state.sunReflectNextTick[idx]) continue;

    int amt = getRandomAmount();
    int bias = state.sunBias;
    int offset = random(-amt, amt + 1) + bias;
    int reflectVal = state.params[idx].value + offset;
    bool hitClampAgain = (reflectVal < state.params[idx].min || reflectVal > state.params[idx].max);
    reflectVal = clampU8(reflectVal, state.params[idx].min, state.params[idx].max);
    state.params[idx].value = (uint8_t)reflectVal;
    state.params[idx].baseValue = (uint8_t)reflectVal;
    sendCC(state.params[idx].cc, state.params[idx].value);

    // Арпеджио: это отражение (уровень level) транспонирует базовую
    // ноту на sunArpSemitones[sunArp]*level полутонов — накопительно
    // от каскада к каскаду, как и просили (3 отражения + Oct = финал
    // на 3 октавы выше).
    if (state.sunArpActive[idx]) {
      int semitones = (int)sunArpSemitones[state.sunArp] * level;
      int transposed = clampInt((int)state.sunArpBaseNote[idx] + semitones, 0, 127);
      sendNoteOff(state.sunArpLastNote[idx]);
      sendNoteOn((uint8_t)transposed, 100);
      state.sunArpLastNote[idx] = (uint8_t)transposed;
    }

    if (hitClampAgain && level < state.sunRflct) {
      armSunReflect(idx, level + 1);
    } else {
      state.sunReflectLevel[idx] = 0;
      stopSunArp(idx);
    }
  }
}

// ============================================================
// SNOW — евклидов секвенсор + заморозка целей
// ============================================================
// Индексация целей заморозки: 0-3 = P1-4, 4 = AMT, 5 = WAV,
// 6 = WTH (weatherMode), 7 = ARM (randomizerEnabled).
constexpr uint8_t SNOW_FREEZE_TARGETS = 8;

void updateFrozenActive() {
  state.frozenActive = false;
  for (uint8_t i = 0; i < NUM_PARAMS; i++) if (state.frozen[i]) state.frozenActive = true;
}

bool getSnowTargetFrozen(uint8_t target) {
  switch (target) {
    case 0: case 1: case 2: case 3: return state.frozen[target];
    case 4: return state.frozenAmt;
    case 5: return state.frozenWav;
    case 6: return state.frozenWth;
    default: return state.frozenArm;
  }
}

void setSnowTargetFrozen(uint8_t target, bool f) {
  switch (target) {
    case 0: case 1: case 2: case 3: state.frozen[target] = f; break;
    case 4: state.frozenAmt = f; break;
    case 5: state.frozenWav = f; break;
    case 6: state.frozenWth = f; break;
    default: state.frozenArm = f; break;
  }
  if (target < NUM_PARAMS) updateFrozenActive();
}

// Простая аппроксимация евклидова ритма (эквивалентна Bjorklund для
// небольших N): хит на шаге i, если целая часть (i*pulses/steps)
// отличается от целой части ((i-1)*pulses/steps) — здесь считается
// через остаток от деления, что эквивалентно и не требует рекурсии.
bool euclideanHit(uint8_t i, uint8_t steps, uint8_t pulses) {
  if (pulses == 0) return false;
  if (pulses >= steps) return true;
  return (((uint16_t)i * pulses) % steps) < pulses;
}

// Длительность заморозки (в тиках) по параметру TIME:
// TIME=0 -> длина одного шага секвенсора (stepTicks);
// TIME 0..64 -> линейно растёт до длины всего паттерна (patternTicks);
// TIME 0..-63 -> случайный разброс вокруг stepTicks, чем отрицательнее,
// тем шире разброс (и тем более рандомна длина между параметрами).
uint32_t snowFreezeDuration(uint32_t stepTicks, uint32_t patternTicks) {
  if (state.snowTime >= 0) {
    return map(state.snowTime, 0, 64, stepTicks, patternTicks);
  }
  uint8_t variancePct = map(-state.snowTime, 0, 63, 0, 100);
  int32_t range = (int32_t)((stepTicks * variancePct) / 100);
  int32_t duration = (int32_t)stepTicks + (range > 0 ? random(-range, range + 1) : 0);
  if (duration < 1) duration = 1;
  return (uint32_t)duration;
}

// Продвигает евклидов секвенсор SNOW на текущем тике. Один шаг
// паттерна занимает interval тиков (interval берётся из текущего
// WAVES — та же величина, что задаёт интервал обычного рандомайзера
// у FOG/RAIN/SUN). На "хит"-шаге паттерна применяется мутация ко
// всем P1-4 через AMT, и для каждой из 8 целей заморозки независимо
// бросается шанс FRZ на заморозку/разморозку.
void processSnowSequencer(uint32_t interval) {
  if (interval == 0) return;
  if (state.midiTicks - state.snowLastStepTick < interval) return;
  state.snowLastStepTick = state.midiTicks;

  uint8_t steps = clampU8(state.snowFlake, 3, 12);
  uint8_t pulses = clampU8(map(state.chaos, 0, 100, 1, steps), 1, steps);
  uint8_t rotation = state.snowRotation % steps;
  uint8_t patternIdx = (state.snowStepIndex + rotation) % steps;
  bool hit = euclideanHit(patternIdx, steps, pulses);

  if (hit) {
    for (uint8_t i = 0; i < NUM_PARAMS; i++) mutateParam(i);

    uint32_t stepTicks = interval;
    uint32_t patternTicks = interval * steps;
    for (uint8_t target = 0; target < SNOW_FREEZE_TARGETS; target++) {
      if ((int)random(0, 128) >= state.snowFrz) continue; // не повезло
      if (getSnowTargetFrozen(target)) {
        setSnowTargetFrozen(target, false);
        state.snowFreezeActive[target] = false;
      } else {
        setSnowTargetFrozen(target, true);
        state.snowFreezeActive[target] = true;
        state.snowFreezeUntilTick[target] = state.midiTicks + snowFreezeDuration(stepTicks, patternTicks);
      }
    }
    state.iceEffectTime = millis();
    state.snowPulseActive = true;
    state.snowPulseTime = millis();
  }

  state.snowStepIndex = (state.snowStepIndex + 1) % steps;
}

// Снимает заморозку с целей, взведённых SNOW, по истечении их
// таймера. Работает независимо от текущего weatherMode — переключение
// погоды не должно "замораживать" уже идущий отсчёт навечно.
void processSnowFreezeExpiry() {
  for (uint8_t target = 0; target < SNOW_FREEZE_TARGETS; target++) {
    if (!state.snowFreezeActive[target]) continue;
    if (state.midiTicks >= state.snowFreezeUntilTick[target]) {
      setSnowTargetFrozen(target, false);
      state.snowFreezeActive[target] = false;
    }
  }
}

// ============================================================
// Глобальный BypassMode::FREEZE — замораживает/размораживает ВСЕ
// 8 целей (P1-4 + AMT/WAV/WTH/ARM), переиспользуя те же примитивы,
// что и точечная заморозка SNOW. Вход бэкапит текущее состояние и
// инвертирует его; выход восстанавливает бэкап — то есть буквально
// "инвертирует фризы параметров" в обе стороны, как и требуется.
void enterGlobalFreeze() {
  for (uint8_t t = 0; t < SNOW_FREEZE_TARGETS; t++) {
    state.frozenBackup[t] = getSnowTargetFrozen(t);
    setSnowTargetFrozen(t, !state.frozenBackup[t]);
  }
}

void exitGlobalFreeze() {
  for (uint8_t t = 0; t < SNOW_FREEZE_TARGETS; t++) {
    setSnowTargetFrozen(t, state.frozenBackup[t]);
  }
}

void triggerSequencerStep(uint8_t stepIndex, bool retrigger = false) {
  if (stepIndex >= state.sequencerSteps) return;
  uint32_t now = millis();
  for (uint8_t p = 0; p < 4; p++) {
    if (state.steps[stepIndex].active[p]) {
      if (retrigger) state.retriggerTime[p] = now;
      else state.triggerTime[p] = now;
      switch (p) {
        case 0:
          if (retrigger) sendNoteOff(state.steps[stepIndex].note);
          sendNoteOn(state.steps[stepIndex].note, 100);
          // Метка тика для проверки строгого совпадения по тику у
          // арпеджиатора RFLCT (см. sunArp / mutateParam).
          state.lastNoteOnTick = state.midiTicks;
          break;
        case 1:
          sendCC(state.sequencerCC, state.steps[stepIndex].cc);
          break;
        case 2: {
          uint8_t val = state.steps[stepIndex].wavesIndex;
          switch (state.seqDest) {
            case SeqDest::WAV:
              if (!state.frozenWav) state.waveIntervalIndex = constrain(val, 0, 4);
              break;
            case SeqDest::AMT:
              if (!state.frozenAmt) state.chaos = constrain(map(val, 0, 127, 0, 100), 0, 100);
              break;
            case SeqDest::WTH: {
              if (!state.frozenWth) {
                uint8_t mode = constrain(val, 0, 3);
                state.weatherMode = (WeatherMode)mode;
              }
              break;
            }
          }
          break;
        }
        case 3:
          break;
      }
    }
  }
}

// ============================================================
// 8. ОБРАБОТКА MIDI CLOCK
// ============================================================
uint32_t lastInternalTickMicros = 0;

// Генерирует синтетический тик MIDI-клока (24 ppqn) из
// state.sequencerBPM, когда нет внешнего клока. Возвращает true
// не чаще одного раза за проход границы тика — это ЕДИНСТВЕННОЕ,
// что должно управлять привязанной к темпу логикой во внутреннем
// режиме.
bool generateInternalTicks() {
  if (!state.sequencerRunning) return false;
  uint32_t now = micros();
  if (lastInternalTickMicros == 0) {
    lastInternalTickMicros = now;
    return false;
  }
  float bpm = state.sequencerBPM;
  if (bpm < 1) bpm = 120;
  float tickPeriodUs = (60.0f / bpm) * 1000000.0f / 24.0f;
  uint32_t period = (uint32_t)tickPeriodUs;
  if (now - lastInternalTickMicros >= period) {
    lastInternalTickMicros += period;
    return true;
  }
  return false;
}

// Вся логика "на каждый тик" вызывается СТРОГО один раз за реальный
// тик — либо из processExternalClockTick() (на каждый 0xF8), либо из
// loop(), когда generateInternalTicks() вернул true.
void onClockTick() {
  state.midiTicks++;
  if (state.midiTicks % 24 == 0) {
    state.beatCounter++;
    if (state.beatCounter % 4 == 0) state.barCounter++;
  }

  bool tapPressed = (buttons[1].lastStable == LOW);

  // Автоматическая разморозка целей, замороженных SNOW-серией —
  // должна работать всегда, даже если сейчас выбран другой режим.
  processSnowFreezeExpiry();

  // Sequencer идёт перед Randomizer: к моменту ареста серии RFLCT
  // (внутри Randomizer -> mutateParam) уже должно быть известно,
  // отправил ли секвенсор Note On именно на этом тике
  // (state.lastNoteOnTick) — иначе проверка "совпадения по тику" для
  // арпеджиатора SUN смотрела бы на события прошлого тика.
  if (state.sequencerRunning) {
    uint8_t scale = scaleMultipliers[state.sequencerScaleIndex];
    uint32_t ticksPerStep = 12 * scale / 2;
    bool tick = (state.midiTicks - state.sequencerLastStepTick >= ticksPerStep);
    if (tick) {
      state.sequencerLastStepTick = state.midiTicks;
      // Реальный плейхед продвигается всегда, независимо от FREEZE —
      // это и даёт бесшовное продолжение после выхода из режима
      // (см. комментарий у sequencerDisplayStep).
      state.sequencerPlayhead = (state.sequencerPlayhead + 1) % state.sequencerSteps;
      // Во FREEZE триггерим и подсвечиваем шаг под курсором, не
      // трогая при этом настоящий плейхед выше.
      uint8_t newPlayhead = (state.bypassMode == BypassMode::FREEZE)
        ? state.sequencerCursor : state.sequencerPlayhead;
      state.sequencerDisplayStep = newPlayhead;
      uint8_t retrigIdx = state.steps[newPlayhead].retrigIndex;
      for (uint8_t p = 0; p < 4; p++) {
        bool active = state.steps[newPlayhead].active[p];
        if (active) {
          if (p == 0) state.lastNote = state.steps[newPlayhead].note;
          if (p == 1) state.lastCC = state.steps[newPlayhead].cc;
          if (p == 2) state.lastWavesIndex = state.steps[newPlayhead].wavesIndex;
          state.retriggerActive[p] = true;
          state.lastActive[p] = true;
        } else {
          if (retrigIdx == 0) state.retriggerActive[p] = false;
          state.lastActive[p] = false;
        }
      }
      state.stepFlashActive = true;
      state.stepFlashTime = millis();
      triggerSequencerStep(newPlayhead, false);
    }

    // Retrigger — тоже относительно отображаемого/триггеримого шага
    // (во FREEZE это шаг под курсором, не молча идущий плейхед).
    uint8_t playhead = state.sequencerDisplayStep;
    uint8_t retrigIdx = state.steps[playhead].retrigIndex;
    if (retrigIdx > 0 && retrigIdx < 5) {
      uint32_t interval = retrigIntervals[retrigIdx];
      uint32_t elapsed = state.midiTicks - state.retrigLastTickGlobal;
      for (uint8_t p = 0; p < 4; p++) {
        if (state.retriggerActive[p] && elapsed >= interval) {
          if (p == 0) { sendNoteOff(state.lastNote); sendNoteOn(state.lastNote, 100); state.lastNoteOnTick = state.midiTicks; }
          if (p == 1) sendCC(state.sequencerCC, state.lastCC);
          if (p == 2) {
            uint8_t val = state.lastWavesIndex;
            switch (state.seqDest) {
              case SeqDest::WAV: if (!state.frozenWav) state.waveIntervalIndex = constrain(val, 0, 4); break;
              case SeqDest::AMT: if (!state.frozenAmt) state.chaos = constrain(map(val, 0, 127, 0, 100), 0, 100); break;
              case SeqDest::WTH: if (!state.frozenWth) state.weatherMode = (WeatherMode)constrain(val, 0, 3); break;
            }
          }
          state.retrigLastTickGlobal = state.midiTicks;
          state.retriggerTime[p] = millis();
        }
      }
    } else {
      for (uint8_t p = 0; p < 4; p++) state.retriggerActive[p] = false;
      state.retrigLastTickGlobal = state.midiTicks;
    }
  }

  // Randomizer
  if (!tapPressed && state.randomizerEnabled && state.waveIntervalIndex < 5) {
    uint8_t interval = waveIntervals[state.waveIntervalIndex];
    if (state.weatherMode == WeatherMode::SNOW) {
      // SNOW заменяет плоский modulo-триггер евклидовым секвенсором,
      // идущим с тем же интервалом WAVES между шагами паттерна.
      processSnowSequencer(interval);
    } else if (state.weatherMode == WeatherMode::RAIN) {
      // RAIN: у каждого параметра свой, расстроенный от клока по
      // DRIP тайминг капель (см. processRainDrops), плюс THNDR.
      processRainDrops(interval);
    } else if (state.midiTicks % interval == 0) {
      for (uint8_t i = 0; i < NUM_PARAMS; i++) mutateParam(i);
    }
  }

  if (state.stepFlashActive && (millis() - state.stepFlashTime > 100)) state.stepFlashActive = false;
  if (state.snowPulseActive && (millis() - state.snowPulseTime > 150)) state.snowPulseActive = false;
  if (state.rainStrikeActive && (millis() - state.rainStrikeTime > 120)) state.rainStrikeActive = false;

  // Отыгрываем взведённые серии повторов RFLCT (SUN) — по тикам,
  // независимо от wave-интервала рандомайзера.
  processSunReflects();

  // LFO для FOG
  if (state.weatherMode == WeatherMode::FOG && state.randomizerEnabled) {
    float phaseIncrement = 2.0f * 3.14159f / (float)waveIntervals[state.waveIntervalIndex];
    state.lfoPhaseAccum += phaseIncrement;
    if (state.lfoPhaseAccum > 2.0f * 3.14159f) state.lfoPhaseAccum -= 2.0f * 3.14159f;
    float raw = computeLFO(state.lfoPhaseAccum, state.lfoType, state.lfoShape, state.lfoPhase);
    // GLIDE=0 -> коэффициент 1.0 (мгновенное отслеживание), GLIDE=127
    // -> маленький, но ненулевой коэффициент (медленный, но живой
    // глайд — никогда не застывает совсем).
    float glideFactor = 1.0f - (state.lfoGlide / 127.0f) * 0.98f;
    state.lfoTargetValue = raw;
    state.lfoCurrentValue += (state.lfoTargetValue - state.lfoCurrentValue) * glideFactor;
    float amp = getRandomAmount() / 63.0f;
    for (uint8_t i = 0; i < NUM_PARAMS; i++) {
      if (state.frozen[i]) continue;
      int center = (state.params[i].min + state.params[i].max) / 2;
      int halfRange = (state.params[i].max - state.params[i].min) / 2;
      int delta = (int)(state.lfoCurrentValue * amp * halfRange);
      int newVal = center + delta;
      newVal = clampU8(newVal, state.params[i].min, state.params[i].max);
      state.params[i].value = newVal;
      sendCC(state.params[i].cc, newVal);
    }
  }
}

// Вызывается на каждый принятый байт 0xF8 (внешний MIDI-клок).
// Оценивает внешний BPM по реальным интервалам между байтами,
// затем прогоняет общую per-tick логику через onClockTick().
void processExternalClockTick() {
  uint32_t now = micros();
  if (state.lastClockMicros) {
    uint32_t delta = now - state.lastClockMicros;
    if (delta > 500 && delta < 100000) {
      state.clockAccumulator += delta;
      if (++state.clockCount >= 24) {
        float avg = (float)state.clockAccumulator / state.clockCount;
        float bpmInst = 60.0f / (avg / 1000000.0f * 24.0f);
        state.bpmSmooth = (state.bpmSmooth == 0) ? bpmInst : state.bpmSmooth * 0.7f + bpmInst * 0.3f;
        state.bpm = state.bpmSmooth;
        state.clockAccumulator = 0; state.clockCount = 0;
      }
    } else {
      state.clockAccumulator = 0; state.clockCount = 0;
    }
  }
  state.lastClockMicros = now;
  onClockTick();
}

// ----- Минимальный парсер входящих канальных сообщений -----
// Нужен только для MIDI Learn (детектирования входящих Control Change).
// Состояние running status переживает между вызовами processMIDI(),
// т.к. байты одного сообщения могут прийти по кускам в разных
// проходах loop().
uint8_t midiRunningStatus = 0;
uint8_t midiParsePendingByte = 0;
bool midiParseHaveFirstData = false;

// Сколько data-байт ждать после статус-байта данного типа.
uint8_t midiDataBytesFor(uint8_t status) {
  uint8_t type = status & 0xF0;
  if (type == 0xC0 || type == 0xD0) return 1; // Program Change, Channel Pressure
  return 2; // Note On/Off, Poly AT, CC, Pitch Bend
}

// MIDI Learn: сработавшее входящее CC переназначает CC-номер
// параметра под курсором. Канал не фильтруем (omni) — контроллер
// может слать не на том канале, что настроен для исходящих сообщений
// этого устройства.
void handleIncomingCC(uint8_t cc, uint8_t val) {
  (void)val;
  if (!state.midiLearnActive) return;
  if (state.midiLearnSequencerCC) {
    state.sequencerCC = cc;
  } else {
    uint8_t idx = state.midiLearnParam;
    if (idx >= NUM_PARAMS) { state.midiLearnActive = false; return; }
    state.params[idx].cc = cc;
    state.needSaveCC = true;
  }
  state.midiLearnActive = false;
  state.displayDirty = true;
}

void processMIDI() {
  uint8_t count = 0;
  while (midi.available() && count < 64) {
    uint8_t data = midi.read();
    count++;
    // FREEZE не про MIDI-thru, а про заморозку параметров и
    // зацикливание шага секвенсора — входящий поток всегда
    // обрабатывается как обычно; блокировка исходящего сигнала
    // реализована отдельно в sendCC/sendNoteOn/sendNoteOff (BYPASS).
    if (data >= 0xF8) {
      // System Realtime — не влияет на running status канальных сообщений.
      switch (data) {
        case 0xF8: processExternalClockTick(); break;
        case 0xFA:
          state.midiRunning = true;
          state.midiTicks = 0; state.beatCounter = 0; state.barCounter = 0;
          state.lastClockMicros = 0; state.clockAccumulator = 0; state.clockCount = 0; state.bpmSmooth = 0;
          state.sequencerRunning = true;
          // Start приводит секвенсор в то же чистое состояние, что и Stop.
          resetSequencerState();
          break;
        case 0xFB: state.midiRunning = true; state.sequencerRunning = true; break;
        case 0xFC:
          state.midiRunning = false;
          state.sequencerRunning = false;
          resetSequencerState();
          break;
      }
      continue;
    }
    if (data & 0x80) {
      // Новый статус-байт канального сообщения (или System Common
      // 0xF1-F7, который мы просто игнорируем как валидный статус
      // без интересующих нас data-байт).
      midiRunningStatus = data;
      midiParseHaveFirstData = false;
      continue;
    }
    // Data-байт — копим по running status.
    if (midiRunningStatus == 0) continue; // байт без статуса — мусор, пропускаем
    uint8_t need = midiDataBytesFor(midiRunningStatus);
    if (need == 1) {
      // Program Change / Channel Pressure нам не нужны — просто съедаем байт.
      continue;
    }
    if (!midiParseHaveFirstData) {
      midiParsePendingByte = data;
      midiParseHaveFirstData = true;
    } else {
      if ((midiRunningStatus & 0xF0) == 0xB0) {
        handleIncomingCC(midiParsePendingByte & 0x7F, data & 0x7F);
      }
      midiParseHaveFirstData = false;
    }
  }
}

// ============================================================
// 9. ОБРАБОТКА КНОПОК
// ============================================================
void updateButtons() {
  static uint32_t tapReleaseTime = 0;
  static bool tapWasReleased = false;

  for (uint8_t i = 0; i < 4; i++) {
    Button &b = buttons[i];
    bool raw = digitalRead(b.pin);
    if (raw != b.raw) { b.raw = raw; b.lastChange = millis(); }
    uint32_t debounceMs = (i == 3) ? ENC_SW_DEBOUNCE_MS : DEBOUNCE_MS;
    if (millis() - b.lastChange >= debounceMs && b.lastStable != b.raw) {
      bool prev = b.lastStable;
      b.lastStable = b.raw;
      if (prev == LOW && b.lastStable == HIGH) {
        // Отпускание
        if (i == 1) { tapReleaseTime = millis(); tapWasReleased = true; }
        else if (i == 0 && state.playWasPressed) {
          if (buttons[1].lastStable == LOW) {
            randomizeCurrentPage();
          } else {
            // Вход уже произошёл по нажатию (см. обработку нажатия
            // выше). Здесь — только перфоманс-выход: держишь PLAY
            // дольше PLAY_HOLD_EXIT_TIME и отпускаешь — режим гасится.
            // Быстрое нажатие-отпускание (<=0.75с) ничего не делает —
            // режим остаётся включённым "залипшим", пока его не снимут
            // именно долгим удержанием с отпусканием.
            uint32_t heldMs = millis() - state.playPressTime;
            if (heldMs > PLAY_HOLD_EXIT_TIME && state.bypassMode != BypassMode::OFF) {
              bool wasFreeze = (state.bypassMode == BypassMode::FREEZE);
              state.bypassMode = BypassMode::OFF;
              if (wasFreeze) exitGlobalFreeze();
              state.iceEffectTime = millis();
              state.bypassTransition = true;
              state.transitionStart = millis();
              state.transitionDirection = false;
              state.displayDirty = true;
              state.needSaveGlobal = true;
            }
          }
          state.playWasPressed = false;
          b.processed = true;
        }
        else if (i == 3) {
          if (prev == LOW && b.lastStable == HIGH) {
            if (state.encLongPressActive) {
              state.encLongPressActive = false;
              state.encLongPressFrameVisible = false;
              state.displayDirty = true;
            }
          }
        }
        b.processed = false;
      } else if (prev == HIGH && b.lastStable == LOW) {
        // Нажатие
        if (b.processed) continue;

        if (i == 1) {
          if (tapWasReleased && (millis() - state.lastTapReleaseTime) < DOUBLE_CLICK_TIME && !state.tapDoubleClicked) {
            if (state.midiRunning) {
              state.sequencerRunning = !state.sequencerRunning;
              if (state.sequencerRunning) {
                state.sequencerPlayhead = 0;
                state.sequencerDisplayStep = 0;
                state.sequencerLastStepTick = state.midiTicks;
              } else {
                lastInternalTickMicros = 0;
                resetSequencerState();
              }
            } else {
              state.sequencerRunning = !state.sequencerRunning;
              if (state.sequencerRunning) {
                state.sequencerPlayhead = 0;
                state.sequencerDisplayStep = 0;
                state.sequencerLastStepTick = 0;
                lastInternalTickMicros = 0;
              } else {
                lastInternalTickMicros = 0;
                resetSequencerState();
              }
            }
            state.tapDoubleClicked = true;
            state.lastTapReleaseTime = 0;
            tapWasReleased = false;
            b.processed = true;
            continue;
          }

          if (buttons[3].lastStable == LOW && state.currentPage == Page::SEQUENCER && state.subPageSequencer == 0) {
            state.sequencerCursor = (state.sequencerCursor + 1) % state.sequencerSteps;
            b.processed = true;
            state.lastTapReleaseTime = 0;
            tapWasReleased = false;
            continue;
          }

          state.tapDoubleClicked = false;
          state.lastTapReleaseTime = 0;
          tapWasReleased = false;
          b.processed = true;
          continue;
        }

        if (i == 0) {
          state.playWasPressed = true;
          state.playPressTime = millis();
          // Перфоманс-функционал: вход в BYPASS/FREEZE происходит
          // сразу по нажатию, а не по отпусканию — чтобы моментально
          // "давить" эффект вживую. Если режим уже включён (залип с
          // прошлого нажатия), новое нажатие PLAY сразу его выключает
          // — это ДОПОЛНИТЕЛЬНЫЙ, мгновенный способ выйти, наряду с
          // уже существующим удержанием >0.75с + отпускание (см. ветку
          // отпускания ниже — она всё ещё актуальна для сценария
          // "нажал из OFF, держишь дольше 0.75с, отпустил"). TAP
          // держится зажатым отдельно для комбо-рандомайза, поэтому
          // пока TAP зажат, PLAY не трогает режим вовсе.
          if (buttons[1].lastStable == HIGH) {
            bool wasFreeze = (state.bypassMode == BypassMode::FREEZE);
            if (state.bypassMode == BypassMode::OFF) {
              state.bypassMode = state.selectedBypassMode;
              if (state.bypassMode == BypassMode::FREEZE) enterGlobalFreeze();
            } else {
              state.bypassMode = BypassMode::OFF;
              if (wasFreeze) exitGlobalFreeze();
            }
            state.iceEffectTime = millis();
            state.bypassTransition = true;
            state.transitionStart = millis();
            state.transitionDirection = (state.bypassMode == BypassMode::FREEZE);
            state.displayDirty = true;
            state.needSaveGlobal = true;
          }
          b.processed = true;
          continue;
        }
        else if (i == 2) {
          // Уход со страницы CC (или её подстраницы) — разумная точка
          // отмены зависшего ожидания MIDI Learn, чтобы не застрять
          // в нём при навигации в другое место.
          state.midiLearnActive = false;
          if (buttons[1].lastStable == LOW) {
            if (state.currentPage == Page::MAIN) state.subPageMain = (state.subPageMain + 1) % 2;
            else if (state.currentPage == Page::CC) state.subPageCC = (state.subPageCC + 1) % 2;
            else if (state.currentPage == Page::SEQUENCER) state.subPageSequencer = (state.subPageSequencer + 1) % 2;
            else if (state.currentPage == Page::STORM) state.subPageStorm = (state.subPageStorm + 1) % 2;
          } else {
            state.currentPage = (Page)(((uint8_t)state.currentPage + 1) % 4);
            state.subPageMain = 0;
            state.subPageCC = 0;
            state.subPageSequencer = 0;
            state.subPageStorm = 0;
          }
          state.displayDirty = true;
          b.processed = true;
        }
        else if (i == 3) {
          uint32_t now = millis();

          // Пока MIDI Learn ждёт данных — любое нажатие ENC_SW просто
          // отменяет ожидание, не запуская обычную double/triple-click
          // логику ниже.
          if (state.midiLearnActive) {
            state.midiLearnActive = false;
            state.displayDirty = true;
            b.processed = true;
            continue;
          }

          // Тройной клик на странице CC (подстраница CC-номеров) —
          // включает MIDI Learn для параметра под курсором. Отдельный
          // от double-click счётчик: тот сбрасывает encSWLastPress в 0
          // при срабатывании специально, чтобы не спутать третий клик
          // со вторым — здесь же нужен именно счёт подряд идущих кликов.
          if (now - state.encSWTripleWindowStart < DOUBLE_CLICK_TIME) {
            state.encSWTripleCount++;
          } else {
            state.encSWTripleCount = 1;
          }
          state.encSWTripleWindowStart = now;
          if (state.encSWTripleCount >= 3) {
            state.encSWTripleCount = 0;
            if (state.currentPage == Page::CC && state.subPageCC == 0) {
              state.midiLearnActive = true;
              state.midiLearnSequencerCC = false;
              state.midiLearnParam = state.selectedParam;
              state.displayDirty = true;
            } else if (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 1 &&
                       state.selectedParam == 2) {
              // Колонка CC на подстранице setup секвенсора.
              state.midiLearnActive = true;
              state.midiLearnSequencerCC = true;
              state.displayDirty = true;
            }
            b.processed = true;
            continue;
          }

          if (now - state.encSWLastPress < DOUBLE_CLICK_TIME) {
            state.encSWDoubleClicked = true;
            state.encSWLastPress = 0;
            bool handled = false;
            if ((state.currentPage == Page::MAIN && state.subPageMain == 0) ||
                (state.currentPage == Page::STORM && state.subPageStorm == 0)) {
              if (state.bypassMode == BypassMode::OFF || state.bypassMode == BypassMode::FREEZE) {
                // selectedParam — общий курсор для MAIN и STORM; на STORM
                // он указывает на AMT/WAV/WTH/ARM, а не на P1-4. Индексация
                // целей та же, что у SNOW/глобального FREEZE: 0-3=P1-4,
                // 4-7=AMT/WAV/WTH/ARM. В FREEZE это переключает конкретную
                // цель поверх уже инвертированного enterGlobalFreeze()
                // состояния — можно точечно разморозить/заморозить
                // параметр, не выходя из общего FREEZE; exitGlobalFreeze()
                // всё равно откатит всё к состоянию до входа в FREEZE.
                uint8_t target = (state.currentPage == Page::MAIN)
                  ? state.selectedParam
                  : (uint8_t)(state.selectedParam + NUM_PARAMS);
                setSnowTargetFrozen(target, !getSnowTargetFrozen(target));
                state.iceEffectTime = millis();
                state.displayDirty = true;
                handled = true;
              }
            }
            if (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 0) {
              uint8_t p = SEQ_COL_ORDER[state.selectedParam];
              state.steps[state.sequencerCursor].active[p] = !state.steps[state.sequencerCursor].active[p];
              state.displayDirty = true;
              handled = true;
            }
            if (handled) {
              b.processed = true;
              continue;
            }
          } else {
            state.encSWLastPress = now;
            state.encSWDoubleClicked = false;
          }

          if (!state.encSWDoubleClicked) {
            if (buttons[1].lastStable == LOW) {
              // FIX: selectedParam означает визуальную позицию колонки
              // ВЕЗДЕ и всегда (как на остальных страницах) — простой
              // +1 по кругу. Перевод в индекс данных для секвенсора
              // (см. SEQ_COL_ORDER) делается только там, где реально
              // читаются/пишутся данные шага, а не здесь.
              state.selectedParam = (state.selectedParam + 1) % NUM_PARAMS;
              b.processed = true;
              state.displayDirty = true;
              continue;
            } else {
              state.encLongPressActive = false;
              state.encLongPressFrameVisible = false;
              state.encLongPressTriggered = false;
              state.encLongPressActive = true;
              state.encLongPressStart = now;
              state.encLongPressTriggered = false;
              state.encLongPressFrameVisible = false;
              b.processed = true;
              continue;
            }
          }
        }
      }
    }
  }
  if (tapWasReleased) state.lastTapReleaseTime = tapReleaseTime;
}

void checkTapPageLongPress() {
  bool tapPressed = (buttons[1].lastStable == LOW);
  bool pagePressed = (buttons[2].lastStable == LOW);
  bool isSequencerMain = (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 0);
  if (tapPressed && pagePressed && isSequencerMain) {
    if (!state.tapPageLongPressActive) {
      state.tapPageLongPressActive = true;
      state.tapPageLongPressStart = millis();
      state.tapPageLongPressTriggered = false;
      state.tapPageLongPressFrameVisible = false;
    }
  } else {
    if (state.tapPageLongPressActive) {
      state.tapPageLongPressActive = false;
      state.tapPageLongPressFrameVisible = false;
      state.displayDirty = true;
    }
  }
}

void checkTapPlayLongPress() {
  bool tapPressed = (buttons[1].lastStable == LOW);
  bool playPressed = (buttons[0].lastStable == LOW);
  bool isSequencerMain = (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 0);
  if (tapPressed && playPressed && isSequencerMain) {
    if (!state.tapPlayLongPressActive) {
      state.tapPlayLongPressActive = true;
      state.tapPlayLongPressStart = millis();
      state.tapPlayLongPressTriggered = false;
      state.tapPlayLongPressFrameVisible = false;
    }
  } else {
    if (state.tapPlayLongPressActive) {
      state.tapPlayLongPressActive = false;
      state.tapPlayLongPressFrameVisible = false;
      state.displayDirty = true;
    }
  }
}

// ============================================================
// 10. ОБРАБОТКА ЭНКОДЕРА
// ============================================================
void handleEncoder() {
  int mov = 0;
  if (abs(encoderTicks) >= 4) {
    mov = (encoderTicks > 0) ? 1 : -1;
    encoderTicks = 0;
    state.displayDirty = true;
  }
  if (mov == 0) return;

  bool tapPressed = (buttons[1].lastStable == LOW);

  // SEQUENCER main
  if (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 0) {
    int stepSize = tapPressed ? 12 : 1;
    mov *= stepSize;
    uint8_t p = SEQ_COL_ORDER[state.selectedParam]; // визуальная позиция -> индекс данных
    switch (p) {
      case 0: { int v = (int)state.steps[state.sequencerCursor].note + mov; state.steps[state.sequencerCursor].note = clampU8(v, 0, 127); break; }
      case 1: { int v = (int)state.steps[state.sequencerCursor].cc + mov; state.steps[state.sequencerCursor].cc = clampU8(v, 0, 127); break; }
      case 2: {
        int v = (int)state.steps[state.sequencerCursor].wavesIndex + mov;
        switch (state.seqDest) {
          case SeqDest::WAV: v = clampU8(v, 0, 4); break;
          case SeqDest::AMT: v = clampU8(v, 0, 127); break;
          case SeqDest::WTH: v = clampU8(v, 0, 3); break;
        }
        state.steps[state.sequencerCursor].wavesIndex = (uint8_t)v;
        break;
      }
      case 3: { int idx = (int)state.steps[state.sequencerCursor].retrigIndex + mov; state.steps[state.sequencerCursor].retrigIndex = clampU8(idx, 0, 4); break; }
    }
    state.displayDirty = true;
    return;
  }

  // SEQUENCER setup
  if (state.currentPage == Page::SEQUENCER && state.subPageSequencer == 1) {
    int stepSize = tapPressed ? 12 : 1;
    mov *= stepSize;
    switch (state.selectedParam) {
      // BPM редактируется вручную, только когда секвенсор не идёт от
      // внешнего MIDI-клока.
      case 0: if (!state.midiRunning) { int v = (int)state.sequencerBPM + mov; state.sequencerBPM = clampInt(v, 1, 300); } break;
      case 1: { int v = (int)state.sequencerSteps + mov; state.sequencerSteps = clampU8(v, 1, 16); if (state.sequencerCursor >= state.sequencerSteps) state.sequencerCursor = state.sequencerSteps - 1; if (state.sequencerPlayhead >= state.sequencerSteps) state.sequencerPlayhead = 0; break; }
      case 2: { if (state.midiLearnActive) break; int v = (int)state.sequencerCC + mov; state.sequencerCC = clampU8(v, 0, 127); break; }
      case 3: { int idx = (int)state.sequencerScaleIndex + mov; state.sequencerScaleIndex = clampU8(idx, 0, 3); break; }
    }
    state.displayDirty = true;
    return;
  }

  // STORM subpage (расширена для SUN)
  if (state.currentPage == Page::STORM && state.subPageStorm == 1) {
    if (state.weatherMode == WeatherMode::FOG) {
      // LFO параметры
      int stepSize = tapPressed ? 4 : 1;
      mov *= stepSize;
      switch (state.selectedParam) {
        case 0: { int v = (int)state.lfoType + mov; state.lfoType = clampU8(v, 0, 2); break; }
        case 1: { int v = (int)state.lfoShape + mov; state.lfoShape = clampInt(v, -128, 127); break; }
        case 2: { int v = (int)state.lfoPhase + mov; state.lfoPhase = clampInt(v, -128, 127); break; }
        case 3: { int v = (int)state.lfoGlide + mov; state.lfoGlide = clampU8(v, 0, 127); break; }
      }
    } else if (state.weatherMode == WeatherMode::SUN) {
      // Параметры SUN
      int stepSize = tapPressed ? 4 : 1;
      mov *= stepSize;
      switch (state.selectedParam) {
        case 0: { int v = (int)state.sunRflct + mov; state.sunRflct = clampU8(v, 0, 4); break; }
        case 1: { int v = (int)state.sunArp + mov; state.sunArp = clampU8(v, 0, 4); break; }
        case 2: { int v = (int)state.sunDflct + mov; state.sunDflct = clampU8(v, 0, 127); break; }
        case 3: { int v = (int)state.sunBias + mov; state.sunBias = clampInt(v, -63, 64); break; }
      }
    } else if (state.weatherMode == WeatherMode::SNOW) {
      // Параметры SNOW
      int stepSize = tapPressed ? 4 : 1;
      mov *= stepSize;
      switch (state.selectedParam) {
        case 0: { int v = (int)state.snowFlake + mov; state.snowFlake = clampU8(v, 3, 12); break; }
        case 1: { int v = (int)state.snowRotation + mov; state.snowRotation = clampU8(v, 0, 11); break; }
        case 2: { int v = (int)state.snowFrz + mov; state.snowFrz = clampU8(v, 0, 127); break; }
        case 3: { int v = (int)state.snowTime + mov; state.snowTime = clampInt(v, -63, 64); break; }
      }
    } else if (state.weatherMode == WeatherMode::RAIN) {
      // Параметры RAIN
      int stepSize = tapPressed ? 4 : 1;
      mov *= stepSize;
      switch (state.selectedParam) {
        case 0: { int v = (int)state.rainDrip + mov; state.rainDrip = clampU8(v, 0, 127); break; }
        case 1: { int v = (int)state.rainWet + mov; state.rainWet = clampU8(v, 0, 127); break; }
        case 2: { int v = (int)state.rainSplsh + mov; state.rainSplsh = clampU8(v, 0, 127); break; }
        case 3: { if (mov != 0) state.rainThunder = !state.rainThunder; break; }
      }
    } else {
      return;
    }
    state.needSaveStorm = true;
    state.displayDirty = true;
    return;
  }

  // CC subpage
  if (state.currentPage == Page::CC && state.subPageCC == 1) {
    int stepSize = tapPressed ? 4 : 1;
    mov *= stepSize;
    switch (state.selectedParam) {
      case 0: {
        int v = (int)state.midiChannel + mov;
        state.midiChannel = clampU8(v, 1, 16);
        state.needSaveGlobal = true;
        break;
      }
      case 1: {
        if (mov != 0) {
          // OFF не выбирается — это только BYPASS или FREEZE.
          state.selectedBypassMode = (state.selectedBypassMode == BypassMode::FREEZE)
            ? BypassMode::BYPASS : BypassMode::FREEZE;
          state.needSaveGlobal = true;
          state.displayDirty = true;
        }
        break;
      }
      case 2: {
        int v = (int)state.seqDest + mov;
        if (v < 0) v = 2;
        if (v > 2) v = 0;
        state.seqDest = (SeqDest)v;
        state.needSaveGlobal = true;
        break;
      }
      case 3: {
        if (mov != 0) {
          state.gfxEnabled = !state.gfxEnabled;
          state.needSaveGlobal = true;
        }
        break;
      }
    }
    state.displayDirty = true;
    return;
  }

  // MAIN / CC subpages (MAIN sub 1 – min/max)
  int step = (tapPressed && !(state.currentPage == Page::MAIN && state.subPageMain == 1)) ? 12 : 1;
  mov *= step;

  if (state.currentPage == Page::MAIN && state.subPageMain == 1) {
    if (tapPressed) {
      int v = state.params[state.selectedParam].max + mov;
      state.params[state.selectedParam].max = clampU8(v, state.params[state.selectedParam].min, 127);
    } else {
      int v = state.params[state.selectedParam].min + mov;
      state.params[state.selectedParam].min = clampU8(v, 0, state.params[state.selectedParam].max);
    }
    state.needSaveMinMax = true;
    state.displayDirty = true;
    return;
  }

  // Основные страницы
  switch (state.currentPage) {
    case Page::MAIN: {
      if (state.frozen[state.selectedParam]) break;
      int v = state.params[state.selectedParam].value + mov;
      state.params[state.selectedParam].value = clampU8(v, state.params[state.selectedParam].min, state.params[state.selectedParam].max);
      state.params[state.selectedParam].baseValue = state.params[state.selectedParam].value;
      sendCC(state.params[state.selectedParam].cc, state.params[state.selectedParam].value);
      state.displayDirty = true;
      break;
    }
    case Page::CC: {
      if (state.midiLearnActive) break; // ждём входящий CC — ручное редактирование не мешаем
      int v = (int)state.params[state.selectedParam].cc + mov;
      v = clampU8(v, 0, 127);
      state.params[state.selectedParam].cc = (uint8_t)v;
      state.needSaveCC = true;
      state.displayDirty = true;
      break;
    }
    case Page::STORM: {
      // Собственные флаги заморозки для AMT/WAV/WTH/ARM — не путать с
      // state.frozen[] (P1-4 на MAIN), т.к. selectedParam общий курсор.
      bool blocked = false;
      switch (state.selectedParam) {
        case 0: blocked = state.frozenAmt; break;
        case 1: blocked = state.frozenWav; break;
        case 2: blocked = state.frozenWth; break;
        case 3: blocked = state.frozenArm; break;
      }
      if (blocked) break;
      switch (state.selectedParam) {
        case 0: { int v = state.chaos + mov * 2; state.chaos = clampU8(v, 0, 100); break; }
        case 1: { int idx = (int)state.waveIntervalIndex + mov; state.waveIntervalIndex = clampU8(idx, 0, 4); break; }
        case 2: { int m = (int)state.weatherMode + mov; if (m < 0) m = 0; if (m > 3) m = 3; state.weatherMode = (WeatherMode)m; break; }
        case 3: { if (mov != 0) state.randomizerEnabled = !state.randomizerEnabled; break; }
      }
      state.needSaveStorm = true;
      state.displayDirty = true;
      break;
    }
    default: break;
  }
}

// ============================================================
// 11. ЛОГИКА СТРАНИЦ
// ============================================================
void randomizeCurrentPage() {
  switch (state.currentPage) {
    case Page::MAIN: {
      for (uint8_t i = 0; i < NUM_PARAMS; i++) {
        int v = random(state.params[i].min, state.params[i].max + 1);
        state.params[i].value = (uint8_t)v;
        state.params[i].baseValue = (uint8_t)v;
        sendCC(state.params[i].cc, state.params[i].value);
      }
      state.displayDirty = true;
      break;
    }
    case Page::CC: {
      if (state.subPageCC == 0) {
        for (uint8_t i = 0; i < NUM_PARAMS; i++) {
          state.params[i].cc = random(0, 128);
          state.needSaveCC = true;
        }
      } else {
        state.midiChannel = random(1, 17);
        state.selectedBypassMode = (random(0, 2) == 0) ? BypassMode::BYPASS : BypassMode::FREEZE;
        state.seqDest = (SeqDest)random(0, 3);
        state.gfxEnabled = random(0, 2);
        state.needSaveGlobal = true;
      }
      state.displayDirty = true;
      break;
    }
    case Page::STORM: {
      if (state.subPageStorm == 1) {
        if (state.weatherMode == WeatherMode::FOG) {
          state.lfoType = random(0, 3);
          state.lfoShape = random(-128, 128);
          state.lfoPhase = random(-128, 128);
          state.lfoGlide = random(0, 128);
        } else if (state.weatherMode == WeatherMode::SUN) {
          state.sunRflct = random(0, 5);  // 0..4
          state.sunArp = random(0, 5);    // 0..4 (OFF/Same/5th/Oct/2Oct)
          state.sunDflct = random(0, 128);
          state.sunBias = random(-63, 65);
        } else if (state.weatherMode == WeatherMode::SNOW) {
          state.snowFlake = random(3, 13);
          state.snowRotation = random(0, 12);
          state.snowFrz = random(0, 128);
          state.snowTime = random(-63, 65);
        } else if (state.weatherMode == WeatherMode::RAIN) {
          state.rainDrip = random(0, 128);
          state.rainWet = random(0, 128);
          state.rainSplsh = random(0, 128);
          state.rainThunder = random(0, 2);
        }
        state.needSaveStorm = true;
      } else {
        if (!state.frozenAmt) state.chaos = random(0, 101);
        if (!state.frozenWav) state.waveIntervalIndex = random(0, 5);
        if (!state.frozenWth) state.weatherMode = (WeatherMode)random(0, 4);
        state.needSaveStorm = true;
      }
      state.displayDirty = true;
      break;
    }
    case Page::SEQUENCER: {
      if (state.subPageSequencer == 0) {
        uint8_t i = state.sequencerCursor;
        for (uint8_t p = 0; p < 4; p++) state.steps[i].active[p] = random(0, 2);
        state.steps[i].note = random(0, 128);
        state.steps[i].cc = random(0, 128);
        switch (state.seqDest) {
          case SeqDest::WAV: state.steps[i].wavesIndex = random(0, 5); break;
          case SeqDest::AMT: state.steps[i].wavesIndex = random(0, 128); break;
          case SeqDest::WTH: state.steps[i].wavesIndex = random(0, 4); break;
        }
        state.steps[i].retrigIndex = random(0, 5);
      } else {
        state.sequencerBPM = random(1, 301);
        state.sequencerSteps = random(1, 17);
        state.sequencerCC = random(0, 128);
        state.sequencerScaleIndex = random(0, 4);
        if (state.sequencerCursor >= state.sequencerSteps) state.sequencerCursor = state.sequencerSteps - 1;
        if (state.sequencerPlayhead >= state.sequencerSteps) state.sequencerPlayhead = 0;
        saveSequencerSettings();
      }
      state.displayDirty = true;
      break;
    }
    default: break;
  }
}

void randomizeSequencerAll() {
  for (uint8_t step = 0; step < SEQUENCER_STEPS; step++) {
    for (uint8_t p = 0; p < 4; p++) state.steps[step].active[p] = random(0, 2);
    state.steps[step].note = random(0, 128);
    state.steps[step].cc = random(0, 128);
    switch (state.seqDest) {
      case SeqDest::WAV: state.steps[step].wavesIndex = random(0, 5); break;
      case SeqDest::AMT: state.steps[step].wavesIndex = random(0, 128); break;
      case SeqDest::WTH: state.steps[step].wavesIndex = random(0, 4); break;
    }
    state.steps[step].retrigIndex = random(0, 5);
  }
  state.retrigLastTickGlobal = 0;
  for (uint8_t p = 0; p < 4; p++) {
    state.retriggerActive[p] = false;
    state.lastActive[p] = false;
    state.retriggerTime[p] = 0;
    state.triggerTime[p] = 0;
  }
  state.lastNote = 72;
  state.lastCC = 20;
  state.lastWavesIndex = 2;
  state.displayDirty = true;
  saveSequencerSettings();
}

void resetToDefaults() {
  Page page = state.currentPage;
  uint8_t sub = 0;
  switch (page) {
    case Page::MAIN:
      sub = state.subPageMain;
      if (sub == 0) {
        for (uint8_t i = 0; i < NUM_PARAMS; i++) {
          state.params[i].value = 0;
          state.params[i].baseValue = 0;
          state.params[i].min = 0;
          state.params[i].max = 127;
          state.frozen[i] = false;
        }
        state.frozenActive = false;
        state.needSaveMinMax = true;
      } else if (sub == 1) {
        for (uint8_t i = 0; i < NUM_PARAMS; i++) {
          state.params[i].min = 0;
          state.params[i].max = 127;
        }
        state.needSaveMinMax = true;
      }
      break;
    case Page::CC:
      sub = state.subPageCC;
      if (sub == 0) {
        for (uint8_t i = 0; i < NUM_PARAMS; i++) state.params[i].cc = i;
        state.needSaveCC = true;
      } else if (sub == 1) {
        state.midiChannel = 1;
        if (state.bypassMode == BypassMode::FREEZE) exitGlobalFreeze();
        state.bypassMode = BypassMode::OFF;
        state.selectedBypassMode = BypassMode::FREEZE;
        state.seqDest = SeqDest::WAV;
        state.gfxEnabled = true;
        state.needSaveGlobal = true;
        state.iceEffectTime = millis();
      }
      break;
    case Page::STORM:
      sub = state.subPageStorm;
      if (sub == 0) {
        state.chaos = 0;
        state.waveIntervalIndex = 2;
        state.weatherMode = WeatherMode::SUN;
        state.randomizerEnabled = true;
        state.needSaveStorm = true;
      } else if (sub == 1) {
        if (state.weatherMode == WeatherMode::FOG) {
          state.lfoType = 0;
          state.lfoShape = 0;
          state.lfoPhase = 0;
          state.lfoGlide = 0;
          state.lfoPhaseAccum = 0.0f;
          state.lfoCurrentValue = 0.0f;
          state.lfoTargetValue = 0.0f;
        } else if (state.weatherMode == WeatherMode::SUN) {
          state.sunRflct = 0;
          state.sunArp = 0;
          state.sunDflct = 0;
          state.sunBias = 0;
          for (uint8_t i = 0; i < NUM_PARAMS; i++) {
            state.sunReflectLevel[i] = 0;
            stopSunArp(i);
          }
        } else if (state.weatherMode == WeatherMode::SNOW) {
          state.snowFlake = 8;
          state.snowRotation = 0;
          state.snowFrz = 0;
          state.snowTime = 0;
          // Гасим все заморозки, ещё активные из-за SNOW.
          for (uint8_t t = 0; t < SNOW_FREEZE_TARGETS; t++) {
            if (state.snowFreezeActive[t]) {
              setSnowTargetFrozen(t, false);
              state.snowFreezeActive[t] = false;
            }
          }
        } else if (state.weatherMode == WeatherMode::RAIN) {
          state.rainDrip = 0;
          state.rainWet = 114;
          state.rainSplsh = 0;
          state.rainThunder = false;
          for (uint8_t i = 0; i < NUM_PARAMS; i++) state.rainNextTick[i] = 0;
          state.rainStrikeActive = false;
        }
        state.needSaveStorm = true;
      }
      break;
    case Page::SEQUENCER:
      sub = state.subPageSequencer;
      if (sub == 0) {
        for (uint8_t step = 0; step < SEQUENCER_STEPS; step++) {
          for (uint8_t p = 0; p < 4; p++) state.steps[step].active[p] = false;
          state.steps[step].note = 72;
          state.steps[step].cc = 00;
          state.steps[step].wavesIndex = 2;
          state.steps[step].retrigIndex = 0;
        }
        state.retrigLastTickGlobal = 0;
        for (uint8_t p = 0; p < 4; p++) {
          state.retriggerActive[p] = false;
          state.lastActive[p] = false;
          state.retriggerTime[p] = 0;
          state.triggerTime[p] = 0;
        }
        state.lastNote = 72;
        state.lastCC = 00;
        state.lastWavesIndex = 2;
        saveSequencerSettings();
      } else if (sub == 1) {
        state.sequencerSteps = 16;
        state.sequencerScaleIndex = 0;
        state.sequencerCC = 00;
        state.sequencerBPM = 120;
        state.sequencerCursor = 0;
        state.sequencerPlayhead = 0;
        state.sequencerRunning = false;
        state.sequencerLastStepTick = 0;
        lastInternalTickMicros = 0;
        saveSequencerSettings();
      }
      break;
    default: break;
  }
  state.displayDirty = true;
  state.iceEffectTime = millis();
}

void resetSequencerState() {
  state.stepFlashActive = false;
  state.stepFlashTime = 0;
  state.retrigLastTickGlobal = 0;
  for (uint8_t i = 0; i < 4; i++) {
    state.retriggerTime[i] = 0;
    state.lastActive[i] = false;
    state.retriggerActive[i] = false;
  }
  state.lastNote = 72;
  state.lastCC = 00;
  state.lastWavesIndex = 2;
  state.sequencerPlayhead = 0;
  state.sequencerDisplayStep = 0;
  state.sequencerLastStepTick = 0;
  lastInternalTickMicros = 0;
  state.displayDirty = true;
}

// ============================================================
// 12. ОТРИСОВКА
// ============================================================
void drawTextWithOutline(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t outlineColor) {
  display.setCursor(x-1, y); display.setTextColor(outlineColor); display.print(text);
  display.setCursor(x+1, y); display.print(text);
  display.setCursor(x, y-1); display.print(text);
  display.setCursor(x, y+1); display.print(text);
  display.setCursor(x, y); display.setTextColor(color); display.print(text);
}

void drawDashedRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  for (uint8_t i=0; i<w; i+=2) { display.drawPixel(x+i, y, color); display.drawPixel(x+i, y+h-1, color); }
  for (uint8_t i=0; i<h; i+=2) { display.drawPixel(x, y+i, color); display.drawPixel(x+w-1, y+i, color); }
}

void drawDitherFill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t fillPct, uint32_t time, uint8_t amt, bool invert=false) {
  if (fillPct==0 || fillPct>100) return;
  uint8_t rows = constrain((fillPct * h) / 100, 1, h);
  uint8_t startY = y + h - rows;
  float noiseAmt;
  if (amt < 30) noiseAmt = (amt / 30.0f) * 0.2f;
  else if (amt < 70) noiseAmt = 0.2f + (amt - 30) / 40.0f * 0.5f;
  else noiseAmt = 0.7f + (amt - 70) / 30.0f * 0.3f;
  uint16_t speed = constrain(150 - amt * 1.3f, 10, 150);
  uint8_t offset = (uint8_t)((time / speed) % 64);
  for (uint8_t dy=0; dy<rows; dy++) {
    uint8_t py = startY + dy;
    if (py >= y+h) break;
    for (uint8_t dx=0; dx<w; dx++) {
      uint8_t px = x + dx;
      if (px >= x+w) break;
      uint8_t noise = getNoise(px, py + time/100, time);
      uint8_t pat = bayer[(dy + offset) & 7][(dx + offset/4) & 7];
      bool white = false;
      if (amt == 0) white = true;
      else if (amt == 100) white = (noise < 128);
      else {
        float density = fillPct / 100.0f;
        if (pat < (uint8_t)(56 * (1.0f - noiseAmt * 0.5f))) white = true;
        else if (noise < (uint8_t)(128 * (1.0f - noiseAmt * density))) white = true;
      }
      if (white) display.drawPixel(px, py, invert ? SSD1306_BLACK : SSD1306_WHITE);
    }
  }
}

void drawCornerCursor(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  for (uint8_t i=0; i<3; i++) {
    if (x+i < Display::W && y < Display::H) display.drawPixel(x+i, y, color);
    if (x < Display::W && y+i < Display::H) display.drawPixel(x, y+i, color);
    if (x+w-1-i < Display::W && y < Display::H) display.drawPixel(x+w-1-i, y, color);
    if (x+w-1 < Display::W && y+i < Display::H) display.drawPixel(x+w-1, y+i, color);
    if (x+i < Display::W && y+h-1 < Display::H) display.drawPixel(x+i, y+h-1, color);
    if (x < Display::W && y+h-1-i < Display::H) display.drawPixel(x, y+h-1-i, color);
    if (x+w-1-i < Display::W && y+h-1 < Display::H) display.drawPixel(x+w-1-i, y+h-1, color);
    if (x+w-1 < Display::W && y+h-1-i < Display::H) display.drawPixel(x+w-1, y+h-1-i, color);
  }
}

void drawWaveform(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t type, int8_t shape, int8_t phase, uint32_t time, uint16_t color) {
  float phaseOffset = (phase / 127.0f) * 2.0f * 3.14159f;
  float ampScale = 0.7f;
  float prevVal = 0;
  for (uint8_t px=0; px<w; px++) {
    float t = (float)px / w;
    float p = t * 2.0f * 3.14159f + phaseOffset;
    float val = computeLFO(p, type, shape, 0) * ampScale;
    int py = y + h/2 - (int)(val * (h/2 - 1));
    if (py < y) py = y;
    if (py >= y+h) py = y+h-1;
    if (px == 0) display.drawPixel(x+px, py, color);
    else {
      int prevPy = y + h/2 - (int)(prevVal * (h/2 - 1));
      if (prevPy < y) prevPy = y;
      if (prevPy >= y+h) prevPy = y+h-1;
      int minY = min(prevPy, py), maxY = max(prevPy, py);
      for (int lineY = minY; lineY <= maxY; lineY++) {
        if (lineY >= y && lineY < y+h) display.drawPixel(x+px, lineY, color);
      }
    }
    prevVal = val;
  }
}

// ----- Погодные эффекты -----
void drawSeaLines(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  uint8_t yTop = 7 + (int8_t)(sin(time / 1200.0f) * 1.5);
  uint8_t yBottom = 17 + (int8_t)(cos(time / 1400.0f) * 1.5);
  for (uint8_t x=0; x<Display::W; x++) {
    float fade = 1.0;
    if (x < 40) fade = (float)x / 40.0;
    else if (x > 88) fade = (float)(Display::W - x) / 40.0;
    if (fade < 0) fade = 0;
    int8_t waveOffset = (int8_t)(sin(x * 0.15f + time / 900.0f) * 1.2);
    uint8_t y1 = yTop + waveOffset;
    uint8_t y2 = yBottom + waveOffset;
    uint8_t patTop = bayer[(y1 + time/60) & 7][(x + time/80) & 7];
    uint8_t patBottom = bayer[(y2 + time/70) & 7][(x + time/90) & 7];
    uint8_t threshold = (uint8_t)(30 + amt / 4 * fade);
    if (patTop < threshold && y1 < Display::H) display.drawPixel(x, y1, color);
    if (patBottom < threshold && y2 < Display::H) display.drawPixel(x, y2, color);
  }
}

void drawFog(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  uint8_t base = 12 + amt / 6;
  uint32_t slow = time / 4;
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t layer=0; layer<1; layer++) {
    float sp = 0.4f + layer * 0.35f;
    int8_t ox = (int8_t)(sin(slow / 3500.0f * sp + layer * 2) * (2 + layer * 2));
    int8_t oy = (int8_t)(cos(slow / 4500.0f * sp + layer * 1.5) * (1 + layer));
    uint8_t dens = base + layer * 6;
    for (uint8_t dy=0; dy<16; dy++) {
      for (uint8_t dx=0; dx<Display::W; dx++) {
        uint8_t px = dx + ox, py = dy + oy + layer * 2 + 8;
        uint32_t h = px * 374761393 + py * 668265263 + slow / (250 + layer * 120) * 1274126177;
        h = (h ^ (h >> 13)) * 1274126177;
        if (((h ^ (h >> 16)) & 0xFF) < dens + layer * 8) {
          uint8_t pat = bayer[(py + slow / 250) & 7][(px + slow / 350) & 7];
          if (pat < 55) display.drawPixel(dx, dy, color);
        }
      }
    }
  }
}

void drawSun(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  uint8_t sx=82, sy=3;
  uint8_t pulse = (uint8_t)(sin(time / 3000.0f) * 1 + 2);
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t i=0; i<6; i++) {
    float ang = (float)i / 6 * 2 * 3.14159f + time / 6000.0f;
    uint8_t len = 1 + pulse / 4;
    for (uint8_t j=0; j<len; j++) {
      uint8_t rx = sx + cos(ang) * len * j / len;
      uint8_t ry = sy + sin(ang) * len * j / len;
      uint8_t pat = bayer[(ry + time/300) & 7][(rx + time/400) & 7];
      if (pat < 15 + amt/15 && rx < Display::W && ry < Display::H) display.drawPixel(rx, ry, color);
    }
  }
  for (int8_t dy=-2; dy<=2; dy++) {
    for (int8_t dx=-2; dx<=2; dx++) {
      if (dx*dx + dy*dy <= 4) {
        uint8_t px = sx+dx, py = sy+dy;
        uint8_t pat = bayer[(py + time/200) & 7][(px + time/250) & 7];
        if (pat < 40 + amt/12 && px < Display::W && py < Display::H) display.drawPixel(px, py, color);
      }
    }
  }
}

// Молния: ломаная линия сверху экрана вниз, координаты берутся
// заново на каждый вызов — за ~120мс вспышки (rainStrikeActive)
// пара перерисовок даёт эффект мерцающего разряда, а не статичную
// картинку.
void drawLightning(bool bypass) {
  if (!state.rainThunder || !state.rainStrikeActive) return;
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  int16_t x = 20 + random(0, Display::W - 40);
  int16_t y = 0;
  for (uint8_t i = 0; i < 4; i++) {
    int16_t nx = constrain((int16_t)(x + random(-4, 5)), (int16_t)0, (int16_t)(Display::W - 1));
    int16_t ny = y + 4;
    display.drawLine(x, y, nx, ny, color);
    x = nx; y = ny;
  }
}

void drawRain(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  uint8_t num = 10 + amt / 10;
  uint32_t fast = (time & 0x1FFFFFFF) * 8;
  uint32_t seed = fast / 50 + 12345;
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t i=0; i<num; i++) {
    uint8_t r1 = (seed + i*31) & 0x0F, r2 = (seed + i*53) & 0x07;
    uint8_t sp = 10 + amt/5 + (r1 & 0x03);
    uint8_t px = i * (Display::W / num) + (fast/50 + i*7 + r1) % (Display::W / num);
    uint8_t py = (fast / 10 * sp / 3 + i*17 + r2) % 18;
    uint8_t len = 4 + (i % 4) + (r1 & 0x01);
    for (uint8_t j=0; j<len; j++) {
      uint8_t py2 = (py + j*2 + i*3) % 18;
      if (py2 < 16 && px < Display::W) {
        uint8_t pat = bayer[(py2 + fast/15) & 7][(px + fast/25) & 7];
        if (pat < 70 + amt/6) display.drawPixel(px, py2, color);
      }
    }
  }
  if (amt > 20) {
    uint8_t splash = 3 + amt / 20;
    for (uint8_t i=0; i<splash; i++) {
      uint8_t sx = (fast / 30 + i*23) % Display::W;
      uint8_t pat = bayer[(15 + fast/25) & 7][(sx + fast/35) & 7];
      if (pat < 45 + amt/8 && sx < Display::W) {
        display.drawPixel(sx, 15, color);
        if (pat < 30 && sx+1 < Display::W) display.drawPixel(sx+1, 15, color);
      }
    }
  }
  drawLightning(bypass);
}

void drawSnow(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  // AMT ускоряет падение и увеличивает количество частиц.
  uint8_t num = 6 + amt / 7;
  uint32_t fall = (time * (25 + amt)) / 25;
  uint32_t seed = fall / 60 + 54321;
  for (uint8_t i=0; i<num; i++) {
    uint8_t r1 = (seed + i*29) & 0x0F, r2 = (seed + i*47) & 0x07;
    uint8_t fallSpeed = 6 + (r1 & 0x03);
    uint8_t px0 = i * (Display::W / num) + (r1 * 5) % (Display::W / num);
    int8_t drift = (int8_t)(sin((fall / 500.0f) + i * 1.3f) * 2.5f);
    uint8_t px = (px0 + drift + Display::W) % Display::W;
    uint8_t py = ((fall / fallSpeed) + i*13 + r2) % 18;
    uint8_t pat = bayer[(py + fall/80) & 7][(px + fall/90) & 7];
    if (pat < 55 + amt/4) {
      display.drawPixel(px, py, color);
      if (amt > 60 && pat < 15) display.drawPixel(px+1, py, color);
    }
  }
}

void drawWeather(uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  switch (state.weatherMode) {
    case WeatherMode::FOG:  drawFog(time, amt, bypass); break;
    case WeatherMode::SUN:  drawSun(time, amt, bypass); break;
    case WeatherMode::RAIN: drawRain(time, amt, bypass); break;
    case WeatherMode::SNOW: drawSnow(time, amt, bypass); break;
  }
}

// ----- Колонки -----
void drawCCColumn(uint8_t x, uint8_t val, bool selected, uint32_t time, bool bypass) {
  const uint8_t cw=Display::COL_W, my=Display::METER_Y, mh=Display::METER_H;
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  if (g_fillBackground) {
    uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
    display.fillRect(x, my, cw, mh, bg);
  }
  if (selected) drawCornerCursor(x, my, cw, mh, fg);
  // Без паддинга — getTextBounds должен мерить реально видимую ширину,
  // иначе центрирование съезжает.
  char txt[4]; sprintf(txt, "%d", val);
  display.setTextSize(1); display.setTextColor(fg);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + (cw-w)/2, my + (mh-h)/2 + 1);
  display.print(txt);
}

// isFrozen передаётся явно вызывающим — на STORM индекс колонки не
// совпадает с индексом в state.frozen[] (P1-4, только на MAIN).
void drawColumn(uint8_t x, uint8_t val, bool selected, uint32_t time, uint8_t amt, bool bypass, bool isFrozen) {
  const uint8_t cw=Display::COL_W, my=Display::METER_Y, mh=Display::METER_H;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  if (g_fillBackground) display.fillRect(x, my, cw, mh, bg);
  uint8_t fillPct = map(val, 0, 127, 0, 100);
  if (fillPct > 0) drawDitherFill(x+1, my+1, cw-2, mh-2, fillPct, time, amt, bypass);
  if (selected) drawCornerCursor(x, my, cw, mh, fg);
  char txt[4]; sprintf(txt, "%d", val);
  display.setTextSize(1);
  bool inv = fillPct > 50;
  uint16_t textColor = inv ? bg : fg;
  uint16_t outlineColor = inv ? fg : bg;
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  drawTextWithOutline(x + (cw-w)/2, my + (mh-h)/2 + 1, txt, textColor, outlineColor);
  if (isFrozen) {
    uint32_t elapsed = millis() - state.iceEffectTime;
    uint8_t iceIntensity = (elapsed < 1000) ? map(elapsed % 200, 0, 200, 200, 50) : 200;
    uint16_t iceColor = fg;
    for (uint8_t dy=0; dy<mh; dy++) {
      for (uint8_t dx=0; dx<cw; dx++) {
        if (dx==0 || dx==cw-1 || dy==0 || dy==mh-1) {
          uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
          if (pat < (uint8_t)(iceIntensity / 4)) display.drawPixel(x+dx, my+dy, iceColor);
        }
      }
    }
  }
}

void drawColumnText(uint8_t x, uint8_t val, const char* text, bool selected, uint32_t time, uint8_t amt, bool bypass, bool isFrozen) {
  const uint8_t cw=Display::COL_W, my=Display::METER_Y, mh=Display::METER_H;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  if (g_fillBackground) display.fillRect(x, my, cw, mh, bg);
  uint8_t fillPct = map(val, 0, 127, 0, 100);
  if (fillPct > 0) drawDitherFill(x+1, my+1, cw-2, mh-2, fillPct, time, amt, bypass);
  if (selected) drawCornerCursor(x, my, cw, mh, fg);
  display.setTextSize(1);
  bool inv = fillPct > 50;
  uint16_t textColor = inv ? bg : fg;
  uint16_t outlineColor = inv ? fg : bg;
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  drawTextWithOutline(x + (cw-w)/2, my + (mh-h)/2 + 1, text, textColor, outlineColor);
  if (isFrozen) {
    uint32_t elapsed = millis() - state.iceEffectTime;
    uint8_t iceIntensity = (elapsed < 1000) ? map(elapsed % 200, 0, 200, 200, 50) : 200;
    uint16_t iceColor = fg;
    for (uint8_t dy=0; dy<mh; dy++) {
      for (uint8_t dx=0; dx<cw; dx++) {
        if (dx==0 || dx==cw-1 || dy==0 || dy==mh-1) {
          uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
          if (pat < (uint8_t)(iceIntensity / 4)) display.drawPixel(x+dx, my+dy, iceColor);
        }
      }
    }
  }
}

void drawColumnMode(uint8_t x, const char* text, bool selected, uint32_t time, bool bypass, bool isFrozen) {
  const uint8_t cw=Display::COL_W, my=Display::METER_Y, mh=Display::METER_H;
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  if (selected) drawCornerCursor(x, my, cw, mh, fg);
  display.setTextSize(1); display.setTextColor(fg);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + (cw-w)/2, my + (mh-h)/2 + 1);
  display.print(text);
  if (isFrozen) {
    uint32_t elapsed = millis() - state.iceEffectTime;
    uint8_t iceIntensity = (elapsed < 1000) ? map(elapsed % 200, 0, 200, 200, 50) : 200;
    uint16_t iceColor = fg;
    for (uint8_t dy=0; dy<mh; dy++) {
      for (uint8_t dx=0; dx<cw; dx++) {
        if (dx==0 || dx==cw-1 || dy==0 || dy==mh-1) {
          uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
          if (pat < (uint8_t)(iceIntensity / 4)) display.drawPixel(x+dx, my+dy, iceColor);
        }
      }
    }
  }
}

void drawColumnYesNo(uint8_t x, bool stateVal, bool selected, uint32_t time, uint8_t amt, bool bypass, bool isFrozen) {
  const uint8_t cw=Display::COL_W, my=Display::METER_Y, mh=Display::METER_H;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  if (g_fillBackground) display.fillRect(x, my, cw, mh, bg);
  if (stateVal) drawDitherFill(x+1, my+1, cw-2, mh-2, 100, time, amt, bypass);
  if (selected) drawCornerCursor(x, my, cw, mh, fg);
  const char* txt = stateVal ? "YES" : "NO";
  display.setTextSize(1);
  uint16_t textColor = stateVal ? bg : fg;
  uint16_t outlineColor = stateVal ? fg : bg;
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  drawTextWithOutline(x + (cw-w)/2, my + (mh-h)/2 + 1, txt, textColor, outlineColor);
  if (isFrozen) {
    uint32_t elapsed = millis() - state.iceEffectTime;
    uint8_t iceIntensity = (elapsed < 1000) ? map(elapsed % 200, 0, 200, 200, 50) : 200;
    uint16_t iceColor = fg;
    for (uint8_t dy=0; dy<mh; dy++) {
      for (uint8_t dx=0; dx<cw; dx++) {
        if (dx==0 || dx==cw-1 || dy==0 || dy==mh-1) {
          uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
          if (pat < (uint8_t)(iceIntensity / 4)) display.drawPixel(x+dx, my+dy, iceColor);
        }
      }
    }
  }
}

// ----- Кораблик -----
const uint8_t sailboat[9][8] = {
  {0,0,0,0,0,0,0,0},{0,0,0,1,1,0,0,0},{0,0,0,1,1,1,0,0},
  {0,0,1,1,1,1,0,0},{0,1,0,1,1,1,0,0},{0,1,0,1,1,0,0,1},
  {1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,0},{0,1,1,1,1,1,0,0}
};
const uint8_t sailboatAlt[9][8] = {
  {0,0,0,0,0,0,0,0},{0,0,0,1,1,0,0,0},{0,0,0,1,1,1,0,0},
  {0,0,1,1,1,1,0,0},{0,1,0,0,1,1,0,0},{0,1,0,1,1,0,0,0},
  {1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,0},{0,1,1,1,1,1,0,0}
};

void drawSailboat(uint8_t x, uint8_t y, uint32_t time, uint8_t amt, bool bypass=false) {
  if (!state.gfxEnabled) return;
  bool alt = ((time / 400) & 1) == 0;
  const uint8_t (*boat)[8] = alt ? sailboatAlt : sailboat;
  float wave = 1.0f + (amt / 100.0f) * 0.8f;
  int8_t off = (int8_t)(sin(time / 800.0f * wave) * 1.5);
  uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t r=0; r<9; r++) {
    for (uint8_t c=0; c<8; c++) {
      if (boat[r][c]) {
        uint8_t px = x + c, py = y + r + (r>=8 ? off : 0);
        if (px < Display::W && py < Display::H) {
          uint8_t pat = bayer[(py + time/100) & 7][(px + time/50) & 7];
          if (pat < 56 || amt > 80) display.drawPixel(px, py, color);
        }
      }
    }
  }
  if (amt > 60 && random(0,100) < ((amt-60)/40.0f)*30) {
    if (x+3 < Display::W) { display.drawPixel(x+3, y, color); if (random(0,100)<50 && x+4<Display::W) display.drawPixel(x+4, y, color); }
  }
}

// ----- Заголовок и страницы -----
const char* pageNames[] = {"Main", "CC", "Storm", ""};
const char* paramNames[4][4] = {
  {"P1","P2","P3","P4"},
  {"P1","P2","P3","P4"},
  {"AMT","WAV","WTH","ARM"},
  {"","","",""}
};

void drawHeader(Page page, uint8_t sel, bool bypass=false) {
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  if (state.bypassMode == BypassMode::FREEZE && page != Page::SEQUENCER) {
    drawTextWithOutline(2, 2, "FROZEN", fg, bg);
    return;
  }
  // BYPASS ничего не морозит (движок работает как обычно), поэтому
  // не переворачивает цвета всей страницы — bypass здесь останется
  // false, и баннер рисуется обычными цветами.
  if (state.bypassMode == BypassMode::BYPASS && page != Page::SEQUENCER) {
    drawTextWithOutline(2, 2, "BYPASS", fg, bg);
    return;
  }
  uint8_t idx = (uint8_t)page;
  const char* pName = pageNames[idx];
  const char* param;
  if (page == Page::STORM && state.subPageStorm == 1) {
    static const char* fogSubParams[4]  = {"TYPE","SHAPE","PHAS","GLIDE"};
    static const char* sunSubParams[4]  = {"RFLCT","ARP","DFLCT","BIAS"};
    static const char* snowSubParams[4] = {"FLAKE","ROTATE","FRZ","TIME"};
    static const char* rainSubParams[4] = {"DRIP","WET","SPLSH","THNDR"};
    switch (state.weatherMode) {
      case WeatherMode::SUN:  param = sunSubParams[sel]; break;
      case WeatherMode::SNOW: param = snowSubParams[sel]; break;
      case WeatherMode::RAIN: param = rainSubParams[sel]; break;
      default:                param = fogSubParams[sel]; break;
    }
  } else if (page == Page::CC && state.subPageCC == 1) {
    static const char* ccSubParams[4] = {"MIDI CH","BYPASS","SEQ DEST","GFX"};
    param = ccSubParams[sel];
  } else {
    param = paramNames[idx][sel];
  }
  int x = 0, y = 0;
  drawTextWithOutline(x, y, pName, fg, bg);
  x += strlen(pName)*6 + 2;
  drawTextWithOutline(x, y, " ", fg, bg);
  x += 4;
  char buf[16];
  strcpy(buf, param);
  if ((int)strlen(param)*6 > 128 - x - 40) {
    int max = (128 - x - 40) / 6 - 1;
    if (max > 1) { strncpy(buf, param, max); buf[max]=0; strcat(buf, "."); }
  }
  drawTextWithOutline(x, y, buf, fg, bg);
  x += strlen(buf)*6 + 2;
  // Позиция "S" считается относительно stateX/pageX ниже, чтобы не
  // пересекаться со счётчиком страниц "N/4".
  bool showSub = (page == Page::MAIN && state.subPageMain == 1) ||
                 (page == Page::CC && state.subPageCC == 1);
  if (page != Page::SEQUENCER) {
    char p[8]; sprintf(p, "%d/4", idx+1);
    int pageX = 128 - strlen(p)*6;
    const char* stateSymbol = state.sequencerRunning ? ">" : "|";
    int stateX = pageX - 8; if (stateX < 0) stateX = 0;
    drawTextWithOutline(stateX, y, stateSymbol, fg, bg);
    drawTextWithOutline(pageX, y, p, fg, bg);
    if (showSub) {
      int sX = stateX - 8; if (sX < 0) sX = 0;
      drawTextWithOutline(sX, y, "S", fg, bg);
    }
  } else if (showSub) {
    drawTextWithOutline(120, y, "S", fg, bg);
  }
}

void drawMain(bool bypass) {
  g_fillBackground = false;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawSeaLines(animTime, amt, bypass);
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::MAIN, state.selectedParam, bypass);
  drawSailboat(64, 0, animTime, amt, bypass);
  if (state.subPageMain == 0) {
    for (uint8_t i=0; i<NUM_PARAMS; i++)
      drawColumn(i*32, state.params[i].value, state.selectedParam==i, animTime, amt, bypass, state.frozen[i]);
  } else {
    for (uint8_t i=0; i<NUM_PARAMS; i++) {
      uint8_t x = i*32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) display.fillRect(x, Display::METER_Y, Display::COL_W, Display::METER_H, fg);
      else display.fillRect(x, Display::METER_Y, Display::COL_W, Display::METER_H, bg2);
      display.setTextSize(1);
      display.setTextColor(state.selectedParam == i ? bg2 : fg);
      display.setCursor(x+2, Display::METER_Y+2); display.print("mi");
      display.setCursor(x+2, Display::METER_Y+Display::METER_H-10); display.print("ma");
      char buf[4]; sprintf(buf, "%3d", state.params[i].min);
      display.setCursor(x+14, Display::METER_Y+2); display.print(buf);
      sprintf(buf, "%3d", state.params[i].max);
      display.setCursor(x+14, Display::METER_Y+Display::METER_H-10); display.print(buf);
      if (state.selectedParam == i) {
        uint8_t yy = state.editMin ? Display::METER_Y+2 : Display::METER_Y+Display::METER_H-10;
        display.drawRect(x+12, yy-1, 16, 8, fg);
      }
    }
  }
  display.display();
}

// Прямоугольная плашка поверх страницы CC на время ожидания входящего
// MIDI Learn — "Waiting for midi data" в две строки (не влезает
// одной при 22 символах на 128px экране).
void drawMidiLearnBanner(bool bypass) {
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  // Рамка на 2px больше в каждом измерении (по 1px на сторону),
  // центр остался на месте.
  const uint8_t bx = 7, by = 5, bw = Display::W - 14, bh = Display::H - 10;
  display.fillRect(bx, by, bw, bh, bg);
  display.drawRect(bx, by, bw, bh, fg);
  display.setTextSize(1);
  display.setTextColor(fg);
  const char* line1 = "Waiting for";
  const char* line2 = "midi data";
  int16_t x1, y1; uint16_t w1, h1, w2, h2;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
  // Обе строки центрируются как единый блок по вертикали.
  const uint8_t gap = 2;
  uint16_t blockH = h1 + gap + h2;
  int16_t textTop = by + ((int16_t)bh - (int16_t)blockH) / 2;
  display.setCursor(bx + (bw - w1) / 2, textTop);
  display.print(line1);
  display.setCursor(bx + (bw - w2) / 2, textTop + h1 + gap);
  display.print(line2);
}

void drawCC(bool bypass) {
  g_fillBackground = false;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawSeaLines(animTime, amt, bypass);
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::CC, state.selectedParam, bypass);
  drawSailboat(64, 0, animTime, amt, bypass);
  if (state.subPageCC == 0) {
    for (uint8_t i=0; i<NUM_PARAMS; i++)
      drawCCColumn(i*32, state.params[i].cc, state.selectedParam==i, animTime, bypass);
  } else {
    const char* labels[4] = {"CH", "BYP", "DST", "GFX"};
    char values[4][8];
    sprintf(values[0], "%d", state.midiChannel);
    sprintf(values[1], "%s", (state.selectedBypassMode == BypassMode::FREEZE) ? "FRZ" : "BYP");
    sprintf(values[2], "%s", seqDestNames[(uint8_t)state.seqDest]);
    sprintf(values[3], "%s", state.gfxEnabled ? "ON" : "OFF");
    for (uint8_t i=0; i<4; i++) {
      uint8_t x = i*32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) {
        display.fillRect(x, Display::METER_Y, Display::COL_W, Display::METER_H, fg);
        display.setTextColor(bg2);
      } else {
        display.fillRect(x, Display::METER_Y, Display::COL_W, Display::METER_H, bg2);
        display.setTextColor(fg);
      }
      display.setTextSize(1);
      int16_t x1,y1; uint16_t w,h;
      display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
      int labelX = x + (Display::COL_W - w) / 2;
      int labelY = Display::METER_Y + 1;
      display.setCursor(labelX, labelY);
      display.print(labels[i]);
      display.getTextBounds(values[i], 0, 0, &x1, &y1, &w, &h);
      int valX = x + (Display::COL_W - w) / 2;
      int valY = Display::METER_Y + Display::METER_H - h - 1;
      display.setCursor(valX, valY);
      display.print(values[i]);
      if (state.selectedParam == i) {
        drawCornerCursor(x, Display::METER_Y, Display::COL_W, Display::METER_H, fg);
      }
    }
  }
  if (state.midiLearnActive) drawMidiLearnBanner(bypass);
  display.display();
}

// Одна снежинка: petals лучей из центра, у каждого луча — короткая
// боковая "веточка" примерно на середине. Число лучей визуально
// передаёт число шагов евклидового секвенсора (FLAKE, 3..12).
void drawSnowflakeSprite(int16_t cx, int16_t cy, uint8_t radius, uint8_t petals, uint16_t color) {
  display.drawPixel(cx, cy, color);
  for (uint8_t i = 0; i < petals; i++) {
    float ang = (2.0f * 3.14159f * i) / petals - 3.14159f / 2.0f;
    float dx = cos(ang), dy = sin(ang);
    for (uint8_t r = 1; r <= radius; r++) {
      display.drawPixel(cx + (int16_t)lroundf(dx * r), cy + (int16_t)lroundf(dy * r), color);
    }
    float branchR = radius * 0.55f;
    float bx = cx + dx * branchR, by = cy + dy * branchR;
    float perpX = -dy, perpY = dx;
    display.drawPixel((int16_t)lroundf(bx + perpX), (int16_t)lroundf(by + perpY), color);
    display.drawPixel((int16_t)lroundf(bx - perpX), (int16_t)lroundf(by - perpY), color);
  }
}

// Пиктограмма RFLCT: стопка прямоугольников, "убегающих" по диагонали
// назад-вправо-вниз, как эхо-повторы. count = state.sunRflct (0..4):
// count=0 — один сплошной прямоугольник (источник, без отражений);
// иначе к нему добавляются count контурных прямоугольников позади —
// каждое следующее "эхо" на step пикселей дальше от источника.
void drawEchoRectSprite(int16_t cx, int16_t cy, uint8_t count, uint16_t color) {
  const uint8_t rw = 7, rh = 5, step = 2;
  int16_t stackW = rw + (int16_t)count * step;
  int16_t stackH = rh + (int16_t)count * step;
  int16_t originX = cx - stackW / 2;
  int16_t originY = cy - stackH / 2;
  for (int8_t r = count; r >= 1; r--) {
    display.drawRect(originX + r * step, originY + r * step, rw, rh, color);
  }
  display.fillRect(originX, originY, rw, rh, color);
}

// Пиктограмма DRIP: круги на воде. Точка падения капли в центре,
// плюс кольца — число колец растёт вместе со значением (0..127).
void drawRippleSprite(int16_t cx, int16_t cy, uint8_t value, uint16_t color) {
  display.fillCircle(cx, cy, 1, color);
  uint8_t rings = map(value, 0, 127, 0, 4);
  for (uint8_t r = 1; r <= rings; r++) {
    display.drawCircle(cx, cy, r * 2 + 1, color);
  }
}

void drawStorm(bool bypass) {
  g_fillBackground = false;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawSeaLines(animTime, amt, bypass);
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::STORM, state.selectedParam, bypass);
  drawSailboat(64, 0, animTime, amt, bypass);
  drawColumn(0, map(state.chaos, 0, 100, 0, 127), state.selectedParam==0, animTime, amt, bypass, state.frozenAmt);
  uint8_t interval = waveIntervals[state.waveIntervalIndex];
  uint32_t currentPos = state.midiTicks % interval;
  uint8_t wavFill = map(currentPos, 0, interval, 0, 127);
  drawColumnText(32, wavFill, waveIntervalNames[state.waveIntervalIndex], state.selectedParam==1, animTime, amt, bypass, state.frozenWav);
  const char* wthText;
  switch (state.weatherMode) {
    case WeatherMode::FOG: wthText="FOG"; break;
    case WeatherMode::SUN: wthText="SUN"; break;
    case WeatherMode::RAIN: wthText="RAIN"; break;
    case WeatherMode::SNOW: wthText="SNOW"; break;
    default: wthText="---";
  }
  // Индикация пульсации евклидового секвенсора SNOW — вспышка
  // снежинки за баром WEATHER. drawColumnMode ничего не заливает под
  // собой (в отличие от других колонок), так что вспышка остаётся
  // видна позади текста/курсора, а не перекрывается им. Видна только
  // в режиме SNOW и только в момент "хита" (см. processSnowSequencer).
  if (state.weatherMode == WeatherMode::SNOW && state.snowPulseActive) {
    uint16_t pulseColor = bypass ? SSD1306_BLACK : SSD1306_WHITE;
    int16_t cx = 64 + Display::COL_W / 2;
    int16_t cy = Display::METER_Y + Display::METER_H / 2;
    drawSnowflakeSprite(cx, cy, 9, state.snowFlake, pulseColor);
  }
  drawColumnMode(64, wthText, state.selectedParam==2, animTime, bypass, state.frozenWth);
  drawColumnYesNo(96, state.randomizerEnabled, state.selectedParam==3, animTime, amt, bypass, state.frozenArm);
  display.display();
}

void drawStormSubpage(bool bypass) {
  g_fillBackground = false;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawSeaLines(animTime, amt, bypass);
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::STORM, state.selectedParam, bypass);
  drawSailboat(64, 0, animTime, amt, bypass);

  if (state.weatherMode == WeatherMode::FOG) {
    // Унифицировано с SUN/SNOW/RAIN: выбранная колонка заливается
    // инвертированным фоном целиком (а не только уголки курсора).
    const uint8_t my = Display::METER_Y;
    const uint8_t mh = Display::METER_H;
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t x = i * 32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) {
        display.fillRect(x, my, Display::COL_W, mh, fg);
        display.setTextColor(bg2);
      } else {
        display.fillRect(x, my, Display::COL_W, mh, bg2);
        display.setTextColor(fg);
      }
      display.setTextSize(1);
      uint16_t contentColor = (state.selectedParam == i) ? bg2 : fg;
      if (i == 0) {
        // Форма волны сама себя объясняет — подпись не нужна.
        drawWaveform(x, my, Display::COL_W, mh, state.lfoType, state.lfoShape, state.lfoPhase, animTime, contentColor);
      } else if (i == 1 || i == 2) {
        const char* lbl = (i == 1) ? "SHAPE" : "PHASE";
        char buf[5];
        sprintf(buf, "%d", (i == 1) ? state.lfoShape : state.lfoPhase);
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(lbl, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(lbl);
        display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + mh - h - 1);
        display.print(buf);
      } else if (i == 3) {
        const char* lbl = "GLIDE";
        char buf[4];
        sprintf(buf, "%d", state.lfoGlide);
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(lbl, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(lbl);
        display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + mh - h - 1);
        display.print(buf);
      }
      if (state.selectedParam == i) drawCornerCursor(x, my, Display::COL_W, mh, fg);
    }
  } else if (state.weatherMode == WeatherMode::SUN) {
    const char* labels[4] = {"RFLCT", "ARP", "DFLCT", "BIAS"};
    char values[4][8];
    sprintf(values[1], "%s", sunArpNames[state.sunArp]);
    sprintf(values[2], "%d", state.sunDflct);
    sprintf(values[3], "%d", state.sunBias);

    const uint8_t my = Display::METER_Y;
    const uint8_t mh = Display::METER_H;
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t x = i * 32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) {
        display.fillRect(x, my, Display::COL_W, mh, fg);
        display.setTextColor(bg2);
      } else {
        display.fillRect(x, my, Display::COL_W, mh, bg2);
        display.setTextColor(fg);
      }
      display.setTextSize(1);
      if (i == 0) {
        // RFLCT: вместо числа — пиктограмма из прямоугольников,
        // "убегающих" по диагонали как эхо-повторы (0..4 штуки).
        uint16_t iconColor = (state.selectedParam == i) ? bg2 : fg;
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[0], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(labels[0]);
        int16_t cx = x + Display::COL_W / 2;
        int16_t cy = my + 1 + h + 1 + (mh - (h + 2)) / 2;
        drawEchoRectSprite(cx, cy, state.sunRflct, iconColor);
      } else {
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
        int labelX = x + (Display::COL_W - w) / 2;
        int labelY = my + 1;
        display.setCursor(labelX, labelY);
        display.print(labels[i]);
        display.getTextBounds(values[i], 0, 0, &x1, &y1, &w, &h);
        int valX = x + (Display::COL_W - w) / 2;
        int valY = my + mh - h - 1;
        display.setCursor(valX, valY);
        display.print(values[i]);
      }
      if (state.selectedParam == i) {
        drawCornerCursor(x, my, Display::COL_W, mh, fg);
      }
    }
  } else if (state.weatherMode == WeatherMode::SNOW) {
    const char* labels[4] = {"FLAKE", "ROTATE", "FRZ", "TIME"};
    char values[4][8];
    sprintf(values[0], "%d", state.snowFlake);
    sprintf(values[1], "%d", state.snowRotation);
    sprintf(values[2], "%d", state.snowFrz);
    sprintf(values[3], "%d", state.snowTime);

    const uint8_t my = Display::METER_Y;
    const uint8_t mh = Display::METER_H;
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t x = i * 32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) {
        display.fillRect(x, my, Display::COL_W, mh, fg);
        display.setTextColor(bg2);
      } else {
        display.fillRect(x, my, Display::COL_W, mh, bg2);
        display.setTextColor(fg);
      }
      display.setTextSize(1);
      uint16_t dotColor = (state.selectedParam == i) ? bg2 : fg;

      if (i == 0) {
        // FLAKE: одна снежинка, число лучей = число шагов секвенсора (3..12).
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(values[0], 0, 0, &x1, &y1, &w, &h);
        uint8_t valueY = my + mh - h - 1;
        int16_t cx = x + Display::COL_W / 2;
        int16_t cy = my + 2 + (valueY - (my + 2)) / 2;
        drawSnowflakeSprite(cx, cy, 5, state.snowFlake, dotColor);
        display.setCursor(x + (Display::COL_W - w) / 2, valueY);
        display.print(values[0]);
      } else {
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(labels[i]);
        display.getTextBounds(values[i], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + mh - h - 1);
        display.print(values[i]);
      }
      if (state.selectedParam == i) drawCornerCursor(x, my, Display::COL_W, mh, fg);
    }
  } else if (state.weatherMode == WeatherMode::RAIN) {
    const char* labels[4] = {"DRIP", "WET", "SPLSH", "THNDR"};
    char values[4][8];
    sprintf(values[1], "%d", state.rainWet);
    sprintf(values[2], "%d", state.rainSplsh);

    const uint8_t my = Display::METER_Y;
    const uint8_t mh = Display::METER_H;
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t x = i * 32;
      uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
      uint16_t bg2 = bypass ? SSD1306_WHITE : SSD1306_BLACK;
      if (state.selectedParam == i) {
        display.fillRect(x, my, Display::COL_W, mh, fg);
        display.setTextColor(bg2);
      } else {
        display.fillRect(x, my, Display::COL_W, mh, bg2);
        display.setTextColor(fg);
      }
      display.setTextSize(1);
      if (i == 0) {
        // DRIP: вместо числового бара — круги на воде (рябь), число
        // колец растёт вместе со значением.
        uint16_t iconColor = (state.selectedParam == i) ? bg2 : fg;
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[0], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(labels[0]);
        int16_t cx = x + Display::COL_W / 2;
        int16_t cy = my + 1 + h + 1 + (mh - (h + 2)) / 2;
        drawRippleSprite(cx, cy, state.rainDrip, iconColor);
      } else if (i == 3) {
        // THNDR: простой вкл/выкл-переключатель, как ARM на STORM.
        const char* txt = state.rainThunder ? "ON" : "OFF";
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[3], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(labels[3]);
        display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + mh - h - 1);
        display.print(txt);
      } else {
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + 1);
        display.print(labels[i]);
        display.getTextBounds(values[i], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(x + (Display::COL_W - w) / 2, my + mh - h - 1);
        display.print(values[i]);
      }
      if (state.selectedParam == i) drawCornerCursor(x, my, Display::COL_W, mh, fg);
    }
  } else {
    display.setTextSize(1);
    display.setTextColor(bypass ? SSD1306_BLACK : SSD1306_WHITE);
    display.setCursor(10, 16);
    display.print("Coming soon...");
  }
  display.display();
}

void drawSequencer(bool bypass) {
  g_fillBackground = true;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::SEQUENCER, state.selectedParam, bypass);
  const uint8_t cellSize=7, spacing=1, total=cellSize+spacing;
  uint8_t startX = (Display::W - SEQUENCER_STEPS * total) / 2;
  uint8_t y=3;
  bool flashNow = state.stepFlashActive && (millis() - state.stepFlashTime < 100);
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t step=0; step<state.sequencerSteps; step++) {
    uint8_t x = startX + step * total;
    // Верхний ряд показывает активность той же колонки, что выбрана
    // внизу — переводим визуальную позицию в индекс данных так же,
    // как и везде на этой странице (см. SEQ_COL_ORDER).
    bool active = state.steps[step].active[SEQ_COL_ORDER[state.selectedParam]];
    bool isPlayhead = (step == state.sequencerDisplayStep);
    bool isCursor = (step == state.sequencerCursor);
    if (isPlayhead && flashNow) display.fillRect(x, y, cellSize, cellSize, fg);
    else display.fillRect(x, y, cellSize, cellSize, active ? fg : bg);
    if (bypass) drawDashedRect(x, y, cellSize, cellSize, fg);
    else display.drawRect(x, y, cellSize, cellSize, fg);
    if (isPlayhead && !flashNow) {
      if (active) display.drawRect(x+1, y+1, cellSize-2, cellSize-2, fg);
      else {
        for (uint8_t i=0; i<cellSize; i+=2) {
          display.drawPixel(x+i, y, fg); display.drawPixel(x+i, y+cellSize-1, fg);
          display.drawPixel(x, y+i, fg); display.drawPixel(x+cellSize-1, y+i, fg);
        }
      }
    }
    if (isCursor) display.fillRect(x+2, y+2, 3, 3, active ? bg : fg);
  }
  const uint8_t my=11, mh=Display::METER_H;
  const char* columnLabels[4] = {"NOTE", "CC", seqDestNames[(uint8_t)state.seqDest], "RTRG"};
  uint8_t noteVal = state.steps[state.sequencerCursor].note;
  uint8_t ccVal = state.steps[state.sequencerCursor].cc;
  uint8_t destVal = state.steps[state.sequencerCursor].wavesIndex;
  uint8_t retrigVal = state.steps[state.sequencerCursor].retrigIndex;
  char noteText[6]; midiNoteToString(noteVal, noteText);
  char ccText[4]; sprintf(ccText, "%d", ccVal);
  char destText[8];
  switch (state.seqDest) {
    case SeqDest::WAV: {
      uint8_t idx = constrain(destVal, 0, 4);
      strcpy(destText, waveIntervalNames[idx]);
      break;
    }
    case SeqDest::AMT: {
      sprintf(destText, "%d", destVal);
      break;
    }
    case SeqDest::WTH: {
      uint8_t w = constrain(destVal, 0, 3);
      const char* wNames[4] = {"FOG", "SUN", "RAIN", "SNOW"};
      strcpy(destText, wNames[w]);
      break;
    }
  }
  char retrigText[6]; strcpy(retrigText, retrigNames[retrigVal]);
  const char* texts[4] = {noteText, ccText, destText, retrigText};

  for (uint8_t i=0; i<4; i++) {
    uint8_t p = SEQ_COL_ORDER[i]; // визуальная позиция i -> индекс данных p
    uint8_t x = i*32;
    bool triggerActive = state.steps[state.sequencerCursor].active[p];
    display.fillRect(x, my, Display::COL_W, mh, triggerActive ? fg : bg);
    uint32_t elapsed = millis() - state.triggerTime[p];
    if (elapsed < 150) {
      uint8_t alpha = map(elapsed, 0, 150, 255, 0);
      uint16_t frameColor = triggerActive ? bg : fg;
      uint8_t threshold = map(alpha, 0, 255, 80, 20);
      for (uint8_t dy=0; dy<mh; dy++) {
        for (uint8_t dx=0; dx<Display::COL_W; dx++) {
          if (dx==0 || dx==Display::COL_W-1 || dy==0 || dy==mh-1) {
            uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
            if (pat < threshold) display.drawPixel(x+dx, my+dy, frameColor);
          }
        }
      }
    }
    uint8_t retrigIdx = state.steps[state.sequencerCursor].retrigIndex;
    if (retrigIdx > 0) {
      uint32_t retrigElapsed = millis() - state.retriggerTime[p];
      if (retrigElapsed < 80) {
        uint8_t alpha = map(retrigElapsed, 0, 80, 255, 0);
        uint16_t frameColor = triggerActive ? bg : fg;
        uint8_t threshold = map(alpha, 0, 255, 60, 15);
        for (uint8_t dy=0; dy<mh; dy++) {
          for (uint8_t dx=0; dx<Display::COL_W; dx++) {
            if (dx==0 || dx==Display::COL_W-1 || dy==0 || dy==mh-1) {
              if ((dx+dy)%2==0) {
                uint8_t pat = bayer[(dy + millis()/100) & 7][(dx + millis()/150) & 7];
                if (pat < threshold) display.drawPixel(x+dx, my+dy, frameColor);
              }
            }
          }
        }
      }
    }
    if (state.selectedParam == i) drawCornerCursor(x, my, Display::COL_W, mh, triggerActive ? bg : fg);
    display.setTextSize(1);
    uint16_t textColor = triggerActive ? bg : fg;
    uint16_t outlineColor = triggerActive ? fg : bg;
    int16_t x1,y1; uint16_t w,h;
    display.getTextBounds(columnLabels[p], 0, 0, &x1, &y1, &w, &h);
    drawTextWithOutline(x + (Display::COL_W-w)/2, my+2, columnLabels[p], textColor, outlineColor);
    display.getTextBounds(texts[p], 0, 0, &x1, &y1, &w, &h);
    drawTextWithOutline(x + (Display::COL_W-w)/2, my + mh - h - 1, texts[p], textColor, outlineColor);
  }
  display.display();
}

void drawSequencerSetup(bool bypass) {
  g_fillBackground = true;
  uint16_t bg = bypass ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillScreen(bg);
  uint32_t animTime = getAnimTime();
  uint8_t amt = state.chaos;
  drawWeather(animTime, amt, bypass);
  drawHeader(Page::SEQUENCER, state.selectedParam, bypass);
  const uint8_t cellSize=7, spacing=1, total=cellSize+spacing;
  uint8_t startX = (Display::W - SEQUENCER_STEPS * total) / 2;
  uint8_t y=3;
  bool flashNow = state.stepFlashActive && (millis() - state.stepFlashTime < 100);
  uint16_t fg = bypass ? SSD1306_BLACK : SSD1306_WHITE;
  for (uint8_t step=0; step<state.sequencerSteps; step++) {
    uint8_t x = startX + step * total;
    bool active = state.steps[step].active[state.selectedParam];
    bool isPlayhead = (step == state.sequencerDisplayStep);
    bool isCursor = (step == state.sequencerCursor);
    if (isPlayhead && flashNow) display.fillRect(x, y, cellSize, cellSize, fg);
    else display.fillRect(x, y, cellSize, cellSize, active ? fg : bg);
    if (bypass) drawDashedRect(x, y, cellSize, cellSize, fg);
    else display.drawRect(x, y, cellSize, cellSize, fg);
    if (isPlayhead && !flashNow) {
      if (active) display.drawRect(x+1, y+1, cellSize-2, cellSize-2, fg);
      else {
        for (uint8_t i=0; i<cellSize; i+=2) {
          display.drawPixel(x+i, y, fg); display.drawPixel(x+i, y+cellSize-1, fg);
          display.drawPixel(x, y+i, fg); display.drawPixel(x+cellSize-1, y+i, fg);
        }
      }
    }
    if (isCursor) display.fillRect(x+2, y+2, 3, 3, active ? bg : fg);
  }
  const uint8_t setupY=11, setupH=Display::METER_H;
  const char* labels[4] = {"BPM","STEPS","CC","SCALE"};
  char values[4][8];
  if (state.midiRunning) sprintf(values[0], "%.0f", state.bpm);
  else sprintf(values[0], "%d", state.sequencerBPM);
  sprintf(values[1], "%d", state.sequencerSteps);
  sprintf(values[2], "%d", state.sequencerCC);
  sprintf(values[3], "%s", scaleNames[state.sequencerScaleIndex]);
  for (uint8_t i=0; i<4; i++) {
    uint8_t x = i*32;
    display.fillRect(x, setupY, Display::COL_W, setupH, bg);
    if (state.selectedParam == i) drawCornerCursor(x, setupY, Display::COL_W, setupH, fg);
    display.setTextSize(1); display.setTextColor(fg);
    int16_t x1,y1; uint16_t w,h;
    display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x + (Display::COL_W-w)/2, setupY+2);
    display.print(labels[i]);
    display.getTextBounds(values[i], 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x + (Display::COL_W-w)/2, setupY + setupH - h - 1);
    display.print(values[i]);
  }
  if (state.midiLearnActive) drawMidiLearnBanner(bypass);
  display.display();
}

// ============================================================
// 13. ОБНОВЛЕНИЕ ДИСПЛЕЯ
// ============================================================
void updateDisplay() {
  bool bypass = (state.bypassMode == BypassMode::FREEZE);

  if (state.bypassTransition) {
    uint32_t elapsed = millis() - state.transitionStart;
    if (elapsed < 150) {
      uint16_t bg = state.transitionDirection ? SSD1306_WHITE : SSD1306_BLACK;
      uint16_t fg = state.transitionDirection ? SSD1306_BLACK : SSD1306_WHITE;
      display.fillScreen(bg);
      uint8_t progress = map(elapsed, 0, 150, 0, 100);
      uint8_t radius = map(progress, 0, 100, 0, 64);
      for (uint8_t y = 0; y < Display::H; y++) {
        for (uint8_t x = 0; x < Display::W; x++) {
          int16_t dx = x - Display::W / 2;
          int16_t dy = y - Display::H / 2;
          uint8_t dist = sqrt(dx * dx + dy * dy);
          if (dist < radius) {
            uint8_t pat = bayer[y & 7][x & 7];
            if (pat < map(progress, 0, 100, 0, 64)) {
              display.drawPixel(x, y, fg);
            }
          }
        }
      }
      display.display();
      state.displayDirty = true;
      return;
    } else {
      state.bypassTransition = false;
      state.displayDirty = true;
    }
  }

  if (!state.displayDirty) return;
  state.displayDirty = false;

  switch (state.currentPage) {
    case Page::MAIN:   drawMain(bypass); break;
    case Page::CC:     drawCC(bypass); break;
    case Page::STORM:
      if (state.subPageStorm == 0) drawStorm(bypass);
      else drawStormSubpage(bypass);
      break;
    case Page::SEQUENCER:
      if (state.subPageSequencer == 0) drawSequencer(bypass);
      else drawSequencerSetup(bypass);
      break;
    default: drawMain(bypass); break;
  }

  bool needDisplay = false;
  if (state.encLongPressFrameVisible && state.encLongPressActive && !state.encLongPressTriggered) {
    uint32_t elapsed = millis() - state.encLongPressStart;
    if (elapsed >= 1000 && elapsed < 3000) {
      uint32_t blinkPhase = (millis() - state.encLongPressLastBlink) % 500;
      if (blinkPhase < 250) {
        uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
        for (uint8_t i = 0; i < 2; i++) {
          for (uint8_t x = 0; x < Display::W; x++) {
            display.drawPixel(x, i, color);
            display.drawPixel(x, Display::H - 1 - i, color);
          }
          for (uint8_t y = 0; y < Display::H; y++) {
            display.drawPixel(i, y, color);
            display.drawPixel(Display::W - 1 - i, y, color);
          }
        }
        needDisplay = true;
      }
    }
  }
  if (state.tapPageLongPressFrameVisible && state.tapPageLongPressActive && !state.tapPageLongPressTriggered) {
    uint32_t elapsed = millis() - state.tapPageLongPressStart;
    if (elapsed >= 1000 && elapsed < 3000) {
      uint32_t blinkPhase = (millis() - state.tapPageLongPressLastBlink) % 500;
      if (blinkPhase < 250) {
        uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
        for (uint8_t i = 0; i < 2; i++) {
          for (uint8_t x = 0; x < Display::W; x++) {
            display.drawPixel(x, i, color);
            display.drawPixel(x, Display::H - 1 - i, color);
          }
          for (uint8_t y = 0; y < Display::H; y++) {
            display.drawPixel(i, y, color);
            display.drawPixel(Display::W - 1 - i, y, color);
          }
        }
        needDisplay = true;
      }
    }
  }
  if (state.tapPlayLongPressFrameVisible && state.tapPlayLongPressActive && !state.tapPlayLongPressTriggered) {
    uint32_t elapsed = millis() - state.tapPlayLongPressStart;
    if (elapsed >= 1000 && elapsed < 3000) {
      uint32_t blinkPhase = (millis() - state.tapPlayLongPressLastBlink) % 500;
      if (blinkPhase < 250) {
        uint16_t color = bypass ? SSD1306_BLACK : SSD1306_WHITE;
        for (uint8_t i = 0; i < 2; i++) {
          for (uint8_t x = 0; x < Display::W; x++) {
            display.drawPixel(x, i, color);
            display.drawPixel(x, Display::H - 1 - i, color);
          }
          for (uint8_t y = 0; y < Display::H; y++) {
            display.drawPixel(i, y, color);
            display.drawPixel(Display::W - 1 - i, y, color);
          }
        }
        needDisplay = true;
      }
    }
  }
  if (needDisplay) display.display();
}

// ============================================================
// 14. ХРАНИЛИЩЕ
// ============================================================
void loadSettings() {
  storage.begin("redsea", true);
  for (uint8_t i = 0; i < NUM_PARAMS; i++) {
    char key[8], keyMin[8], keyMax[8];
    sprintf(key, "cc%u", i);
    sprintf(keyMin, "min%u", i);
    sprintf(keyMax, "max%u", i);
    state.params[i].cc = storage.getUChar(key, state.params[i].cc);
    state.params[i].min = storage.getUChar(keyMin, 0);
    state.params[i].max = storage.getUChar(keyMax, 127);
  }
  state.chaos = storage.getUChar("chaos", state.chaos);
  state.waveIntervalIndex = storage.getUChar("waveIdx", 2);
  if (state.waveIntervalIndex > 4) state.waveIntervalIndex = 2;
  uint8_t weather = storage.getUChar("weather", 0);
  if (weather > 3) weather = 0;
  state.weatherMode = (WeatherMode)weather;
  // LFO
  state.lfoType = storage.getUChar("lfoType", 0);
  state.lfoShape = storage.getInt("lfoShape", 0);
  state.lfoPhase = storage.getInt("lfoPhase", 0);
  state.lfoGlide = storage.getUChar("lfoGlide", 0);
  // SUN-параметры
  state.sunRflct = storage.getUChar("sunRflct", 0);
  if (state.sunRflct > 4) state.sunRflct = 4;
  state.sunArp = storage.getUChar("sunArp", 0);
  if (state.sunArp > 4) state.sunArp = 0;
  state.sunDflct = storage.getUChar("sunDflct", 64);
  state.sunBias = storage.getInt("sunBias", 0);
  if (state.sunBias < -63) state.sunBias = -63;
  if (state.sunBias > 64) state.sunBias = 64;
  // SNOW-параметры
  state.snowFlake = storage.getUChar("snowFlake", 8);
  if (state.snowFlake < 3 || state.snowFlake > 12) state.snowFlake = 8;
  state.snowRotation = storage.getUChar("snowRot", 0);
  if (state.snowRotation > 11) state.snowRotation = 0;
  state.snowFrz = storage.getUChar("snowFrz", 0);
  state.snowTime = storage.getInt("snowTime", 0);
  if (state.snowTime < -63) state.snowTime = -63;
  if (state.snowTime > 64) state.snowTime = 64;
  // RAIN-параметры
  state.rainDrip = storage.getUChar("rainDrip", 0);
  state.rainWet = storage.getUChar("rainWet", 114);
  state.rainSplsh = storage.getUChar("rainSplsh", 0);
  state.rainThunder = storage.getBool("rainThndr", false);

  state.midiChannel = storage.getUChar("midiCh", 1);
  if (state.midiChannel < 1 || state.midiChannel > 16) state.midiChannel = 1;
  // Сохраняется/загружается только ВЫБОР режима (BYPASS/FREEZE) —
  // сам bypassMode всегда стартует в OFF, иначе после перезагрузки
  // устройство просыпалось бы в "FROZEN" с баннером на экране, но
  // ни один параметр фактически не был бы заморожен (frozen[] не
  // персистится и всегда стартует пустым).
  uint8_t mode = storage.getUChar("bypassMode", (uint8_t)BypassMode::FREEZE);
  state.selectedBypassMode = (mode == (uint8_t)BypassMode::BYPASS) ? BypassMode::BYPASS : BypassMode::FREEZE;
  state.bypassMode = BypassMode::OFF;
  uint8_t dest = storage.getUChar("seqDest", 0);
  state.seqDest = (dest < 3) ? (SeqDest)dest : SeqDest::WAV;
  state.gfxEnabled = storage.getBool("gfx", true);
  state.sequencerSteps = storage.getUChar("seqSteps", 16);
  if (state.sequencerSteps < 1 || state.sequencerSteps > 16) state.sequencerSteps = 16;
  state.sequencerBPM = storage.getUChar("seqBPM", 120);
  if (state.sequencerBPM < 1 || state.sequencerBPM > 300) state.sequencerBPM = 120;
  state.sequencerCC = storage.getUChar("seqCC", 20);
  state.sequencerScaleIndex = storage.getUChar("seqScale", 0);
  if (state.sequencerScaleIndex > 3) state.sequencerScaleIndex = 0;
  size_t stepsSize = storage.getBytesLength("steps");
  if (stepsSize == sizeof(state.steps)) {
    storage.getBytes("steps", state.steps, sizeof(state.steps));
  }
  storage.end();
  state.displayDirty = true;
}

void saveSequencerSettings() {
  storage.begin("redsea", false);
  storage.putUChar("seqSteps", state.sequencerSteps);
  storage.putUChar("seqBPM", state.sequencerBPM);
  storage.putUChar("seqCC", state.sequencerCC);
  storage.putUChar("seqScale", state.sequencerScaleIndex);
  storage.putBytes("steps", state.steps, sizeof(state.steps));
  storage.end();
}

// ============================================================
// 15. SETUP & LOOP
// ============================================================
void setup() {
  pinMode(Pins::PLAY, INPUT_PULLUP);
  pinMode(Pins::TAP, INPUT_PULLUP);
  pinMode(Pins::PAGE, INPUT_PULLUP);
  pinMode(Pins::ENC_SW, INPUT_PULLUP);
  pinMode(Pins::ENC_A, INPUT_PULLUP);
  pinMode(Pins::ENC_B, INPUT_PULLUP);
  Wire.begin(Pins::SDA, Pins::SCL);
  randomSeed(analogRead(0) + micros());
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("RED SEA");
  display.setCursor(0, 10);
  display.print("BOOTING...");
  display.display();
  lastEncState = (digitalRead(Pins::ENC_A) << 1) | digitalRead(Pins::ENC_B);
  attachInterrupt(digitalPinToInterrupt(Pins::ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(Pins::ENC_B), encoderISR, CHANGE);
  midi.begin(Pins::MIDI_BAUD, SERIAL_8N1, Pins::MIDI_RX, Pins::MIDI_TX);
  loadSettings();
  for (uint8_t i = 0; i < 8; i++) state.frozenBackup[i] = getSnowTargetFrozen(i);
  delay(50);
  display.clearDisplay();
  display.display();
  state.displayDirty = true;
}

void loop() {
  processMIDI();
  updateButtons();
  handleEncoder();
  checkTapPageLongPress();
  checkTapPlayLongPress();

  static uint32_t lastAnimTime = 0;
  if (millis() - lastAnimTime >= 50) {
    lastAnimTime = millis();
    if (state.bypassMode == BypassMode::FREEZE && !state.animationPaused) {
      state.animationPaused = true;
      state.frozenAnimTime = millis() - state.animationTimeOffset;
    } else if (state.bypassMode == BypassMode::OFF && state.animationPaused) {
      state.animationPaused = false;
      state.animationTimeOffset += (millis() - state.animationPauseTime);
    }
    state.displayDirty = true;
  }

  if (state.encLongPressActive) {
    uint32_t elapsed = millis() - state.encLongPressStart;
    if (elapsed >= 3000) {
      if (!state.encLongPressTriggered) {
        resetToDefaults();
        state.encLongPressTriggered = true;
      }
      state.encLongPressActive = false;
      state.encLongPressFrameVisible = false;
      state.displayDirty = true;
    } else if (elapsed >= 1000) {
      state.encLongPressFrameVisible = true;
      uint32_t blinkPeriod = 250;
      if (millis() - state.encLongPressLastBlink >= blinkPeriod) {
        state.encLongPressLastBlink = millis();
        state.displayDirty = true;
      }
    } else {
      state.encLongPressFrameVisible = false;
    }
  } else {
    state.encLongPressFrameVisible = false;
  }

  if (state.tapPageLongPressActive) {
    uint32_t elapsed = millis() - state.tapPageLongPressStart;
    if (elapsed >= 3000) {
      if (!state.tapPageLongPressTriggered) {
        randomizeSequencerAll();
        state.tapPageLongPressTriggered = true;
      }
      state.tapPageLongPressActive = false;
      state.tapPageLongPressFrameVisible = false;
      state.displayDirty = true;
    } else if (elapsed >= 1000) {
      state.tapPageLongPressFrameVisible = true;
      uint32_t blinkPeriod = 250;
      if (millis() - state.tapPageLongPressLastBlink >= blinkPeriod) {
        state.tapPageLongPressLastBlink = millis();
        state.displayDirty = true;
      }
    } else {
      state.tapPageLongPressFrameVisible = false;
    }
  } else {
    state.tapPageLongPressFrameVisible = false;
  }

  if (state.tapPlayLongPressActive) {
    uint32_t elapsed = millis() - state.tapPlayLongPressStart;
    if (elapsed >= 3000) {
      if (!state.tapPlayLongPressTriggered) {
        randomizeSequencerAll();
        state.tapPlayLongPressTriggered = true;
        // Подавляем randomizeCurrentPage() и BYPASS-логику на грядущем
        // отпускании PLAY — этот жест уже полностью отработал здесь.
        state.playWasPressed = false;
      }
      state.tapPlayLongPressActive = false;
      state.tapPlayLongPressFrameVisible = false;
      state.displayDirty = true;
    } else if (elapsed >= 1000) {
      state.tapPlayLongPressFrameVisible = true;
      uint32_t blinkPeriod = 250;
      if (millis() - state.tapPlayLongPressLastBlink >= blinkPeriod) {
        state.tapPlayLongPressLastBlink = millis();
        state.displayDirty = true;
      }
    } else {
      state.tapPlayLongPressFrameVisible = false;
    }
  } else {
    state.tapPlayLongPressFrameVisible = false;
  }

  updateDisplay();

  // Внутренний клок: per-tick логика вызывается строго один раз на
  // настоящий тик, когда generateInternalTicks() возвращает true.
  if (!state.midiRunning && state.sequencerRunning) {
    if (generateInternalTicks()) {
      onClockTick();
    }
  }

  uint32_t now = millis();
  if (now - state.lastSaveTime > 2000) {
    bool needSave = state.needSaveMinMax || state.needSaveCC || state.needSaveStorm || state.needSaveGlobal;
    if (needSave) {
      storage.begin("redsea", false);
      if (state.needSaveMinMax) {
        for (uint8_t i = 0; i < NUM_PARAMS; i++) {
          char keyMin[8], keyMax[8];
          sprintf(keyMin, "min%u", i);
          sprintf(keyMax, "max%u", i);
          storage.putUChar(keyMin, state.params[i].min);
          storage.putUChar(keyMax, state.params[i].max);
        }
        state.needSaveMinMax = false;
      }
      if (state.needSaveCC) {
        char key[8];
        sprintf(key, "cc%u", state.selectedParam);
        storage.putUChar(key, state.params[state.selectedParam].cc);
        state.needSaveCC = false;
      }
      if (state.needSaveStorm) {
        storage.putUChar("chaos", state.chaos);
        storage.putUChar("waveIdx", state.waveIntervalIndex);
        storage.putUChar("weather", (uint8_t)state.weatherMode);
        storage.putUChar("lfoType", state.lfoType);
        storage.putInt("lfoShape", state.lfoShape);
        storage.putInt("lfoPhase", state.lfoPhase);
        storage.putUChar("lfoGlide", state.lfoGlide);
        storage.putUChar("sunRflct", state.sunRflct);
        storage.putUChar("sunArp", state.sunArp);
        storage.putUChar("sunDflct", state.sunDflct);
        storage.putInt("sunBias", state.sunBias);
        storage.putUChar("snowFlake", state.snowFlake);
        storage.putUChar("snowRot", state.snowRotation);
        storage.putUChar("snowFrz", state.snowFrz);
        storage.putInt("snowTime", state.snowTime);
        storage.putUChar("rainDrip", state.rainDrip);
        storage.putUChar("rainWet", state.rainWet);
        storage.putUChar("rainSplsh", state.rainSplsh);
        storage.putBool("rainThndr", state.rainThunder);
        state.needSaveStorm = false;
      }
      if (state.needSaveGlobal) {
        storage.putUChar("midiCh", state.midiChannel);
        storage.putUChar("bypassMode", (uint8_t)state.selectedBypassMode);
        storage.putUChar("seqDest", (uint8_t)state.seqDest);
        storage.putBool("gfx", state.gfxEnabled);
        state.needSaveGlobal = false;
      }
      storage.end();
    }
    state.lastSaveTime = now;
  }
  static uint32_t lastSequencerSave = 0;
  if (now - lastSequencerSave > 5000) {
    saveSequencerSettings();
    lastSequencerSave = now;
  }
}
