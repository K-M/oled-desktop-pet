/***************************************************
 * Desktop Pet — autonomous animated eyes, fully offline.
 * Board:   ESP8266 NodeMCU (Arduino core for ESP8266)
 * Display: 1.3" SH1106 128x64, 4-wire hardware SPI
 * Touch:   Adafruit AT42QT1010 breakout (#1374), active-high output
 * Wiring:  OLED  SCK=D5  MOSI=D7  CS=D8  DC=D2  RST=D1  VCC=3V3  GND=GND
 *          Touch OUT=D6  VDD=3V3  GND=GND
 *          Audio RX(GPIO3) -> 10k -> node(1k + cap to GND) -> LM386 IN; module VCC=VIN(5V), 100uF
 * Settings: Tools -> CPU Frequency -> 160 MHz
 * Library: U8g2 (Library Manager). Eye engine adapted from
 *          playfultechnology/esp32-eyes (GPL-3.0), see LICENSE.
 * Tunables: PetConfig.h
 *
 * Mood timeline (time since the finger last lifted):
 *   touching      PETTED     Glee, snapped in 150 ms
 *   0 - 3 min     CONTENT    positive only
 *   3 - 5 min     REGULAR    curious mix, no negatives, faster gaze
 *   > 5 min       NEGLECTED  no positive faces; negative share grows 40% -> 90% by 20 min
 * After power-up the pet starts in REGULAR (never touched yet).
 *
 * Sounds: boot chirp; touch -> happy chirp then purr while held; CONTENT blips (1-2 min);
 * REGULAR "hm?" (2-4 min); NEGLECTED whines every 90 s shrinking to 30 s by 20 min, with
 * "pet me" (SAM voice) at most once per 5 min. Hold the pad 5 s to toggle mute.
 ****************************************************/

#include <ESP8266WiFi.h>
#include "PetConfig.h"
#include "Face.h"
#include "PetAudio.h"

Face *face;

// ---------------------------------------------------------------- mood tables
struct EmotionWeight { eEmotions emotion; float weight; };

const EmotionWeight CONTENT_MIX[] = {
  { eEmotions::Happy,      0.50f },
  { eEmotions::Normal,     0.40f },
  { eEmotions::Sleepy,     0.10f },   // drowsy-relaxed, not sad
};
const EmotionWeight REGULAR_MIX[] = {
  { eEmotions::Normal,     0.25f },
  { eEmotions::Happy,      0.15f },
  { eEmotions::Skeptic,    0.15f },
  { eEmotions::Focused,    0.15f },
  { eEmotions::Surprised,  0.10f },
  { eEmotions::Suspicious, 0.10f },
  { eEmotions::Sleepy,     0.10f },
};
// NEGLECTED = neutral and negative only. Share between the two tables is set by negShare().
const EmotionWeight NEGLECT_NEUTRAL_MIX[] = {
  { eEmotions::Normal,     0.50f },
  { eEmotions::Skeptic,    0.30f },
  { eEmotions::Sleepy,     0.20f },
};
const EmotionWeight NEGLECT_NEGATIVE_MIX[] = {
  { eEmotions::Sad,        0.45f },   // "yearning" faces dominate
  { eEmotions::Worried,    0.35f },
  { eEmotions::Angry,      0.20f },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// ---------------------------------------------------------------- state
enum PetState : uint8_t { PETTED, CONTENT, REGULAR, NEGLECTED };
const char *STATE_NAME[] = { "PETTED", "CONTENT", "REGULAR", "NEGLECTED" };

PetState state = REGULAR;
bool wasTouched = false;
bool everTouched = false;
uint32_t lastTouchMs = 0;

const uint32_t FRAME_US = 1000000UL / TARGET_FPS;
uint32_t nextEmotionAt, nextBlinkAt, nextLookAt, nextFrameUs;
bool pendingDoubleBlink = false;

// sound scheduling + mute
uint32_t nextSoundAt = 0;
uint32_t lastPetMeAt = 0; bool petMeEver = false;
uint32_t pressStartMs = 0;
bool muteToggledThisHold = false;
uint32_t feedbackUntil = 0;              // zzz / note overlay visible until this time

// ---------------------------------------------------------------- helpers
static uint32_t rnd(uint32_t lo, uint32_t hi) { return (uint32_t)random(lo, hi + 1); }
static float rand01() { return random(0, 10000) / 10000.0f; }                 // [0, 1)
static float rndf(float lo, float hi) { return lo + (hi - lo) * rand01(); }
static bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }  // rollover-safe

