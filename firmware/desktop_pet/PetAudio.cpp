// PetAudio.cpp — see PetAudio.h.
#include "PetAudio.h"
#include "PetConfig.h"
#include "pet_me.h"
#include <core_esp8266_i2s.h>

namespace {

// ------------------------------------------------------------ sound definitions
// A sound is a short sequence of notes. ms == 0 marks a looping note (held until release()).
struct Note {
  uint16_t ms;        // duration (0 = loop)
  uint16_t f0, f1;    // start/end frequency, Hz (exponential glide); 0 = rest
  uint8_t  gain;      // 0..100 % of MASTER_VOLUME
  uint8_t  atkMs;     // linear attack
  uint8_t  relMs;     // linear release at note end (or after release() for loops)
  uint8_t  vibHz;     // vibrato rate (0 = off)
  uint8_t  vibPct;    // vibrato depth, % of frequency
  uint8_t  amHz;      // amplitude-modulation rate (0 = off) — gives the purr its "rrr"
};

const Note N_BOOT[]    = { {90, 500, 900, 70, 5, 20, 0,0,0}, {30, 0,0,0,0,0,0,0,0},
                           {130, 900, 1800, 80, 5, 70, 0,0,0} };
const Note N_HAPPY[]   = { {110, 1200, 2400, 90, 3, 30, 0,0,0}, {50, 0,0,0,0,0,0,0,0},
                           {120, 1400, 2800, 90, 3, 50, 0,0,0} };
const Note N_PURR[]    = { {0, 210, 210, 55, 120, 200, 0,0, 24} };
const Note N_BLIP[]    = { {70, 1600, 1450, 35, 2, 60, 0,0,0} };
const Note N_CURIOUS[] = { {110, 620, 620, 50, 8, 30, 0,0,0}, {40, 0,0,0,0,0,0,0,0},
                           {230, 650, 1150, 60, 10, 60, 6, 3, 0} };
const Note N_WHINE[]   = { {700, 1400, 650, 70, 30, 200, 7, 4, 0} };
const Note N_MUTE[]    = { {120, 900, 450, 60, 5, 40, 0,0,0} };
const Note N_UNMUTE[]  = { {110, 450, 900, 60, 5, 30, 0,0,0}, {90, 900, 1300, 60, 5, 50, 0,0,0} };

struct SoundDef { const Note *notes; uint8_t count; uint8_t prio; };
#define SD(arr, p) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), p }
// Priority: a new sound interrupts the current one only if its priority is >= current.
const SoundDef SOUNDS[SND_COUNT] = {
  { nullptr, 0, 0 },     // NONE
  SD(N_BOOT, 1),
  SD(N_HAPPY, 4),
  SD(N_PURR, 3),
  SD(N_BLIP, 1),
  SD(N_CURIOUS, 1),
  SD(N_WHINE, 2),
  { nullptr, 0, 2 },     // PETME (sample playback)
  SD(N_MUTE, 5),
  SD(N_UNMUTE, 5),
};

// "pet me": single clip in pet_me.h (PETME_DATA / PETME_LEN)

// ------------------------------------------------------------ engine state
const uint32_t SR = AUDIO_SAMPLE_RATE;
const uint32_t IDLE_WORD = 0xAAAAAAAAUL;          // 50 % density = mid-level silence, no encoding cost
const uint32_t RAMP_SAMPLES = SR / 6;             // ~170 ms soft start from 0 V to mid-level

int16_t SINE[257];
bool started = false, mutedFlag = false;
uint32_t underflowCount = 0;
uint32_t pending = 0; bool hasPending = false;
uint32_t rampPos = 0; uint32_t rampAcc = 0;

// delta-sigma (2nd order) state
int32_t dsV1 = 0, dsV2 = 0, dsY = 0;

// voice state
SoundId cur = SND_NONE;
uint8_t noteIdx = 0;
uint32_t noteN = 0, noteLen = 0, atkN = 1, relN = 1;
bool looping = false, releasing = false;
uint32_t relLeft = 0;
float freq = 0, ratio = 1, gainT = 0, env = 0;    // env = smoothed output gain (de-click)
uint32_t oscPhase = 0, lfoPhase = 0, amPhase = 0;
uint32_t samplePos = 0;                           // 16.16 fixed position into PETME_DATA
const uint32_t SAMPLE_STEP = (uint32_t)((22050.0 * 65536.0) / AUDIO_SAMPLE_RATE);
bool fadingOut = false;                           // stop(): fade env to 0, then go idle

inline int16_t sineAt(uint32_t ph) {
  uint32_t i = ph >> 24, fr = (ph >> 8) & 0xFFFF;
  return SINE[i] + (int16_t)(((int32_t)(SINE[i + 1] - SINE[i]) * (int32_t)fr) >> 16);
}
inline uint32_t incFor(float hz) { return (uint32_t)(hz * (4294967296.0f / SR)); }

uint32_t deltaSigma(int16_t s) {
  int32_t x = s;
  uint32_t w = 0;
  for (int i = 0; i < 32; i++) {
    dsV1 += x - dsY;
    dsV2 += dsV1 - dsY;
    w <<= 1;
    if (dsV2 >= 0) { dsY = 32767; w |= 1; } else { dsY = -32768; }
  }
  return w;
}

// 1st-order encoder for the start-up ramp only (stable across the full 0..50 % range).
uint32_t rampWord() {
  uint32_t target = (uint32_t)((uint64_t)rampPos * 32768 / RAMP_SAMPLES);   // 0 -> 32768 of 65536
  uint32_t w = 0;
  for (int i = 0; i < 32; i++) {
    rampAcc += target; w <<= 1;
    if (rampAcc >= 65536) { rampAcc -= 65536; w |= 1; }
  }
  rampPos++;
  return w;
}

void startNote() {
  const Note &n = SOUNDS[cur].notes[noteIdx];
  noteN = 0;
  looping = (n.ms == 0);
  noteLen = looping ? 0xFFFFFFFFUL : (uint32_t)n.ms * SR / 1000;
  atkN = max<uint32_t>(1, (uint32_t)n.atkMs * SR / 1000);
  relN = max<uint32_t>(1, (uint32_t)n.relMs * SR / 1000);
  freq = n.f0;
  ratio = (looping || n.f0 == 0 || n.f1 == n.f0) ? 1.0f : powf((float)n.f1 / n.f0, 1.0f / noteLen);
  gainT = n.gain / 100.0f;
  releasing = false;
}

void finish() { cur = SND_NONE; fadingOut = false; }

// One output sample of the current synth sound (pre-volume, -1..1 scaled to int16).
int16_t synthSample() {
  const Note &n = SOUNDS[cur].notes[noteIdx];
  float e = 0;
  if (n.f0 != 0) {
    e = gainT;
    if (noteN < atkN) e *= (float)noteN / atkN;
    if (!looping) {
      uint32_t left = noteLen - noteN;
      if (left < relN) e *= (float)left / relN;
    } else if (releasing) {
      e *= (float)relLeft / relN;
    }
    if (n.amHz) {                                   // purr: 0.25..1.0 amplitude wobble
      amPhase += incFor(n.amHz);
      e *= 0.625f + 0.375f * (sineAt(amPhase) / 32767.0f);
    }
  }
  if (fadingOut) e = 0;
  env += (e - env) * 0.02f;                         // ~1.5 ms smoothing: removes clicks on jumps

  float f = freq;
  if (n.vibHz) {
    lfoPhase += incFor(n.vibHz);
    f *= 1.0f + (n.vibPct / 100.0f) * (sineAt(lfoPhase) / 32767.0f);
  }
  if (n.f0 != 0) oscPhase += incFor(f);             // phase continues across notes/sounds
  freq *= ratio;

  int16_t out = (int16_t)(sineAt(oscPhase) * env * MASTER_VOLUME);

  // advance timeline
  noteN++;
  if (fadingOut) { if (env < 0.001f) finish(); return out; }
  if (looping) {
    if (releasing) { if (relLeft == 0) finish(); else relLeft--; }
  } else if (noteN >= noteLen) {
    if (++noteIdx >= SOUNDS[cur].count) { cur = SND_NONE; }
    else startNote();
  }
  return out;
}

int16_t speechSample() {
  uint32_t i = samplePos >> 16;
  if (i + 1 >= PETME_LEN || fadingOut) { finish(); return 0; }
  int32_t s0 = (int32_t)pgm_read_byte(&PETME_DATA[i]) - 128;
  int32_t s1 = (int32_t)pgm_read_byte(&PETME_DATA[i + 1]) - 128;
  int32_t v = s0 + (((s1 - s0) * (int32_t)(samplePos & 0xFFFF)) >> 16);
  samplePos += SAMPLE_STEP;
  return (int16_t)(v * 256 * (PETME_GAIN * MASTER_VOLUME));
}

uint32_t nextWord() {
  if (rampPos < RAMP_SAMPLES) return rampWord();
  if (cur == SND_NONE) {
    if (env > 0.0005f) { env *= 0.98f; return deltaSigma(0); }   // settle encoder after a sound
    return IDLE_WORD;
  }
  int16_t s = (cur == SND_PETME) ? speechSample() : synthSample();
  return deltaSigma(s);
}

} // namespace

