/***************************************************
 * Speaker test — desktop pet (ESP8266 NodeMCU + LM386 module)
 *
 * Wiring:
 *   RX (GPIO3) -> 10k -> node -> LM386 module IN
 *                        node -> 1k -> GND, node -> filter cap -> GND
 *   Module VCC -> VIN (5 V), GND -> GND, 100 uF across module VCC/GND
 *   OLED unchanged (SCK=D5 MOSI=D7 CS=D8 DC=D2 RST=D1)
 *
 * Plays a repeating 5 s cycle in two stages:
 *   Stage A  tone() square wave on GPIO3        -> proves amp + wiring
 *   Stage B  I2S delta-sigma on GPIO3 only      -> proves the real audio path
 * The OLED shows which sound is playing. If the display glitches while
 * Stage B plays, there is a pin conflict -- report it.
 *
 * Start with the module's volume pot LOW.
 * Serial is TX-only (115200) because RX is now the audio pin.
 ****************************************************/

#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <core_esp8266_i2s.h>

#define PIN_AUDIO     3            // RX / I2S data out
#define SAMPLE_RATE   32000        // Hz; 1 I2S frame (32 bits) per audio sample = 32x oversampling.
                                   // 32 kHz pushes the sample-rate images to 28-32 kHz, beyond hearing.
#define AMPLITUDE     0.5f         // 0..0.7 of full scale (2nd-order modulator is stable up to ~0.7)

U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /*cs=*/15, /*dc=*/4, /*reset=*/5);

// ---------------------------------------------------------------- sine table
int16_t SINE[257];                 // 257th entry = wrap, for interpolation
static void buildSine() {
  for (int i = 0; i <= 256; i++) SINE[i] = (int16_t)(32767.0f * sinf(2.0f * PI * i / 256.0f));
}

// ---------------------------------------------------------------- delta-sigma encoder
// Second-order 1-bit modulator, 32 output bits per sample. Compared with first order it pushes
// quantisation noise far above the audio band (simulated in-band SNR ~70 dB vs ~30-37 dB),
// which removes the hiss that grew with frequency. The board's RC filter recovers the audio.
static int32_t dsV1 = 0, dsV2 = 0, dsY = 0;
static uint32_t deltaSigma(int16_t s) {
  int32_t x = s;
  uint32_t word = 0;
  for (int i = 0; i < 32; i++) {
    dsV1 += x - dsY;
    dsV2 += dsV1 - dsY;
    word <<= 1;
    if (dsV2 >= 0) { dsY = 32767;  word |= 1; }
    else           { dsY = -32768; }
  }
  return word;
}

// ---------------------------------------------------------------- test program (5 s cycle)
const uint32_t CYCLE = SAMPLE_RATE * 5;
uint32_t sampleIdx = 0;          // position inside the I2S part of the cycle
uint32_t phase = 0;              // oscillator phase accumulator (2^32 = one period)
const char *label = "";
float sweepFreq = 200.0f;        // current sweep frequency
float sweepRatio = 1.0f;         // per-sample multiplier, computed once in setup()

static int16_t osc(float freqHz) {
  phase += (uint32_t)(freqHz * (4294967296.0f / SAMPLE_RATE));
  uint32_t idx = phase >> 24, frac = (phase >> 8) & 0xFFFF;      // linear interpolation between
  int32_t v = SINE[idx] + (((int32_t)(SINE[idx + 1] - SINE[idx]) * (int32_t)frac) >> 16);  // table points
  return (int16_t)(v * AMPLITUDE);
}

// Returns the next Stage B sample and sets the on-screen label.
static int16_t nextSample() {
  float t = (float)sampleIdx / SAMPLE_RATE;
  sampleIdx = (sampleIdx + 1) % CYCLE;
  if (t < 0.5f)  { label = "B1 sine 440 Hz";  return osc(440); }
  if (t < 0.7f)  { label = "B  gap";          return 0; }
  if (t < 2.2f)  { label = "B2 sweep";        // exponential 200 -> 4000 Hz over 1.5 s
                   if (t < 0.7f + 1.0f / SAMPLE_RATE) sweepFreq = 200.0f;   // restart at segment start
                   int16_t v = osc(sweepFreq);
                   sweepFreq *= sweepRatio;                                 // one multiply, no powf()
                   return v; }
  if (t < 2.5f)  { label = "B  gap";          return 0; }
  if (t < 2.62f) { label = "B3 chirp";        return osc(1200 + (t - 2.5f) / 0.12f * 1200); }
  if (t < 2.68f) {                            return 0; }
  if (t < 2.80f) {                            return osc(1400 + (t - 2.68f) / 0.12f * 1400); }
  label = "B  silence";
  return 0;
}