static eEmotions pick(const EmotionWeight *t, size_t n) {
  float sum = 0;
  for (size_t i = 0; i < n; i++) sum += t[i].weight;
  float r = rand01() * sum, acc = 0;
  for (size_t i = 0; i < n; i++) {
    acc += t[i].weight;
    if (r < acc) return t[i].emotion;
  }
  return t[n - 1].emotion;
}

// Negative share while neglected: linear from NEG_SHARE_START at 5 min to NEG_SHARE_MAX at 20 min.
static float negShare(uint32_t sinceMs) {
  if (sinceMs <= NEGLECT_MS) return NEG_SHARE_START;
  float f = (float)(sinceMs - NEGLECT_MS) / (float)(NEGLECT_FULL_MS - NEGLECT_MS);
  if (f > 1.0f) f = 1.0f;
  return NEG_SHARE_START + (NEG_SHARE_MAX - NEG_SHARE_START) * f;
}

static eEmotions rollFor(PetState s, uint32_t sinceMs) {
  switch (s) {
    case CONTENT:   return pick(CONTENT_MIX, COUNT(CONTENT_MIX));
    case REGULAR:   return pick(REGULAR_MIX, COUNT(REGULAR_MIX));
    case NEGLECTED: return (rand01() < negShare(sinceMs))
                           ? pick(NEGLECT_NEGATIVE_MIX, COUNT(NEGLECT_NEGATIVE_MIX))
                           : pick(NEGLECT_NEUTRAL_MIX, COUNT(NEGLECT_NEUTRAL_MIX));
    default:        return eEmotions::Glee;
  }
}

static uint32_t dwellFor(eEmotions e) {
  if (e == eEmotions::Surprised || e == eEmotions::Angry)
    return rnd(DWELL_SHORT_MIN_MS, DWELL_SHORT_MAX_MS);
  return rnd(DWELL_MIN_MS, DWELL_MAX_MS);
}

// Change expression with a chosen transition time (ms).
static void goTo(eEmotions e, uint32_t transitionMs) {
  face->LeftEye.Transition.Animation.Interval = transitionMs;
  face->RightEye.Transition.Animation.Interval = transitionMs;
  if (e != face->Behavior.CurrentEmotion) face->Behavior.GoToEmotion(e);
}

// Neglect factor 0..1 over 5 -> 20 min untouched.
static float neglectFactor(uint32_t sinceMs) {
  if (sinceMs <= NEGLECT_MS) return 0.0f;
  float f = (float)(sinceMs - NEGLECT_MS) / (float)(NEGLECT_FULL_MS - NEGLECT_MS);
  return f > 1.0f ? 1.0f : f;
}

// Time until the next ambient sound for a state (ms).
static uint32_t soundIntervalFor(PetState s, uint32_t sinceMs) {
  switch (s) {
    case CONTENT:   return rnd(BLIP_MIN_S, BLIP_MAX_S) * 1000UL;
    case REGULAR:   return rnd(CURIOUS_MIN_S, CURIOUS_MAX_S) * 1000UL;
    case NEGLECTED: {
      float f = neglectFactor(sinceMs);
      float base = DEMAND_START_S + (DEMAND_END_S - DEMAND_START_S) * f;   // 90 s -> 30 s
      return (uint32_t)(base * rndf(0.8f, 1.2f) * 1000.0f);               // +-20 % jitter
    }
    default:        return 0xFFFFFFFFUL;
  }
}

static void ambientSound(uint32_t now, uint32_t sinceMs) {
  switch (state) {
    case CONTENT:  PetAudio::play(SND_BLIP); break;
    case REGULAR:  PetAudio::play(SND_CURIOUS); break;
    case NEGLECTED:
      if (!petMeEver || now - lastPetMeAt >= PETME_MIN_INTERVAL_S * 1000UL) {
        if (PetAudio::play(SND_PETME)) { lastPetMeAt = now; petMeEver = true; }
      } else {
        PetAudio::play(SND_WHINE);
      }
      break;
    default: break;
  }
  nextSoundAt = now + soundIntervalFor(state, sinceMs);
}

// ---------------------------------------------------------------- overlay (drawn over the eyes)
static void drawNote(int x, int y) {          // small eighth note, head at (x, y)
  u8g2.drawDisc(x, y, 3);
  u8g2.drawVLine(x + 3, y - 12, 12);
  u8g2.drawLine(x + 3, y - 12, x + 8, y - 8);
}

