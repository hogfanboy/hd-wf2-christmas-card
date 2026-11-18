// S-PWM panel probe (RUL6024 test). Bit-bangs the ICN2053/FM6353-style protocol slowly:
// commands are encoded by how many clocks LAT is high (1 latch data, 3 vsync, 4/6/8/10 write config 1-4,
// 12 enable output, 14 pre-activate), OE is the gray-scale clock (GCLK), A-E pick the scan line.
// Every pixel is sent as full brightness, so data ordering doesn't matter: if ANY LEDs light, the chip
// understands this protocol. Cycles through variants; watch the panel + serial log.
#include <Arduino.h>
#include "soc/gpio_struct.h"
#include "variant.h"
typedef uint8_t (*Pattern)(int line, int ch, int chip);   // declared early for Arduino prototypes

// HD-WF2 HUB75 pins
static const uint8_t P_R1 = 2, P_G1 = 6, P_B1 = 10, P_R2 = 3, P_G2 = 7, P_B2 = 11;
static const uint8_t PA = 39, PB = 38, PC = 37, PD = 36, PE = 21, LAT = 33, OE = 35, CLK = 34;
static const int CHIPS = 8;        // 128 columns / 16 channels per chip
static const int LINES = 32;       // 1/32 scan
static const int CH = 16;

static inline void pinSet(uint8_t p, bool v) {
  if (p < 32) { if (v) GPIO.out_w1ts = 1UL << p; else GPIO.out_w1tc = 1UL << p; }
  else        { if (v) GPIO.out1_w1ts.val = 1UL << (p - 32); else GPIO.out1_w1tc.val = 1UL << (p - 32); }
}
static inline void clk() { pinSet(CLK, 1); __asm__ __volatile__("nop; nop; nop; nop;"); pinSet(CLK, 0); }
static inline void rgb(bool r, bool g, bool b) { pinSet(P_R1, r); pinSet(P_R2, r); pinSet(P_G1, g); pinSet(P_G2, g); pinSet(P_B1, b); pinSet(P_B2, b); }

// LAT high for n clocks, data lines low: a bare command.
static void command(int n) {
  rgb(0, 0, 0);
  pinSet(LAT, 1);
  for (int i = 0; i < n; i++) clk();
  pinSet(LAT, 0);
  for (int i = 0; i < 4; i++) clk();
}
// Write one 16-bit value per chip down the whole chain (same value for R/G/B unless given),
// with LAT high during the last `latClocks` clocks of the chain.
static void writeChain(uint16_t vr, uint16_t vg, uint16_t vb, int latClocks) {
  int total = CHIPS * 16;
  for (int i = 0; i < total; i++) {
    int bit = 15 - (i % 16);
    rgb((vr >> bit) & 1, (vg >> bit) & 1, (vb >> bit) & 1);
    if (i == total - latClocks) pinSet(LAT, 1);
    clk();
  }
  pinSet(LAT, 0);
  rgb(0, 0, 0);
  for (int i = 0; i < 4; i++) clk();
}
static void setRow(int r) { pinSet(PA, r & 1); pinSet(PB, r & 2); pinSet(PC, r & 4); pinSet(PD, r & 8); pinSet(PE, r & 16); }

// GCLK on OE + scan lines, for ms milliseconds.
static void refresh(uint32_t ms, int gclkPerRow) {
  uint32_t end = millis() + ms;
  while ((int32_t)(end - millis()) > 0)
    for (int r = 0; r < LINES; r++) {
      setRow(r);
      for (int i = 0; i < gclkPerRow; i++) { pinSet(OE, 1); __asm__ __volatile__("nop; nop;"); pinSet(OE, 0); }
    }
}

