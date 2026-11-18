// S-PWM panel probe (RUL6024 test). Bit-bangs the ICN2053/FM6353-style protocol slowly:
// commands are encoded by how many clocks LAT is high (1 latch data, 3 vsync, 4/6/8/10 write config 1-4,
// 12 enable output, 14 pre-activate), OE is the gray-scale clock (GCLK), A-E pick the scan line.
// Every pixel is sent as full brightness, so data ordering doesn't matter: if ANY LEDs light, the chip
// understands this protocol. Cycles through variants; watch the panel + serial log.
#include <Arduino.h>
#include "soc/gpio_struct.h"
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

// Pattern: which of the 6 data lines are ON for a given (line, channel, chip) word.
// bits: 1=R1 2=G1 4=B1 (top half)  8=R2 16=G2 32=B2 (bottom half)
// Probe 3 format (MBI5153-style): for each line, for each channel, shift one 16-bit word per chip down the
// whole chain (last chip's word first), then DATA_LATCH (LAT high) on the final clock only: 1 latch per
// channel, 16 per line, 512 per frame. (Probe 2 latched after every chip's word = 8x too often.)
static void writeFrame(Pattern pat) {
  for (int line = 0; line < LINES; line++)
    for (int ch = 0; ch < CH; ch++)
      for (int chip = CHIPS - 1; chip >= 0; chip--) {
        uint8_t m = pat ? pat(line, ch, chip) : 0;
        pinSet(P_R1, m & 1); pinSet(P_G1, m & 2); pinSet(P_B1, m & 4);
        pinSet(P_R2, m & 8); pinSet(P_G2, m & 16); pinSet(P_B2, m & 32);
        for (int bit = 15; bit >= 0; bit--) {
          bool last = (chip == 0 && bit == 0);
          if (last) pinSet(LAT, 1);
          clk();
          if (last) pinSet(LAT, 0);
        }
      }
  rgb(0, 0, 0);
}

static void show(const char *name, Pattern pat, uint32_t ms) {
  Serial.printf("%7lu ms  %s\n", millis(), name);
  command(14); command(12);
  writeConfig();
  writeFrame(pat);
  command(3);
  refresh(ms, 138);
}

static uint8_t p1(int l, int c, int k) { return l == 0 ? 1 : 0; }                      // RED: line 0
static uint8_t p2(int l, int c, int k) { return c == 0 ? 2 : 0; }                      // GREEN: channel 0
static uint8_t p3(int l, int c, int k) { return k == 0 ? 4 : 0; }                      // BLUE: chip 0
static uint8_t p4(int l, int c, int k) { return (l == 0 && c == 0 && k == 0) ? 1 : 0; } // RED: one pixel
static uint8_t p5(int l, int c, int k) { return (l < 4 && c < 4 && k == 0) ? 2 : 0; }  // GREEN: 4x4 corner
static uint8_t p6(int l, int c, int k) { return k == 7 ? 4 : 0; }                      // BLUE: chip 7
static uint8_t p7(int l, int c, int k) { return 1 | 8; }                               // RED: everything

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nS-PWM mapping probe (RUL6024)");
  for (uint8_t p : (const uint8_t[]){P_R1, P_G1, P_B1, P_R2, P_G2, P_B2, PA, PB, PC, PD, PE, LAT, OE, CLK}) { pinMode(p, OUTPUT); pinSet(p, 0); }
}

void loop() {
  const struct { const char *name; Pattern p; } stages[] = {
    {"1 RED    line 0", p1}, {"2 GREEN  channel 0", p2}, {"3 BLUE   chip 0", p3}, {"4 RED    one pixel (line0 ch0 chip0)", p4},
    {"5 GREEN  4x4 (lines0-3, ch0-3, chip0)", p5}, {"6 BLUE   chip 7", p6}, {"7 RED    everything (both halves)", p7}};
  for (auto &s : stages) { show(s.name, s.p, 8000); show("  (black)", nullptr, 1500); }
}
