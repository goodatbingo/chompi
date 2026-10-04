// Tape Bench host stand-in for libDaisy's umbrella header.
//
// The firmware engines include "daisy.h" / "daisy_seed.h" but only touch a
// small, hardware-free part of libDaisy: System timing, the SDRAM section
// macros, FatFs, and a few utility headers. This directory provides just
// that, so the engine headers compile for WebAssembly without edits.
#pragma once
#include "daisy_seed.h"