static void overlay() {
  uint32_t now = millis();
  if ((int32_t)(feedbackUntil - now) > 0) {
    uint32_t t = MUTE_FEEDBACK_MS - (feedbackUntil - now);            // ms since toggle
    int rise = (int)(t / 120) % 8;                                     // gentle float upwards
    if (PetAudio::muted()) {
      u8g2.setFont(u8g2_font_6x10_tf);  u8g2.drawStr(104, 22 - rise, "z");
      u8g2.setFont(u8g2_font_7x13B_tf); u8g2.drawStr(110, 14 - rise, "Z");
      u8g2.setFont(u8g2_font_6x10_tf);  u8g2.drawStr(119, 8 - rise / 2, "z");
    } else {
      drawNote(110, 20 - rise);
    }
  } else if (MUTE_INDICATOR && PetAudio::muted()) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(122, 7, "z");
  }
}

static void setState(PetState s) {
  if (s == state) return;
  state = s;
#if DEBUG_STATS
  Serial.printf("[%lu s] state -> %s\n", millis() / 1000, STATE_NAME[s]);
#endif
}

// ---------------------------------------------------------------- setup
void setup() {
  // Always offline: radio fully off (~70 mA saved, no RF interrupts disturbing frame timing).
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);   // RX (GPIO3) is the audio output
  randomSeed(RANDOM_REG32 ^ micros());   // ESP8266 hardware RNG register

  face = new Face(128, 64, EYE_SIZE);   // starts the display: SPI.begin() claims GPIO12 as MISO

  // Must come AFTER the display starts. D6/GPIO12 is the hardware-SPI MISO pin; SPI.begin()
  // switches it to the SPI function, so a pinMode() before that is silently overridden.
  // The display is write-only, so reclaiming MISO as a plain input is safe.
  pinMode(PIN_TOUCH, INPUT);             // QT1010 drives the line push-pull

  // The scheduler below replaces the library's fixed-interval behaviour, blink and look timers.
  face->Overlay = overlay;
  face->RandomBehavior = false;
  face->RandomBlink = false;
  face->RandomLook = false;

  // Eyes open from closed on boot. Call GoToEmotion directly: CurrentEmotion is uninitialised
  // at this point, so goTo()'s "already showing it" check could wrongly skip the transition.
  face->LeftEye.Transition.Animation.Interval = NORMAL_TRANSITION_MS;
  face->RightEye.Transition.Animation.Interval = NORMAL_TRANSITION_MS;
  face->Behavior.GoToEmotion(eEmotions::Normal);

  uint32_t now = millis();
  lastTouchMs   = now;                   // neglect clock starts at power-up
  nextEmotionAt = now + dwellFor(eEmotions::Normal);
  nextBlinkAt   = now + rnd(BLINK_MIN_MS, BLINK_MAX_MS);
  nextLookAt    = now + rnd(LOOK_MIN_MS, LOOK_MAX_MS);
  nextFrameUs   = micros();

  PetAudio::begin();                     // GPIO3 only; soft ramp avoids a power-on pop
  PetAudio::play(SND_BOOT);
  nextSoundAt   = now + soundIntervalFor(REGULAR, 0);
}

