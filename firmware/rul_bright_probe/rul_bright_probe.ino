// RUL6024 brightness-register probe. The panel runs normally (library DP3246 mode), and every 8 s we
// briefly take the HUB75 pins back as plain GPIO, write the driver chips' hidden config registers with a
// different recipe, then hand the pins back to the DMA. Watch which label (A-D) looks brightest.
//   A: library default (DP3246 recipe: REG1 at LAT 11 clocks, REG2 at 12, gain max)
//   B: FM6126A/ICN2038S recipe (REG1 at LAT 12, REG2 at 13), library's FM6126A values
//   C: same as B, but REG1 all ones (max)
//   D: DP3246 recipe written again (should look like A)
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

static const uint8_t R1 = 2, G1 = 6, B1_ = 10, R2 = 3, G2 = 7, B2 = 11, PA = 39, PB = 38, PC = 37, PD = 36, PE = 21,
                     LAT = 33, OE = 35, CLK = 34;
static const int W = 128, H = 64;
MatrixPanel_I2S_DMA *dma;

static void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) { dma->drawPixelRGB888((x + 1) % W, y, r, g, b); }

static void drawTest(const char *label) {
  dma->clearScreen();
  for (int x = 0; x < W; x++) {
    uint8_t v = x * 255 / (W - 1);
    for (int y = 0; y < 14; y++) px(x, y, 255, 255, 255);          // full white
    for (int y = 16; y < 26; y++) px(x, y, 255, 0, 0);
    for (int y = 26; y < 36; y++) px(x, y, 0, 255, 0);
    for (int y = 36; y < 46; y++) px(x, y, 0, 0, 255);
    for (int y = 48; y < 64; y++) px(x, y, v, v, v);               // gray ramp
  }
  dma->fillRect(2, 50, 14, 12, 0);
  dma->setTextColor(dma->color565(255, 190, 60));
  dma->setCursor(6, 53); dma->print(label);
}

// Take pins back from the LCD peripheral, bit-bang one register write, give them back.
static const uint8_t STEAL[] = {R1, G1, B1_, R2, G2, B2, CLK, LAT, OE};
static void regWrite(const bool reg[16], int latClocks) {
  uint32_t saved[sizeof(STEAL)];
  gpio_set_level((gpio_num_t)OE, 1); gpio_set_level((gpio_num_t)LAT, 0); gpio_set_level((gpio_num_t)CLK, 0);
  for (int i = 0; i < (int)sizeof(STEAL); i++) {
    saved[i] = GPIO.func_out_sel_cfg[STEAL[i]].val;
    GPIO.func_out_sel_cfg[STEAL[i]].val = 0x100;                    // SIG_GPIO_OUT_IDX: plain GPIO output
  }
  for (int l = 0; l < W; l++) {
    for (uint8_t p : {R1, G1, B1_, R2, G2, B2}) gpio_set_level((gpio_num_t)p, reg[l % 16]);
    if (l == W - latClocks) gpio_set_level((gpio_num_t)LAT, 1);
    gpio_set_level((gpio_num_t)CLK, 1); gpio_set_level((gpio_num_t)CLK, 0);
  }
  gpio_set_level((gpio_num_t)LAT, 0);
  for (uint8_t p : {R1, G1, B1_, R2, G2, B2}) gpio_set_level((gpio_num_t)p, 0);
  gpio_set_level((gpio_num_t)CLK, 1); gpio_set_level((gpio_num_t)CLK, 0);
  for (int i = 0; i < (int)sizeof(STEAL); i++) GPIO.func_out_sel_cfg[STEAL[i]].val = saved[i];
}

static const bool DP_REG1[16] = {0,0,0, 0,0,0,0, 0, 1,1,1,1,1,1,1,1};
static const bool DP_REG2[16] = {1,1,1,1,1, 1,1,1, 0, 0,0,0,0, 0,0,0};
static const bool FM_REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
static const bool FM_REG1_MAX[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static const bool FM_REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nRUL6024 brightness-register probe");
  HUB75_I2S_CFG::i2s_pins pins = {R1, G1, B1_, R2, G2, B2, PA, PB, PC, PD, PE, LAT, OE, CLK};
  HUB75_I2S_CFG cfg(W, H, 1, pins);
  cfg.driver = HUB75_I2S_CFG::DP3246;
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) { Serial.println("DMA begin FAILED"); for (;;) delay(1000); }
  dma->setBrightness8(140);
}

void loop() {
  Serial.printf("%7lu ms  A: library default\n", millis());
  drawTest("A"); delay(8000);

  Serial.printf("%7lu ms  B: FM6126A recipe (LAT 12/13)\n", millis());
  regWrite(FM_REG1, 12); regWrite(FM_REG2, 13);
  drawTest("B"); delay(8000);

  Serial.printf("%7lu ms  C: FM6126A recipe, REG1 max\n", millis());
  regWrite(FM_REG1_MAX, 12); regWrite(FM_REG2, 13);
  drawTest("C"); delay(8000);

  Serial.printf("%7lu ms  D: DP3246 recipe again (LAT 11/12)\n", millis());
  regWrite(DP_REG1, 11); regWrite(DP_REG2, 12);
  drawTest("D"); delay(8000);
}
