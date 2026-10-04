# Builds web/tape_fx.wasm from the TAPE firmware's FX headers.
# Needs Emscripten (https://emscripten.org): `brew install emscripten` or emsdk.

TAPE_SRC := ../firmware/chompi-tape/code/src
DAISYSP  := ../firmware/chompi-tape/code/libs/DaisySP/Source

SRCS := src/tape_fx.cpp $(DAISYSP)/Utility/dcblock.cpp
DEPS := $(SRCS) $(TAPE_SRC)/DJFilter.h $(TAPE_SRC)/Warble.h $(TAPE_SRC)/BasicMMF.h

EXPORT_NAMES := init process in_l in_r out_l out_r max_block sample_rate \
	set_filter set_resonance set_saturate set_warble set_bypass
comma := ,
empty :=
space := $(empty) $(empty)
EXPORTS := $(subst $(space),$(comma),$(addprefix _tape_fx_,$(EXPORT_NAMES)))

CXXFLAGS := -O2 -std=c++14 -fno-exceptions -fno-rtti \
	-I$(TAPE_SRC) -I$(DAISYSP)/Utility

# A standalone reactor module: no Emscripten JS glue, so the AudioWorklet can
# instantiate it with plain WebAssembly.instantiate().
LDFLAGS := --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 \
	-sINITIAL_MEMORY=1MB -sSTACK_SIZE=64KB -sALLOW_MEMORY_GROWTH=0 \
	-sEXPORTED_FUNCTIONS=$(EXPORTS)

web/tape_fx.wasm: $(DEPS)
	em++ $(CXXFLAGS) $(SRCS) $(LDFLAGS) -o $@

.PHONY: serve test clean
serve: web/tape_fx.wasm
	cd web && python3 -m http.server 8000

test: web/tape_fx.wasm
	node test/smoke.mjs

clean:
	rm -f web/tape_fx.wasm
