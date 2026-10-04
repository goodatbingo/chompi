// Tape Bench: the WAVE 1.0 wavetable synth engine (subtractiveEngine.h and
// WavetableManager.h), compiled to WebAssembly from the unmodified firmware.
// Wavetable .wav files are copied onto the RAM-disk SD card, then the
// firmware's own loader finds and reads them, exactly as at boot.
//
// Setup follows main() and MainLoop() in chompi-wave/code/src/chompi_main.cpp;
// the default patch follows MenuPage::SetVoiceSlot(15); keys follow
// NormalPage's key handling.

#define MAX_CYCLES 256 // chompi_main.cpp

#include "daisy.h"
using namespace daisy; // chompi_main.cpp gets this via hardware.h, before the engine headers

#include "subtractiveEngine.h"
#include "card.h"
#include "block_adapter.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
constexpr float  kSampleRate = 48000.f;

myEngine        engine;
wavetableLoader wtLoader;
daisysp::Reverb reverb;
chompi::InterpolatedDelayLine::AudioSample del_mem[kMaxDelayTime];
float wavetableMemory[MAX_CYCLES][MAX_SAMPLES_PER_CYCLE];

bool booted = false;

// AudioCallback() and SDCallback() from chompi_main.cpp, once per 1 ms block.
void AudioBlock(float** in, float** out, size_t size)
{
    for(int c = 0; c < 4; c++)
        std::fill(out[c], out[c] + size, 0.f);
    if(!booted)
        return;
    engine.Prepare();
    engine.Process(in, out, size);
    engine.ProcessFileRequests();
}
tapebench::BlockAdapter audio(AudioBlock);

// ui.h midi2key: MIDI note (minus 24) to panel key ID.
const uint8_t midi2key[49] = {
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
    15, 7, 8, 12, 9, 10, 13, 11,
    14, 16, 21, 17, 18, 22, 19, 23,
    20, 24, 29, 25, 30, 26, 31, 27, 28};
} // namespace

EXPORT float* wave_out(int ch) { return audio.HostOut(ch); }

EXPORT int wave_init()
{
    if(sdcard_format_and_mount() != 0)
        return -1;
    return 0;
}

// Call once the wavetable files are on the card. Returns the table count.
EXPORT int wave_boot()
{
    wtLoader.Init(nullptr, wavetableMemory, kSampleRate);
    engine.Init(kSampleRate, &del_mem[0], &reverb, &wtLoader);
    wtLoader.loadAllToMemory();
    while(engine.ProcessFileRequests()) {}

    // MenuPage::SetVoiceSlot(15) with ui.h enc_defaults.
    engine.setCycle(0, true);
    engine.nextTable(0, true);
    engine.setPitchLfoOn(true);
    engine.setFilterLfoOn(true);
    engine.setGlobalPitch(.5f);
    engine.setAttack(0.f);
    engine.setRelease(0.f);
    engine.setPitchLfoDepth(0.f);
    engine.setLfoDepth(0.f);
    engine.setPitchLfoRate(.58f);
    engine.setLfoRate(.58f);
    engine.setMasterResonance(.63f);
    engine.setDelayTime(.4f);
    engine.setDelayFeedback(.5f);
    engine.setMasterCutoff(.5f);
    engine.setGain(.84f);
    engine.setPan(.5f);
    booted = true;
    return wtLoader.numPreloaded();
}

EXPORT void wave_process(int frames) { audio.Render(frames); }

EXPORT void wave_note(int note, int on)
{
    int key = note - 24;
    if(key < 0 || key > 48)
        return;
    engine.request_fifo.PushBack(KeyRequest(on ? KeyRequest::Type::START : KeyRequest::Type::STOP,
                                            key - 36, midi2key[key], 127.f));
}

EXPORT void wave_set(int id, float v)
{
    switch(id)
    {
        case 0: engine.setGlobalPitch(v); break;
        case 1: engine.setCycle(int8_t(v * (CYCLES - 1) + .5f), true); break;
        case 2: engine.setAttack(v); break;
        case 3: engine.setRelease(v); break;
        case 4: engine.setPitchLfoDepth(v); break;
        case 5: engine.setLfoDepth(v); break;
        case 6: engine.setPitchLfoRate(v); break;
        case 7: engine.setLfoRate(v); break;
        case 8: engine.setMasterCutoff(v); break;
        case 9: engine.setMasterResonance(v); break;
        case 10: engine.setDelayFeedback(v); break;
        case 11: engine.setDelayTime(v); break;
        case 12: engine.setGain(v); break;
        case 13: engine.setPan(v); break;
        case 14: engine.setFinalComp(v); break;
    }
}

EXPORT void wave_table(int t) { engine.nextTable(int8_t(t), true); }
EXPORT int  wave_table_count() { return wtLoader.numPreloaded(); }
EXPORT void wave_octave(int delta) { engine.setOctave(delta); }
EXPORT float wave_vu() { return engine.getVUSample(); }
