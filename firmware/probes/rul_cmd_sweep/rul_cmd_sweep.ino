// RUL6024 register-command sweep: writes an all-ones value with LAT held for N clocks, N = 1,2,4..16.
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

static const uint8_t R1 = 2, G1 = 6, B1_ = 10, R2 = 3, G2 = 7, B2 = 11, PA = 39, PB = 38, PC = 37, PD = 36, PE = 21,
                     LAT = 33, OE = 35, CLK = 34;
static const int W = 128, H = 64;
MatrixPanel_I2S_DMA *dma;

static void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) { dma->drawPixelRGB888((x + 1) % W, y, r, g, b); }
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

static const bool ONES[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static const bool FM_REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
static const bool FM_REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};

static void card(int n) {
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    if (y < 21) px(x, y, 255, 255, 255);
    else if (y < 42) px(x, y, x < 43 ? 255 : 0, x >= 43 && x < 86 ? 255 : 0, x >= 86 ? 255 : 0);
    else { uint8_t v = x * 255 / (W - 1); px(x, y, v, v, v); }
  }
  dma->fillRect(50, 23, 28, 18, 0);
  dma->setTextSize(2); dma->setTextColor(dma->color565(255, 190, 60));
  dma->setCursor(n < 10 ? 59 : 53, 25); dma->print(n);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nRUL6024 register-command sweep");
  HUB75_I2S_CFG::i2s_pins pins = {R1, G1, B1_, R2, G2, B2, PA, PB, PC, PD, PE, LAT, OE, CLK};
  HUB75_I2S_CFG cfg(W, H, 1, pins);
  cfg.driver = HUB75_I2S_CFG::DP3246;
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) { Serial.println("DMA begin FAILED"); for (;;) delay(1000); }
  regWrite(FM_REG1, 12); regWrite(FM_REG2, 13);
  dma->setBrightness8(140);
  card(0);
  Serial.printf("%7lu ms  0: start (player's settings)\n", millis());
  delay(8000);
  for (int n = 1; n <= 16; n++) {
    if (n == 3) continue;                      // 3 = the normal data latch
    regWrite(ONES, n);
    card(n);
    Serial.printf("%7lu ms  %d: all ones written with LAT high for %d clocks\n", millis(), n, n);
    delay(6000);
  }
  Serial.println("Sweep done. Power-cycle the panel to reset its registers.");
}
void loop() {}
