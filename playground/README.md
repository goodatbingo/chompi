# TAPE FX Playground

Hear a firmware tweak before you flash it. This folder compiles TAPE's filter,
saturation and wow/flutter code to WebAssembly and runs it in the browser with
Web Audio.

The DSP is **the firmware's own source**, included straight from
`firmware/chompi-tape/code/src`: `DJFilter.h`, `BasicMMF.h`, `Warble.h`, plus
DaisySP's `SoftClip`, `DcBlock` and `DelayLine`. Change one of those files,
rebuild, reload the page, and you hear the changed code.

## Try it

A prebuilt `web/tape_fx.wasm` is checked in, so you only need a local web
server (browsers won't load WebAssembly or AudioWorklets from `file://`):

```bash
cd playground/web
python3 -m http.server 8000
# open http://localhost:8000
```

Pick a source (a synthesised drum loop, TAPE's built-in boot tone, or your own
audio file), press **Play**, and move the knobs. **FX / Bypass** switches
between the processed and dry signal without a gap, so you can A/B a change.
**Magic wand reset** puts the knobs back where the panel's encoder-press reset
puts them.

## Change the firmware and listen

1. Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
   (`brew install emscripten` on macOS).
2. Edit a firmware file, for example the warble depth (`880.f`) in
   `firmware/chompi-tape/code/src/Warble.h`.
3. Rebuild and test:

   ```bash
   cd playground
   make        # rebuilds web/tape_fx.wasm
   make test   # Node smoke test: each control does what it should
   make serve  # serves web/ on http://localhost:8000
   ```

4. Reload the page.

## What is and isn't included

`src/tape_fx.cpp` runs the first per-sample loop of `DSPEngine::ApplyFx()`:
DC block → DJ filter → soft-clip saturation with gain compensation → warble.
The knob mappings (`SetSaturate`, `SetFilter`, `SetFilterResonance`,
`SetWarble`) match `DSPEngine.h`.

`DSPEngine.h` itself depends on libDaisy's hardware drivers, so that chain and
those mappings are copied into `src/tape_fx.cpp` rather than included. If you
change them in `DSPEngine.h`, make the same change there. Everything they call
is the original code.

Not included yet: delay, reverb, the looper, sample playback and the
WAVE/TEMPO engines. Each one can follow the same pattern: include the
firmware header, export a few C functions, and add controls to the page.

The engine runs at 48 kHz like the hardware. `Warble.h` hard-codes that rate,
so the page asks the browser for a 48 kHz AudioContext.

## Files

| | |
|---|---|
| `src/tape_fx.cpp` | C wrapper around the firmware headers |
| `Makefile` | Emscripten build, `test` and `serve` targets |
| `web/index.html` | The playground page |
| `web/tape-fx-worklet.js` | AudioWorklet that runs the WebAssembly on the audio thread |
| `web/tape_fx.wasm` | Prebuilt module (rebuilt by `make`) |
| `test/smoke.mjs` | Checks bypass, filter, saturation and warble behaviour |