// ---------------------------------------------------------------- display
static void show(const char *stage, const char *what) {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.drawStr(4, 14, "SPEAKER TEST");
  u8g2.drawStr(4, 32, stage);
  u8g2.drawStr(4, 48, what);
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------- Stage A: tone()
static void stageA() {
  Serial.println("Stage A: tone() beeps 440 / 880 / 1320 Hz");
  const int f[] = { 440, 880, 1320 };
  for (int i = 0; i < 3; i++) {
    char buf[24]; snprintf(buf, sizeof buf, "A  tone %d Hz", f[i]);
    show("Stage A: tone()", buf);
    tone(PIN_AUDIO, f[i]);
    delay(250);
    noTone(PIN_AUDIO);
    delay(150);
  }
}

// ---------------------------------------------------------------- Stage B: I2S
static void stageB(uint32_t durationMs) {
  Serial.println("Stage B: I2S delta-sigma on GPIO3 only");
  // enableRx=false, enableTx=true, driveRxClocks=false, driveTxClocks=false
  // -> only GPIO3 (data) is claimed; GPIO15 (OLED CS) and GPIO2 stay untouched.
  if (!i2s_rxtxdrive_begin(false, true, false, false)) {
    Serial.println("I2S begin FAILED");
    return;
  }
  i2s_set_rate(SAMPLE_RATE);
  sampleIdx = 0; phase = 0;

  uint32_t start = millis(), lastDraw = 0, lastReport = millis(), pending = 0;
  uint32_t generated = 0, underflows = 0;
  bool hasPending = false;
  while (millis() - start < durationMs) {
    if (i2s_is_empty()) underflows++;             // DMA ran dry -> driver plays zeros -> static
    // Keep the DMA buffer (8 x 64 words = 16 ms of audio at 32 kHz) full.
    for (;;) {
      if (!hasPending) { pending = deltaSigma(nextSample()); hasPending = true; generated++; }
      if (!i2s_write_sample_nb(pending)) break;   // buffer full
      hasPending = false;
    }
    if (millis() - lastReport >= 1000) {
      Serial.printf("  [%-15s] generated %u samples/s (need %u)  underflows=%u\n",
                    label, generated, SAMPLE_RATE, underflows);
      generated = 0; underflows = 0; lastReport = millis();
    }
    // Redraw at 10 Hz while audio plays: checks the display is unaffected.
    if (millis() - lastDraw >= 100) { show("Stage B: I2S", label); lastDraw = millis(); }
    yield();
  }
  i2s_end();
  pinMode(PIN_AUDIO, OUTPUT);
  digitalWrite(PIN_AUDIO, LOW);
}

// Measures per-sample cost against the real-time budget (1e6 / SAMPLE_RATE us).
static void benchmark() {
  const int N = 2000;
  volatile uint32_t sink = 0;
  float budget = 1e6f / SAMPLE_RATE;
  uint32_t t0 = micros();
  for (int i = 0; i < N; i++) sink += deltaSigma(osc(1000));
  float encUs = (micros() - t0) / (float)N;
  t0 = micros();
  volatile float sinkf = 0;
  for (int i = 0; i < N; i++) sinkf += powf(20.0f, i * 0.0001f);
  float powUs = (micros() - t0) / (float)N;
  Serial.printf("CPU %u MHz | budget %.2f us/sample | osc+encode %.2f us (%.0f%%) | powf %.2f us (%.0f%%)\n",
                ESP.getCpuFreqMHz(), budget, encUs, 100 * encUs / budget, powUs, 100 * powUs / budget);
  phase = 0;
}

void setup() {
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);   // RX is the audio pin now
  Serial.println("\nSpeaker test");

  u8g2.setBusClock(8000000);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  buildSine();
  sweepRatio = powf(20.0f, 1.0f / (1.5f * SAMPLE_RATE));
  pinMode(PIN_AUDIO, OUTPUT);
  benchmark();
}

void loop() {
  stageA();
  delay(400);
  stageB(3500);
  show("pause", "");
  delay(1000);
}
