# Builds Tape Bench's WebAssembly engines straight from the firmware source.
# Needs Emscripten (https://emscripten.org): `brew install emscripten` or emsdk.
#
#   make          build every engine into web/ and link the factory SD cards
#   make test     run the Node smoke tests
#   make serve    serve web/ on http://localhost:8000

FW      := ../firmware
OBJ     := build
CFLAGS  := -O2 -fno-exceptions -include stddef.h
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-rtti

# Every engine links as a standalone reactor module: no Emscripten JS glue,
# so the AudioWorklet instantiates it with plain WebAssembly.instantiate().
LDFLAGS := --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -Wl,--export-dynamic

ENGINES := tape wave tempo
all: $(addprefix web/,$(addsuffix .wasm,$(ENGINES))) web/card/manifest.json

# The page reads factory card files from web/card: symlinks into
# firmware/card-profiles. card.py lists which files each engine gets.
web/card/manifest.json: card.py
	python3 card.py link

# ---------------------------------------------------------------------------
# Each firmware vendors its own libDaisy and DaisySP, so each engine builds
# against its own copy. $(call firmware,name) defines name_INC and name_LIB
# (library objects) plus their compile rules.
define firmware
$(1)_CODE := $(FW)/chompi-$(1)/code
$(1)_SP   := $$($(1)_CODE)/libs/DaisySP/Source
$(1)_FF   := $$($(1)_CODE)/libs/libDaisy/Middlewares/Third_Party/FatFs/src
$(1)_INC  := -Ishim -I$$($(1)_CODE)/src -I$$($(1)_SP) -I$$($(1)_SP)/Utility \
             -I$$($(1)_FF) -I$$($(1)_CODE)/libs/libDaisy/src/sys \
             -I$$($(1)_CODE)/libs/libDaisy/src -I$$($(1)_CODE)/libs/coreJSON/source/include
$(1)_LIBSRC := $$(shell find $$($(1)_SP) -name '*.cpp') shim/ramdisk.cpp
$(1)_LIB  := $$(foreach s,$$($(1)_LIBSRC),$(OBJ)/$(1)/lib/$$(notdir $$(s)).o) \
             $(OBJ)/$(1)/lib/ff.o $(OBJ)/$(1)/lib/unicode.o $(OBJ)/$(1)/lib/core_json.o
$(OBJ)/$(1)/lib/core_json.o: $$($(1)_CODE)/libs/coreJSON/source/core_json.c
	@mkdir -p $$(@D)
	emcc $$(CFLAGS) $$($(1)_INC) -c $$< -o $$@
$(OBJ)/$(1)/lib/ff.o: $$($(1)_FF)/ff.c
	@mkdir -p $$(@D)
	emcc $$(CFLAGS) $$($(1)_INC) -c $$< -o $$@
$(OBJ)/$(1)/lib/unicode.o: $$($(1)_FF)/option/unicode.c
	@mkdir -p $$(@D)
	emcc $$(CFLAGS) $$($(1)_INC) -c $$< -o $$@
endef

define lib_cpp_rule
$(OBJ)/$(1)/lib/$(notdir $(2)).o: $(2)
	@mkdir -p $$(@D)
	em++ $$(CXXFLAGS) $$($(1)_INC) -c $$< -o $$@
endef

$(foreach f,tape wave tempo,$(eval $(call firmware,$(f))))
$(foreach f,tape wave tempo,$(foreach s,$($(f)_LIBSRC),$(eval $(call lib_cpp_rule,$(f),$(s)))))

