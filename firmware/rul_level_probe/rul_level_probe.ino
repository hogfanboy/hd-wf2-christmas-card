// RUL6024 brightness-level probe. Each variant boots the panel fresh with its own settings, shows a
// test card for 10 s with a big number, then restarts into the next variant.
//   1: what the player does now at 100% (brightness 140 of 255, latch blanking 2)
//   2: brightness 255, latch blanking 2
//   3: brightness 255, latch blanking 1 (LEDs lit a larger share of each row: "brightness priority")
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_attr.h"

static const uint8_t R1 = 2, G1 = 6, B1_ = 10, R2 = 3, G2 = 7, B2 = 11, PA = 39, PB = 38, PC = 37, PD = 36, PE = 21,
                     LAT = 33, OE = 35, CLK = 34;
static const int W = 128, H = 64;
MatrixPanel_I2S_DMA *dma;
RTC_NOINIT_ATTR uint32_t variant;

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

static const bool FM_REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
static const bool FM_REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};

void setup() {
  Serial.begin(115200);
  delay(300);
  if (esp_reset_reason() != ESP_RST_SW || variant > 2) variant = 0;
  const uint8_t bright[] = {140, 255, 255}, blank[] = {2, 2, 1};
  Serial.printf("\nRUL level probe: variant %u  brightness %u  latch_blanking %u\n", (unsigned)variant + 1, bright[variant], blank[variant]);
  HUB75_I2S_CFG::i2s_pins pins = {R1, G1, B1_, R2, G2, B2, PA, PB, PC, PD, PE, LAT, OE, CLK};
  HUB75_I2S_CFG cfg(W, H, 1, pins);
  cfg.driver = HUB75_I2S_CFG::DP3246;
  cfg.latch_blanking = blank[variant];
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) { Serial.println("DMA begin FAILED"); for (;;) delay(1000); }
  regWrite(FM_REG1, 12); regWrite(FM_REG2, 13);
  dma->setBrightness8(bright[variant]);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    if (y < 21) px(x, y, 255, 255, 255);
    else if (y < 42) px(x, y, x < 43 ? 255 : 0, x >= 43 && x < 86 ? 255 : 0, x >= 86 ? 255 : 0);
    else { uint8_t v = x * 255 / (W - 1); px(x, y, v, v, v); }
  }
  dma->fillRect(54, 23, 20, 18, 0);
  dma->setTextSize(2); dma->setTextColor(dma->color565(255, 190, 60));
  dma->setCursor(59, 25); dma->print(variant + 1);
  delay(10000);
  variant++;
  ESP.restart();
}
void loop() {}
