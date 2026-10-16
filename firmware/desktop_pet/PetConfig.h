// PetConfig.h — every tunable for the desktop pet lives here.
#ifndef _PETCONFIG_h
#define _PETCONFIG_h

// ---- Display: SH1106 128x64, 4-wire hardware SPI, ESP8266 NodeMCU ----
// SCK = D5 (GPIO14) and MOSI = D7 (GPIO13) are fixed by the hardware SPI block.
#define PIN_OLED_CS   15        // D8
#define PIN_OLED_DC    4        // D2
#define PIN_OLED_RST   5        // D1
#define OLED_SPI_HZ   8000000UL // 8 MHz, verified working with the test sketch

// ---- Frame pacing ----
#define TARGET_FPS    60        // fixed cadence; measured headroom is ~200 fps

// ---- Face geometry ----
#define EYE_SIZE      40        // px, matches the library presets

// ---- Emotion dwell time (ms) before the next re-roll ----
#define DWELL_MIN_MS        6000
#define DWELL_MAX_MS       10000
#define DWELL_SHORT_MIN_MS  2000  // transient emotions (Surprised, Angry)
#define DWELL_SHORT_MAX_MS  4000

// ---- Blinking (ms between blinks) ----
#define BLINK_MIN_MS        2000
#define BLINK_MAX_MS        6000
#define DOUBLE_BLINK_PCT      15  // % chance a blink is followed by a second one
#define DOUBLE_BLINK_GAP_MS  250

// ---- Gaze (ms between eye movements) ----
#define LOOK_MIN_MS         1500
#define LOOK_MAX_MS         4500
#define LOOK_CENTER_PCT       40  // % chance the pet looks back to centre

// ---- Touch sensor: Adafruit AT42QT1010 breakout (active-high, momentary) ----
#define PIN_TOUCH          12     // D6 (also HSPI MISO; reclaimed as input after display init)
#define TOUCH_TRANSITION_MS 150   // snap to Glee on touch (normal blend is 500 ms)
#define NORMAL_TRANSITION_MS 500
#define GLEE_AFTER_RELEASE_MS 2500 // keep Glee this long after the finger lifts

// ---- Mood timeline, measured from the last touch release ----
#define CONTENT_MS        180000UL  // 0-3 min: positive only
#define NEGLECT_MS        300000UL  // 3-5 min: curious; >5 min: neglected
#define NEGLECT_FULL_MS  1200000UL  // negative share stops growing at 20 min
#define NEG_SHARE_START       0.40f // negative share of rolls when neglect begins
#define NEG_SHARE_MAX         0.90f // negative share at NEGLECT_FULL_MS and beyond

// ---- Curious gaze (Regular state) ----
#define LOOK_CURIOUS_MIN_MS 1000
#define LOOK_CURIOUS_MAX_MS 2500

// ---- Audio: LM386 module fed from RX (GPIO3) through the RC filter ----
// Set Tools -> CPU Frequency -> 160 MHz (measured: 16 % CPU while a sound plays, vs 32 % at 80 MHz).
#define AUDIO_ENABLED          1
#define AUDIO_SAMPLE_RATE  32000    // Hz; verified clean, 0 underflows
#define MASTER_VOLUME       0.5f    // 0..0.7 (2nd-order modulator stable up to ~0.7)
#define PETME_GAIN          0.9f    // relative loudness of the spoken "pet me"

// ---- Sound behaviour (seconds) ----
#define BLIP_MIN_S            60    // CONTENT: quiet blip every 1-2 min
#define BLIP_MAX_S           120
#define CURIOUS_MIN_S        120    // REGULAR: "hm?" every 2-4 min
#define CURIOUS_MAX_S        240
#define DEMAND_START_S        90    // NEGLECTED: whine interval at 5 min untouched...
#define DEMAND_END_S          30    // ...shrinking to this by 20 min
#define PETME_MIN_INTERVAL_S 300    // "pet me" at most once per 5 min

// ---- Mute: hold the touch pad this long to toggle ----
#define MUTE_HOLD_MS        5000
#define MUTE_FEEDBACK_MS    2500    // zzz / note overlay duration
#define MUTE_INDICATOR         1    // small "z" in the corner while muted

// ---- Serial frame statistics (115200 baud, every 5 s). 0 to disable. ----
#define DEBUG_STATS           1

#endif
