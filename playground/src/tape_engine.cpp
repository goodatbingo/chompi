// Tape Bench: the complete TAPE 2.0 audio engine (DSPEngine.h), compiled to
// WebAssembly. Seven streaming sample voices, the varispeed tape looper,
// filter, saturation, warble, delay and reverb all run from the firmware's
// unmodified source. The hardware-facing parts of libDaisy are replaced by
// playground/shim, and the microSD card by a RAM disk.
//
// The setup in tape_init() follows main() in chompi-tape/code/src's
// chompi_main.cpp; tape_note() follows the MIDI note handling in ui.h.

#include "DSPEngine.h"
#include "card.h"
#include "block_adapter.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

using namespace daisy;

namespace
{
constexpr float  kSampleRate = 48000.f;

Engine          engine;
daisysp::Reverb reverb;
chompi::InterpolatedDelayLine::AudioSample del_mem[kMaxDelayTime];
RamBufferMemory loop_buff, chompi_buff;
int16_t         loop_mem[kMaxRamBuffSize];
int16_t         chompi_mem[kMaxRamBuffSize];

// AudioCallback() and SDCallback() from chompi_main.cpp, once per 1 ms
// hardware block. Codec channels: in = mic, unused, line L, line R;
// out = headphone L/R, main L/R.
void AudioBlock(float** in, float** out, size_t size)
{
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

EXPORT float* tape_in(int ch) { return audio.HostIn(ch); }
EXPORT float* tape_out(int ch) { return audio.HostOut(ch); }

EXPORT int tape_init()
{
    if(sdcard_format_and_mount() != 0)
        return -1;
    loop_buff.Init(&loop_mem[0]);
    chompi_buff.Init(&chompi_mem[0]);
    engine.Init(kSampleRate, &reverb, &del_mem[0], &loop_buff, &chompi_buff,
                false, true, MonitorMode::HP);
    engine.FillDefaultSample(kSampleRate);
    engine.SetGlobalPitch(1.f);
    engine.SetInputGain(.75f);
    engine.SetMainGain(.8f);
    engine.SetGain(.704f);
    return 0;
}

// Call after copying sample files onto the card.
EXPORT void tape_rescan()
{
    engine.UpdateFileExists();
    engine.SetVoiceSlot(engine.GetVoiceSlot(), true);
}

EXPORT void tape_process(int frames) { audio.Render(frames); }

// MIDI note in, as ui.h handles NoteOn / NoteOff.
EXPORT void tape_note(int note, int velocity, int on)
{
    int key = note - 24;
    if(key < 0 || key > 48)
        return;
    if(on && engine.GetVoiceMode() == VoiceMode::CUBBI)
    {
        size_t slot = KeyToSlot(midi2key[key]);
        if(slot == kSlotNone || !engine.GetFileExists(slot - 1))
            return;
        engine.OpenCubbiSlot(1.f, 0.f, 1.f, 0.f, 1.f, true, true, .704f, .5f);
    }
    engine.request_fifo.PushBack(KeyRequest(
        on ? KeyRequest::Type::START : KeyRequest::Type::STOP,
        key - 36, midi2key[key], velocity + 1));
    if(on && engine.GetLooperRecordArm())
        engine.ToggleLooperRecord();
}

EXPORT void tape_set(int id, float v)
{
    switch(id)
    {
        case 0: engine.SetReverb(v); break;
        case 1: engine.SetDelayFeedback(v); break;
        case 2: engine.SetDelayTime(v); break;
        case 3: engine.SetSaturate(v); break;
        case 4: engine.SetWarble(v); break;
        case 5: engine.SetFilter(v); break;
        case 6: engine.SetFilterResonance(v); break;
        case 7: engine.SetGlobalPitchFree(v); break;
        case 8: engine.SetStartPointForce(v); break;
        case 9: engine.SetEndPointForce(v); break;
        case 10: engine.SetAttack(v); break;
        case 11: engine.SetDecay(v); break;
        case 12: engine.SetGain(v); break;
        case 13: engine.SetPan(v); break;
        case 14: engine.SetLooperPitchFree(v); break;
        case 15: engine.SetMainGain(v); break;
        case 16: engine.SetInputGain(v); break;
        case 17: engine.IncrementLooperDubGain(v); break;
    }
}

EXPORT void tape_looper_record(int rising) { engine.LooperRecordButton(rising != 0); }
EXPORT void tape_looper_play(int rising) { engine.LooperPlayButton(rising != 0); }
EXPORT int  tape_looper_state()
{
    return (engine.IsLooperPlaying() ? 1 : 0) | (engine.IsLooperRecording() ? 2 : 0)
           | (engine.IsLooperRecordArmed() ? 4 : 0) | (engine.GetLooperIsEmpty() ? 8 : 0);
}
EXPORT float tape_looper_position() { return engine.GetLooperPosition(); }

EXPORT void tape_record(int on)
{
    if(on)
        engine.StartNewRecording(0);
    else
        engine.StopRecording();
}
EXPORT void tape_input_source(int src) { engine.SetInputSource(InputSource(src)); }
EXPORT void tape_monitor(int on) { engine.SetInputMonitor(on != 0); }
// 0 = headphones only, 1 = both outputs (input runs through the FX), 2 = send/return.
EXPORT void tape_monitor_mode(int m)
{
    for(int i = 0; i < 3 && int(engine.GetMonitorMode()) != m; i++)
        engine.IncrementMonitorMode();
}

EXPORT void tape_voice_mode(int cubbi)
{
    engine.SetVoiceMode(cubbi ? VoiceMode::CUBBI : VoiceMode::JAMMI);
}
EXPORT void tape_bank(int b) { engine.SetBank(b); }
EXPORT void tape_slot(int slot) { engine.SetVoiceSlot(slot, true); }
EXPORT int  tape_file_exists(int slot) { return engine.GetFileExists(slot); }
EXPORT float tape_vu(int output) { return engine.GetVUSample(output ? VUTarget::VU_OUTPUT : VUTarget::VU_INPUT); }
EXPORT int  tape_any_playing() { return engine.AnyVoicesPlaying(); }
