# Tape Bench

Hear a firmware change before you flash it. Tape Bench compiles the audio
engines of all three firmwares to WebAssembly and plays them in the browser:

| Tab | Firmware code that runs | What you can do |
|---|---|---|
| **TAPE** | `DSPEngine.h`: 7 streaming sample voices, tape looper, filter, saturation, warble, delay, reverb | Play JAMMI and CUBBI bank A from the factory card, sample your input, record and overdub loops |
| **WAVE** | `subtractiveEngine.h` + `WavetableManager.h` | Play the 7 factory wavetables; cycle, filter, LFOs, delay/reverb |
| **TEMPO** | `SampleEngine.h`, `SliceEngine.h`, `ArpeggiatorSequencer.h`, `clockManager.h`, `FxEngine.h`, `granularDelay.h` | Chromatic and slice engines on the factory card, the arpeggiator on its clock, granular delay |

The engine code is **compiled unmodified** from `firmware/chompi-*/code/src`
with each firmware's own vendored DaisySP and libDaisy. Edit a firmware file,
rebuild, reload the page, and you hear the edited code.

Tape Bench is an unofficial community tool. It is built from the
MIT-licensed firmware in this repo and is not affiliated with or endorsed by
the hardware's makers.

## Try it

Prebuilt `web/*.wasm` files are checked in, so you only need Python to serve
the page (browsers won't load AudioWorklets from `file://`):

```bash
cd playground
python3 card.py link          # point web/card at firmware/card-profiles
cd web && python3 -m http.server 8000
# open http://localhost:8000
```

Press **Start audio**, pick a tab, and play the keyboard: on screen, with the
computer keys (A W S E D … K, Z/X for octave), or with a MIDI controller in
browsers that support Web MIDI. **Audio in** feeds a file or your microphone
into the engine's line input: in TAPE it runs through the FX and can be
sampled or looped.

## Change the firmware and listen

1. Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
   (`brew install emscripten` on macOS). CI pins 6.0.11.
2. Edit a firmware file, for example the warble depth (`880.f`) in
   `firmware/chompi-tape/code/src/Warble.h`.
3. Rebuild and test:

   ```bash
   cd playground
   make -j8    # rebuilds web/tape.wasm, web/wave.wasm, web/tempo.wasm
   make test   # Node smoke tests against the factory card
   make serve  # serves web/ on http://localhost:8000
   ```

4. Reload the page.

## How it works

The firmware engines only touch a small, hardware-free part of the board, so
`shim/` stands in for it and nothing in `firmware/` is edited:

- **SD card.** The real FatFs from libDaisy, on a RAM disk (`shim/ramdisk.cpp`)
  that allocates memory only for what's written. The page copies factory card
  files onto it; the firmware's own loaders find and read them.
- **SDRAM.** Plain arrays the same size as the hardware buffers.
- **Timing.** `System::GetNow()` follows the audio clock. Engines run in
  48-frame blocks, as the Daisy Seed calls them (`shim/block_adapter.h`),
  re-chunked into Web Audio's 128-frame quanta with 1 ms of added latency.
  TEMPO's MIDI-clock timer fires at the period the firmware programs.
- **TEMPO extras.** Its `hardware.h` describes the whole board, so the build
  swaps in `shim/tempo/hardware.h` (just the MIDI-out calls, as no-ops) with a
  clang VFS overlay. `SampleManager.h` and `SliceEngine.h` use GCC's `void*`
  arithmetic, which clang rejects; `shim/gnu_void_ptr.py` writes copies with
  the same byte arithmetic made explicit. Both happen at build time.
- **Case.** The firmware includes `Limiter.h`; the file is `limiter.h`.
  `shim/Limiter.h` forwards to it for case-sensitive file systems.

Each wrapper in `src/` sets its engine up the way that firmware's `main()`
does and exposes a few C functions. The setters it calls are the ones the
panel code calls; `web/index.html` names the firmware function under every
control.

### What isn't emulated

The panel itself: LEDs, the encoders' page and shift logic, presets, MIDI
out, battery and USB. Controls on the page call the engine directly with the
panel's 0–1 encoder values. TAPE serves bank A only to keep the download
reasonable; other banks are in `firmware/card-profiles/tape-2.0` if you add
them to `card.py`.

## GitHub Pages

`.github/workflows/pages.yml` rebuilds the wasm from the firmware source,
checks it matches the committed copies, runs the smoke tests, and publishes
the page plus the factory card files to GitHub Pages on every push to `main`.
To turn it on for a fork, go to **Settings → Pages** and set **Source** to
**GitHub Actions**.

## Files

| | |
|---|---|
| `src/tape_engine.cpp`, `src/wave_engine.cpp`, `src/tempo_engine.cpp` | One wrapper per firmware |
| `shim/` | Stand-ins for the hardware layer (see above) |
| `card.py` | Stages factory card files and writes `web/card/manifest.json` |
| `Makefile` | Emscripten build, `test` and `serve` targets |
| `web/index.html` | The page |
| `web/engine-worklet.js` | AudioWorklet that hosts whichever engine is selected |
| `web/*.wasm` | Prebuilt engines (rebuilt by `make`) |
| `test/smoke.mjs` | Boots each engine on the factory card and checks it plays |
