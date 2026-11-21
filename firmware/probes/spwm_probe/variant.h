#pragma once
#include <stdint.h>
struct Variant {
  const char *name;
  uint8_t pin;         // which data line carries the pattern (R1/G1/B1 top, R2/G2/B2 bottom)
  bool perWord;        // latch after every chip's 16-bit word (else once per channel after the whole chain)
  int latchWidth;      // clocks LAT stays high for the data latch
  bool setAddr;        // drive A-E with the line number while writing that line
  bool gclkDuring;     // pulse OE (GCLK) on every data clock while writing
  bool startup;        // send pre-activate/enable/config before the data
  bool chipReverse;    // send chip 0's word first (else chip 7 first)
};