// ICN2053 config values (from LEDVISION via the ICN2053 library), line count = 32.
static void writeConfig() {
  const uint16_t cfg1 = (0 << 14) | ((LINES - 1) << 8) | (1 << 6) | (3 << 4);
  // brightness raised vs. probe 1: current 16 (=100%), bright 15 (max)
  const uint16_t cfg2r = (31 << 10) | (15 << 5) | (16 << 1) | 1, cfg2g = (28 << 10) | (15 << 5) | (16 << 1) | 1, cfg2b = (23 << 10) | (15 << 5) | (16 << 1) | 1;
  const uint16_t cfg3 = (4 << 4) | (1 << 2) | 0x4003, cfg4 = (4 << 4) | 0x0e00;
  command(14); writeChain(cfg1, cfg1, cfg1, 4);
  command(14); writeChain(cfg2r, cfg2g, cfg2b, 6);
  command(14); writeChain(cfg3, cfg3, cfg3, 8);
  command(14); writeChain(cfg4, cfg4, cfg4, 10);
  command(14); writeChain(0x0008, 0x0008, 0x0008, 2);   // debug reg
}

// ---------------- Probe 4: six data-latch variants in one loop ----------------
// Every variant draws the same test pattern: lines 0-15 and channels 0-7 of every chip lit, in ONE colour
// on ONE half (data line chosen by the variant), so success = 8-column-wide stripes in the top part of
// that half. Uniform-per-chip patterns can't produce this, so it proves real per-pixel storage.


static bool lit(int line, int ch) { return line < 16 && ch < 8; }

static void writeFrameV(const Variant &v) {
  const int total = CHIPS * 16;
  for (int line = 0; line < LINES; line++) {
    if (v.setAddr) setRow(line);
    for (int ch = 0; ch < CH; ch++)
      for (int n = 0; n < CHIPS; n++) {
        bool on = lit(line, ch);
        rgb(0, 0, 0);
        pinSet(v.pin, on);
        for (int bit = 15; bit >= 0; bit--) {
          int pos = n * 16 + (15 - bit);                       // position within this channel's chain
          bool lat = v.perWord ? (bit < v.latchWidth) : (pos >= total - v.latchWidth);
          pinSet(LAT, lat);
          clk();
          if (v.gclkDuring) { pinSet(OE, 1); pinSet(OE, 0); }
        }
        pinSet(LAT, 0);
      }
  }
  rgb(0, 0, 0);
}

static void showV(const Variant &v, uint32_t ms) {
  Serial.printf("%7lu ms  %s\n", millis(), v.name);
  if (v.startup) { command(14); command(12); writeConfig(); }
  writeFrameV(v);
  command(3);                                                  // vsync
  refresh(ms, 138);
}
static void blank(uint32_t ms) {
  Variant b = {"  (black)", P_R1, false, 1, false, false, true, false};
  // all-dark frame: pattern pin never set because we write with a pin that is masked off below
  command(14); command(12); writeConfig();
  for (int i = 0; i < LINES * CH * CHIPS * 16; i++) { pinSet(LAT, (i % (CHIPS * 16)) == CHIPS * 16 - 1); clk(); }
  pinSet(LAT, 0); command(3); refresh(ms, 138);
  (void)b;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nS-PWM probe 4 (RUL6024): six latch variants");
  for (uint8_t p : (const uint8_t[]){P_R1, P_G1, P_B1, P_R2, P_G2, P_B2, PA, PB, PC, PD, PE, LAT, OE, CLK}) { pinMode(p, OUTPUT); pinSet(p, 0); }
}

void loop() {
  const Variant vs[] = {
    {"1 TOP RED      per-channel latch + row address set",  P_R1, false, 1, true,  false, true,  false},
    {"2 TOP GREEN    per-word latch + row address set",     P_G1, true,  1, true,  false, true,  false},
    {"3 TOP BLUE     per-channel latch, 2-clock wide",      P_B1, false, 2, false, false, true,  false},
    {"4 BOTTOM RED   per-channel latch + GCLK during data", P_R2, false, 1, false, true,  true,  false},
    {"5 BOTTOM GREEN per-channel latch, no startup/config", P_G2, false, 1, false, false, false, false},
    {"6 BOTTOM BLUE  per-word latch, chip order reversed",  P_B2, true,  1, false, false, true,  true},
  };
  for (auto &v : vs) { showV(v, 8000); blank(1500); }
}