# App objects: a wrapper in src/ plus any firmware .cpp files the engine needs.
# They depend on every firmware and shim header, since the engines are
# header-only and a tweak anywhere should rebuild.
define app_obj
$(OBJ)/$(1)/$(notdir $(2)).o: $(2) $$(wildcard $$($(1)_CODE)/src/*.h shim/*.h)
	@mkdir -p $$(@D)
	em++ $$(CXXFLAGS) $$($(1)_INC) -c $$< -o $$@
endef

TAPE_APP  := src/tape_engine.cpp $(tape_CODE)/src/FileStreamingManager.cpp
WAVE_APP  := src/wave_engine.cpp $(wave_CODE)/src/FileStreamingManager.cpp
TEMPO_APP := src/tempo_engine.cpp $(tempo_CODE)/src/FileStreamingManager.cpp

$(foreach s,$(TAPE_APP),$(eval $(call app_obj,tape,$(s))))
$(foreach s,$(WAVE_APP),$(eval $(call app_obj,wave,$(s))))
$(foreach s,$(TEMPO_APP),$(eval $(call app_obj,tempo,$(s))))

# TEMPO needs two build-time substitutions, both applied through a clang VFS
# overlay so the firmware files stay untouched:
#  - hardware.h describes the whole board; shim/tempo/hardware.h keeps only
#    what the engines use.
#  - SampleManager.h and SliceEngine.h use GCC's void* arithmetic, which clang
#    rejects; shim/gnu_void_ptr.py writes byte-arithmetic copies.
TEMPO_GEN     := $(OBJ)/tempo/gen
TEMPO_VOIDPTR := SampleManager.h SliceEngine.h
TEMPO_OVERLAY := $(TEMPO_GEN)/overlay.yaml
$(TEMPO_GEN)/%.h: $(tempo_CODE)/src/%.h shim/gnu_void_ptr.py
	@mkdir -p $(@D)
	python3 shim/gnu_void_ptr.py $< $@
$(TEMPO_OVERLAY): $(addprefix $(TEMPO_GEN)/,$(TEMPO_VOIDPTR)) shim/tempo/hardware.h
	@mkdir -p $(@D)
	@printf '{ "version": 0, "case-sensitive": "false", "roots": [\n' > $@
	@printf '  { "type": "file", "name": "%s", "external-contents": "%s" },\n' \
		$(abspath $(tempo_CODE)/src/hardware.h) $(abspath shim/tempo/hardware.h) >> $@
	@$(foreach h,$(TEMPO_VOIDPTR),printf '  { "type": "file", "name": "%s", "external-contents": "%s" },\n' \
		$(abspath $(tempo_CODE)/src/$(h)) $(abspath $(TEMPO_GEN)/$(h)) >> $@;)
	@sed -i '$$ s/,$$//' $@
	@printf ']}\n' >> $@
tempo_INC += -ivfsoverlay $(abspath $(TEMPO_OVERLAY))
$(foreach s,$(TEMPO_APP),$(OBJ)/tempo/$(notdir $(s)).o): $(TEMPO_OVERLAY)

objs = $(foreach s,$(2),$(OBJ)/$(1)/$(notdir $(s)).o) $($(1)_LIB)

# ---------------------------------------------------------------------------
# Full engines. Memory starts large enough for each firmware's SDRAM buffers
# and grows as files are copied onto the RAM-disk SD card.
BIG := -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2GB -sSTACK_SIZE=256KB

web/tape.wasm: $(call objs,tape,$(TAPE_APP))
	em++ $^ $(LDFLAGS) $(BIG) -sINITIAL_MEMORY=80MB -o $@

web/wave.wasm: $(call objs,wave,$(WAVE_APP))
	em++ $^ $(LDFLAGS) $(BIG) -sINITIAL_MEMORY=16MB -o $@

web/tempo.wasm: $(call objs,tempo,$(TEMPO_APP))
	em++ $^ $(LDFLAGS) $(BIG) -sINITIAL_MEMORY=96MB -o $@

.PHONY: all serve test clean
serve: all
	cd web && python3 -m http.server 8000

test: all
	node test/smoke.mjs

clean:
	rm -rf $(OBJ) web/card $(addprefix web/,$(addsuffix .wasm,$(ENGINES)))
