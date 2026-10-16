// OLED driver test — SH1106 (use this to confirm your controller; swap the constructor for SSD1309 if the image is shifted) (128x64, 4-wire hardware SPI) on ESP8266 NodeMCU
// Library: U8g2 (Library Manager -> "U8g2" by oliver)
// Wiring: SCK=D5(14)  MOSI=D7(13)  CS=D8(15)  DC=D2(4)  RST=D1(5)
//
// PASS = a 1-px border touching all four screen edges, no noise column on the
//        left/right, the label readable, and a square sweeping smoothly.

#include <U8g2lib.h>
#include <ESP8266WiFi.h>

U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /*cs=*/15, /*dc=*/4, /*reset=*/5);

const char *LABEL = "SH1106";

void setup() {
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  Serial.begin(115200);
  u8g2.setBusClock(8000000);   // 8 MHz SPI
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
}

void loop() {
  static int x = 0, dx = 2;
  static uint32_t frames = 0, tSum = 0, lastPrint = 0;

  uint32_t t0 = micros();
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);         // edge-alignment check
  u8g2.drawStr(4, 12, LABEL);
  u8g2.drawBox(x, 30, 16, 16);           // motion-smoothness check
  u8g2.sendBuffer();
  tSum += micros() - t0;
  frames++;

  x += dx;
  if (x <= 1 || x >= 128 - 17) dx = -dx;

  if (millis() - lastPrint >= 1000) {
    Serial.printf("%s  fps=%u  avg frame=%u us\n", LABEL, frames, tSum / frames);
    frames = 0; tSum = 0; lastPrint = millis();
  }
}
