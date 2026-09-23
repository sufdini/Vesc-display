// Pulls the TFT_eSPI font tables into the simulator build.
// The tables are `const`, which gives them internal linkage in C++, so they
// are declared extern first to make them visible to TFT_eSPI.cpp.
#define PROGMEM
extern const unsigned char widtbl_f16[96];
extern const unsigned char *const chrtbl_f16[96];
extern const unsigned char widtbl_f32[96];
extern const unsigned char *const chrtbl_f32[96];
extern const unsigned char widtbl_f7s[96];
extern const unsigned char *const chrtbl_f7s[96];
#include "Font16.inc"
#include "Font32rle.inc"
#include "Font7srle.inc"