// ------------------------------------------------------------ public API
namespace PetAudio {

void begin() {
#if AUDIO_ENABLED
  for (int i = 0; i <= 256; i++) SINE[i] = (int16_t)(32767.0f * sinf(2.0f * PI * i / 256.0f));
  // enableRx=false, enableTx=true, driveRxClocks=false, driveTxClocks=false:
  // claims only GPIO3 (I2S data). ESP8266Audio would also claim GPIO15 = OLED CS.
  if (!i2s_rxtxdrive_begin(false, true, false, false)) return;
  i2s_set_rate(SR);
  started = true;
  rampPos = 0; rampAcc = 0;
  service();
#endif
}

void service() {
  if (!started) return;
  if (i2s_is_empty()) underflowCount++;
  for (;;) {
    if (!hasPending) { pending = nextWord(); hasPending = true; }
    if (!i2s_write_sample_nb(pending)) break;
    hasPending = false;
  }
}

bool play(SoundId id) {
  if (!started || id == SND_NONE || id >= SND_COUNT) return false;
  bool feedback = (id == SND_MUTE || id == SND_UNMUTE);
  if (mutedFlag && !feedback) return false;
  if (cur != SND_NONE && !fadingOut && SOUNDS[id].prio < SOUNDS[cur].prio) return false;
  cur = id; fadingOut = false;
  if (id == SND_PETME) { samplePos = 0; return true; }
  noteIdx = 0;
  startNote();
  return true;
}

void release() {
  if (cur != SND_NONE && cur != SND_PETME && looping && !releasing) { releasing = true; relLeft = relN; }
}

void stop() { if (cur != SND_NONE) fadingOut = true; }
bool busy() { return cur != SND_NONE; }
SoundId current() { return cur; }

void setMuted(bool m) {
  mutedFlag = m;
  if (m && cur != SND_MUTE) stop();
}
bool muted() { return mutedFlag; }

uint32_t takeUnderflows() { uint32_t u = underflowCount; underflowCount = 0; return u; }

} // namespace PetAudio
