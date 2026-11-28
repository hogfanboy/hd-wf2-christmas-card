// Panel driver setup: RUL6024 config registers, fast scan for the QR screen, restarting the driver.

// RUL6024 config registers, found by experiment on card 2 (no datasheet). A register is written by holding
// LAT high for the last N clocks of a 128-bit chain; the panel keeps it until power-off.
//  - N=12/13 with the FM6126A values: smoother dark grays than the library's DP3246 init leaves.
//  - N=11 all ones: LED current/brightness (Huidu-level bright; the library's value left it dim).
//  - N=14 = 0x0100: removes the fixed noise in the lower middle-right. Other bits there either do nothing,
//    make the noise worse, or roughen the gradient.
// The pins are taken from the LCD peripheral for a moment as plain GPIO, then handed back.
static void panelRegWrite(const bool reg[16], int latClocks) {
  const uint8_t steal[] = {PINS.r1, PINS.g1, PINS.b1, PINS.r2, PINS.g2, PINS.b2, PINS.clk, PINS.lat, PINS.oe};
  const uint8_t data[] = {PINS.r1, PINS.g1, PINS.b1, PINS.r2, PINS.g2, PINS.b2};
  uint32_t saved[sizeof(steal)];
  gpio_set_level((gpio_num_t)PINS.oe, 1); gpio_set_level((gpio_num_t)PINS.lat, 0); gpio_set_level((gpio_num_t)PINS.clk, 0);
  for (int i = 0; i < (int)sizeof(steal); i++) { saved[i] = GPIO.func_out_sel_cfg[steal[i]].val; GPIO.func_out_sel_cfg[steal[i]].val = 0x100; }
  for (int l = 0; l < W; l++) {
    for (uint8_t p : data) gpio_set_level((gpio_num_t)p, reg[l % 16]);
    if (l == W - latClocks) gpio_set_level((gpio_num_t)PINS.lat, 1);
    gpio_set_level((gpio_num_t)PINS.clk, 1); gpio_set_level((gpio_num_t)PINS.clk, 0);
  }
  gpio_set_level((gpio_num_t)PINS.lat, 0);
  for (uint8_t p : data) gpio_set_level((gpio_num_t)p, 0);
  gpio_set_level((gpio_num_t)PINS.clk, 1); gpio_set_level((gpio_num_t)PINS.clk, 0);
  for (int i = 0; i < (int)sizeof(steal); i++) GPIO.func_out_sel_cfg[steal[i]].val = saved[i];
}
void panelRegInit() {
  if (panelType) return;                         // other panel types: no register writes from us (FM6126A: the library does its own)
  static const bool REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
  static const bool REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};
  static const bool GAIN[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  static const bool R14[16]  = {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};   // 0x0100, MSB first
  panelRegWrite(REG1, 12);
  panelRegWrite(REG2, 13);
  panelRegWrite(GAIN, 11);
  panelRegWrite(R14, 14);
}

// Fast scan (QR screen): the panel normally sends 8 bit planes per row, so a phone camera's rolling shutter
// catches bands. A QR code is black and white, so the address screen restarts the panel at 2-bit color,
// which refreshes about 10x faster, and goes back to full color after.
// The library never frees its GDMA channel on delete, so we free it ourselves (members reached through the
// standard explicit-instantiation access trick; no library edits, CI uses the stock library).
#include "esp_private/gdma.h"
template <typename Tag, typename Tag::type M> struct Reach { friend typename Tag::type get(Tag) { return M; } };
struct BusTag  { typedef Bus_Parallel16 MatrixPanel_I2S_DMA::*type; friend type get(BusTag); };
struct ChanTag { typedef gdma_channel_handle_t Bus_Parallel16::*type; friend type get(ChanTag); };
template struct Reach<BusTag, &MatrixPanel_I2S_DMA::dma_bus>;
template struct Reach<ChanTag, &Bus_Parallel16::dma_chan>;
bool panelFast = false;
static void panelStop() {
  panel->stopDMAoutput();
  gdma_channel_handle_t ch = (panel->*get(BusTag())).*get(ChanTag());
  if (ch) { gdma_disconnect(ch); gdma_del_channel(ch); }
  delete panel; panel = nullptr;
}

bool startPanel(bool dbl, bool fast) {
  HUB75_I2S_CFG cfg(W, H, 1, PINS);
  if (fast) { cfg.setPixelColorDepthBits(fastBits); cfg.min_refresh_rate = fastBits <= 2 ? 300 : 120; }
  // One setup for both panel types the cards came with (FM6124C and RUL6024), tested 2026-11-18:
  // the library's DP3246 mode gives the 3-clock latch the RUL6024 needs, but it also inverts the clock,
  // which shifts the RUL6024 image one column and misaligns the FM6124C's two halves. So after begin()
  // the clock pin goes back to the original edge (the library applies the inversion in the GPIO matrix).
  // Panel type (app: Panel type, saved as NVS ptype): 0 and 1 = the setup above, 2 = the library's FM6126A
  // driver and init, 3 = a plain shift register. Only types 0 and 1 get the clock fix below.
  cfg.driver = panelType == 2 ? HUB75_I2S_CFG::FM6126A : panelType == 3 ? HUB75_I2S_CFG::SHIFTREG : HUB75_I2S_CFG::DP3246;
  cfg.double_buff = dbl;
  panel = new MatrixPanel_I2S_DMA(cfg);
  if (panel->begin()) {
    if (panelType <= 1) GPIO.func_out_sel_cfg[PINS.clk].inv_sel = 0;
    doubleBuffered = dbl;
    panelFast = fast;
    return true;
  }
  delete panel; panel = nullptr;                    // (its DMA channel may never have been set up)
  return false;
}
// Switches between full color and fast scan. If the panel can't restart, the card reboots.
void panelSetFast(bool on) {
  if (!panel || on == panelFast) return;
  bool dbl = doubleBuffered;
  uint32_t t = millis();
  panelStop();
  if (!startPanel(dbl, on) && !startPanel(false, on)) { Serial.println("Panel restart failed, rebooting"); ESP.restart(); }
  panelRegInit();
  applyBrightness();
  panel->clearScreen();
  shownValid = false;
  Serial.printf("Panel %s: %d Hz (%u ms), free heap %u\n", on ? "fast scan" : "full color",
                panel->calculated_refresh_rate, (unsigned)(millis() - t), (unsigned)ESP.getFreeHeap());
}
