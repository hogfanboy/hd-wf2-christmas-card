// Pocket Jumbotron - WF2 color/pin test.
// Cycles through test screens so we can check pin mapping, orientation and color depth.
// Board: Huidu HD-WF2 (ESP32-S3, no PSRAM), panel 128x64 1/32 scan HUB75E on connector X1.
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// HD-WF2 HUB75 pinout (from ESP32-HUB75-MatrixPanel-DMA issue #433 / WLED / ESPHome presets).
#define R1_PIN 2
#define G1_PIN 6
#define B1_PIN 10
#define R2_PIN 3
#define G2_PIN 7
#define B2_PIN 11
#define A_PIN 39
#define B_PIN 38
#define C_PIN 37
#define D_PIN 36
#define E_PIN 21
#define LAT_PIN 33
#define OE_PIN 35
#define CLK_PIN 34

static const int PANEL_W = 128, PANEL_H = 64;
static const uint8_t BRIGHTNESS = 140;  // ~55% of 255: protects the barrel jack / supply.

MatrixPanel_I2S_DMA *dma = nullptr;

static uint8_t gamma8(uint8_t v) {  // gamma 2.2 so gradients look even
  return (uint8_t)(powf(v / 255.0f, 2.2f) * 255.0f + 0.5f);
}
static void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  dma->drawPixelRGB888(x, y, gamma8(r), gamma8(g), gamma8(b));
}

static void hsv(float h, uint8_t &r, uint8_t &g, uint8_t &b) {
  float f = h * 6; int i = (int)f; f -= i;
  uint8_t q = 255 * (1 - f), t = 255 * f;
  switch (i % 6) { case 0: r=255;g=t;b=0;break; case 1: r=q;g=255;b=0;break; case 2: r=0;g=255;b=t;break;
                   case 3: r=0;g=q;b=255;break; case 4: r=t;g=0;b=255;break; default: r=255;g=0;b=q; }
}

// Screen 1: corner markers + border. Red top-left, green top-right, blue bottom-left, white bottom-right.
static void screenOrientation() {
  dma->clearScreen();
  for (int x = 0; x < PANEL_W; x++) { px(x, 0, 80, 80, 80); px(x, PANEL_H - 1, 80, 80, 80); }
  for (int y = 0; y < PANEL_H; y++) { px(0, y, 80, 80, 80); px(PANEL_W - 1, y, 80, 80, 80); }
  dma->fillRect(2, 2, 12, 12, dma->color565(255, 0, 0));
  dma->fillRect(PANEL_W - 14, 2, 12, 12, dma->color565(0, 255, 0));
  dma->fillRect(2, PANEL_H - 14, 12, 12, dma->color565(0, 0, 255));
  dma->fillRect(PANEL_W - 14, PANEL_H - 14, 12, 12, dma->color565(255, 255, 255));
  dma->setTextColor(dma->color565(255, 190, 60));
  dma->setTextSize(1);
  dma->setCursor(34, 22); dma->print("POCKET");
  dma->setCursor(25, 34); dma->print("JUMBOTRON");
}

// Screen 2: 8 color bars (checks every R/G/B line on both halves).
static void screenBars() {
  const uint8_t c[8][3] = {{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}};
  for (int x = 0; x < PANEL_W; x++) { const uint8_t *k = c[x / 16]; for (int y = 0; y < PANEL_H; y++) px(x, y, k[0], k[1], k[2]); }
}

// Screen 3: smooth gradients - red, green, blue, white bands. Banding here = low color depth.
static void screenGradients() {
  for (int x = 0; x < PANEL_W; x++) {
    uint8_t v = x * 255 / (PANEL_W - 1);
    for (int y = 0; y < 16; y++) { px(x, y, v, 0, 0); px(x, y + 16, 0, v, 0); px(x, y + 32, 0, 0, v); px(x, y + 48, v, v, v); }
  }
}

// Screen 4: full HSV rainbow, hue across, brightness down.
static void screenRainbow() {
  for (int x = 0; x < PANEL_W; x++) for (int y = 0; y < PANEL_H; y++) {
    uint8_t r, g, b; hsv((float)x / PANEL_W, r, g, b);
    float k = 1.0f - (float)y / PANEL_H * 0.85f;
    px(x, y, r * k, g * k, b * k);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nPocket Jumbotron WF2 color test");

  HUB75_I2S_CFG::i2s_pins pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
                                  A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, 1, pins);
  cfg.clkphase = false;
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) {
    Serial.println("DMA begin FAILED (not enough memory?)");
    for (;;) delay(1000);
  }
  dma->setBrightness8(BRIGHTNESS);
  dma->clearScreen();
  Serial.printf("Panel ready %dx%d, free heap %u bytes\n", PANEL_W, PANEL_H, (unsigned)ESP.getFreeHeap());
}

void loop() {
  static uint8_t screen = 0;
  const char *names[] = {"orientation", "bars", "gradients", "rainbow"};
  Serial.printf("Screen %u: %s\n", screen, names[screen]);
  switch (screen) {
    case 0: screenOrientation(); break;
    case 1: screenBars(); break;
    case 2: screenGradients(); break;
    case 3: screenRainbow(); break;
  }
  screen = (screen + 1) % 4;
  delay(5000);
}