// ---------------------------------------------------------------- loop
void loop() {
  PetAudio::service();                                       // keep the 16 ms audio buffer full

  // ---- Fixed frame cadence ----
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextFrameUs) < 0) return;            // not time yet
  nextFrameUs += FRAME_US;
  if ((int32_t)(nowUs - nextFrameUs) > (int32_t)FRAME_US)    // fell >1 frame behind: resync
    nextFrameUs = nowUs + FRAME_US;

  uint32_t now = millis();

  // ---- Touch edges (sampled once per frame = every 16.7 ms) ----
  bool touched = digitalRead(PIN_TOUCH) == HIGH;
  if (touched && !wasTouched) {                              // finger down
    setState(PETTED);
    goTo(eEmotions::Glee, TOUCH_TRANSITION_MS);
    face->Look.LookAt(0.0f, 0.0f);                           // look at you
    PetAudio::play(SND_HAPPY);
    pressStartMs = now;
    muteToggledThisHold = false;
  } else if (!touched && wasTouched) {                       // finger up
    everTouched = true;
    lastTouchMs = now;
    setState(CONTENT);
    nextEmotionAt = now + GLEE_AFTER_RELEASE_MS;             // linger in Glee briefly
    if ((int32_t)(feedbackUntil - now) > 0) nextEmotionAt = feedbackUntil;  // keep mute feedback face
    nextSoundAt = now + soundIntervalFor(CONTENT, 0);
    PetAudio::release();                                     // purr fades out
  }
  wasTouched = touched;

  // ---- While held: purr, and a 5 s hold toggles mute (once per hold) ----
  if (touched) {
    if (!muteToggledThisHold && now - pressStartMs >= MUTE_HOLD_MS) {
      muteToggledThisHold = true;
      bool m = !PetAudio::muted();
      if (m) { PetAudio::play(SND_MUTE); PetAudio::setMuted(true); }   // MUTE tone still plays
      else   { PetAudio::setMuted(false); PetAudio::play(SND_UNMUTE); }
      goTo(m ? eEmotions::Sleepy : eEmotions::Glee, TOUCH_TRANSITION_MS);
      feedbackUntil = now + MUTE_FEEDBACK_MS;
#if DEBUG_STATS
      Serial.printf("[%lu s] %s\n", now / 1000, m ? "MUTED" : "UNMUTED");
#endif
    }
    if (!PetAudio::busy()) PetAudio::play(SND_PURR);         // after the chirp; blocked when muted
  }

  // ---- Mood state from time since last touch ----
  uint32_t since = now - lastTouchMs;
  if (!touched) {
    PetState s = (everTouched && since < CONTENT_MS) ? CONTENT
               : (since < NEGLECT_MS)                ? REGULAR
                                                     : NEGLECTED;
    if (s != state) {
      setState(s);
      nextEmotionAt = now;                                   // re-roll now: no leftover face from the old state
      nextSoundAt = now + ((s == NEGLECTED) ? 20000UL        // first demand 20 s into neglect
                                            : soundIntervalFor(s, since));
    }
  }

  // ---- Emotion re-roll ----
  if (state != PETTED && due(now, nextEmotionAt) && (int32_t)(feedbackUntil - now) <= 0) {
    eEmotions e = rollFor(state, since);
    goTo(e, NORMAL_TRANSITION_MS);
    nextEmotionAt = now + dwellFor(e);
  }

  // ---- Ambient sounds per state ----
  if (state != PETTED && due(now, nextSoundAt)) ambientSound(now, since);

  // ---- Blink: random interval, occasional double blink ----
  if (due(now, nextBlinkAt)) {
    face->DoBlink();
    if (!pendingDoubleBlink && (int)random(100) < DOUBLE_BLINK_PCT) {
      pendingDoubleBlink = true;
      nextBlinkAt = now + DOUBLE_BLINK_GAP_MS;
    } else {
      pendingDoubleBlink = false;
      nextBlinkAt = now + rnd(BLINK_MIN_MS, BLINK_MAX_MS);
    }
  }

  // ---- Gaze: holds on you while petted; faster wandering when curious ----
  if (state != PETTED && due(now, nextLookAt)) {
    if ((int)random(100) < LOOK_CENTER_PCT) face->Look.LookAt(0.0f, 0.0f);
    else face->Look.LookAt(rndf(-0.7f, 0.7f), rndf(-0.5f, 0.5f));
    nextLookAt = now + ((state == REGULAR) ? rnd(LOOK_CURIOUS_MIN_MS, LOOK_CURIOUS_MAX_MS)
                                           : rnd(LOOK_MIN_MS, LOOK_MAX_MS));
  }

  // ---- Render + push one frame ----
#if DEBUG_STATS
  uint32_t t0 = micros();
#endif
  face->Update();
  PetAudio::service();                                       // refill right after the ~5 ms frame push
#if DEBUG_STATS
  static uint32_t frames = 0, sumUs = 0, maxUs = 0, lastReport = 0;
  uint32_t dt = micros() - t0;
  frames++; sumUs += dt; if (dt > maxUs) maxUs = dt;
  if (now - lastReport >= 5000) {
    Serial.printf("fps=%.1f  frame avg=%u us  max=%u us  budget=%u us  state=%s  audio_underflows=%u%s\n",
                  frames * 1000.0f / (now - lastReport), sumUs / frames, maxUs, FRAME_US,
                  STATE_NAME[state], PetAudio::takeUnderflows(), PetAudio::muted() ? "  MUTED" : "");
    frames = 0; sumUs = 0; maxUs = 0; lastReport = now;
  }
#endif
}
