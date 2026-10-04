// Tape Bench: the TEMPO 1.0 engines compiled to WebAssembly from the
// unmodified firmware. That's the chromatic sample engine, the slice engine,
// the arpeggiator/pattern sequencer and its clock, and the FX engine with
// granular delay and reverb. Samples load from the RAM-disk SD card through
// the firmware's own sampleManager.
//
// Setup and the audio path follow main(), AudioCallback() and
// MidiClockCallback() in chompi-tempo/code/src/chompi_main.cpp; key handling
// follows NormalPage's key cases.

#include "daisy.h"
using namespace daisy;

#include "OptionsManager.h"
#include "EngineBase.h"
#include "SampleEngine.h"
#include "SliceEngine.h"
#include "SampleManager.h"
#include "ArpeggiatorSequencer.h"
#include "clockManager.h"
#include "FxEngine.h"
#include "granularDelay.h"
#include "reverb.h"
#include "card.h"
#include "block_adapter.h"

using namespace chompi;

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
constexpr float  kSampleRate = 48000.f;
constexpr size_t kBlock      = tapebench::kHardwareBlock;
constexpr size_t kBufferSize = 480000; // chompi_main.cpp: 10 s at 48 kHz

float granularBuffer[kBufferSize * 2];
float frozenBuffer[kBufferSize * 2];
// Stands in for the 64 MB external SDRAM at 0xC0000000. sampleManager skips
// the first 7.68 MB (where the granular buffers live on the hardware), then
// lays out a record buffer and one 1.92 MB region per sample slot.
alignas(16) uint8_t sample_ram[64 * 1024 * 1024];

Hardware             hw;
OptionsManager       options;
sampleEngine         sEngine;
sliceEngine          slice;
sampleManager        sManager;
fxEngine             fx;
FileStreamingManager file_manager;
ArpeggiatorSequencer arpSeq;
clockManager         cManager;
granularDelay        delay;
Reverb               reverb;
TimerHandle          midi_clock_timer;
BaseEngine*          engines[2] = {&sEngine, &slice};

float  chromaBuffer[2][kBlock], sliceBuffer[2][kBlock];
float* chromaPtr[2] = {chromaBuffer[0], chromaBuffer[1]};
float* slicePtr[2]  = {sliceBuffer[0], sliceBuffer[1]};

bool loaded = false;

// System::GetPClk2Freq() on the Seed. It cancels out of the clock rate: the
// firmware programs the timer period from it, and the timer runs from it.
constexpr double kPClk2      = 120000000.0;
double           clock_phase = 0.0;

// ui.h midi2key: MIDI note (minus 24) to panel key ID.
const uint8_t midi2key[49] = {
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
    15, 7, 8, 12, 9, 10, 13, 11,
    14, 16, 21, 17, 18, 22, 19, 23,
    20, 24, 29, 25, 30, 26, 31, 27, 28};

// AudioCallback() and SDCallback() from chompi_main.cpp, minus the panel and
// MIDI ports, once per 1 ms hardware block.
void AudioBlock(float** in, float** out, size_t size)
{
    for(int c = 0; c < 4; c++)
        std::fill(out[c], out[c] + size, 0.f);
    if(!loaded)
        return;

    if(cManager.checkIntervalExpired(0))
    {
        arpSeq.setClockEdge(0);
        delay.setClockEdge();
    }
    if(cManager.checkIntervalExpired(1))
        arpSeq.setClockEdge(1);
    cManager.checkIntervalExpired(2);
    arpSeq.Prepare();
    for(auto* e : engines)
        e->Prepare();

    engines[0]->Process(in, chromaPtr, size);
    engines[1]->Process(in, slicePtr, size);
    for(size_t i = 0; i < size; ++i)
    {
        out[0][i] = chromaBuffer[0][i] + sliceBuffer[0][i];
        out[1][i] = chromaBuffer[1][i] + sliceBuffer[1][i];
    }
    fx.Process(in, out, size, chromaPtr, slicePtr);
    fx.ApplyOutputFX(in, out, size);

    file_manager.ProcessRequests();

    // TIM16 fires MidiClockCallback() at the period clockManager programs
    // (prescaler 239 on a 2 x PCLK2 timer clock). Checked once per block, so
    // ticks land within 1 ms of where the hardware timer would put them.
    const double tick_hz = (2.0 * kPClk2 / 240.0) / (midi_clock_timer.GetPeriod() + 1.0);
    clock_phase += tick_hz * size / kSampleRate;
    while(clock_phase >= 1.0)
    {
        clock_phase -= 1.0;
        cManager.incrementCounters(); // MidiClockCallback()
        delay.setClockPulse();
    }
}
tapebench::BlockAdapter audio(AudioBlock);
} // namespace

EXPORT float* tempo_in(int ch) { return audio.HostIn(ch); }
EXPORT float* tempo_out(int ch) { return audio.HostOut(ch); }

EXPORT int tempo_init() { return sdcard_format_and_mount(); }

// Call once the card's Chromatic/Slice/Buffer folders are written.
EXPORT int tempo_boot()
{
    options.Init();
    reverb.Init(kSampleRate);
    fx.Init(&sManager, kSampleRate, &delay, &reverb, options.monitor_position);
    delay.Init(granularBuffer, frozenBuffer, kBufferSize, &cManager, options.delay_mute);
    sManager.Init(sample_ram, &file_manager, kSampleRate);
    for(auto* e : engines)
        e->Init(&sManager, kSampleRate);
    cManager.Init(&midi_clock_timer, size_t(kPClk2));
    arpSeq.Init(engines, &cManager, &hw, &options);

    // MidiClockCallback(): load everything, then select slot 14 when done.
    sManager.loadFileData();
    for(int guard = 0; guard < 1000000 && !sManager.checkLoaded(); guard++)
        file_manager.ProcessRequests();
    for(auto* e : engines)
        e->setSample(14);
    loaded = true;
    return 0;
}


EXPORT void tempo_process(int frames) { audio.Render(frames); }

// NormalPage's key handling for a panel key, given as a MIDI note.
EXPORT void tempo_note(int note, int on)
{
    int k = note - 24;
    if(k < 0 || k > 48)
        return;
    const size_t buttonID = midi2key[k];
    const size_t eng      = fx.getEngine();
    const float  nn       = float(note - 60);
    const float  transpose_nn = eng == CHROMATIC ? nn : 0.f;
    if(on)
    {
        if(arpSeq.getPlay(eng))
            arpSeq.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, nn, buttonID, 127.f));
        else if(arpSeq.getLatch())
        {
            if(!(arpSeq.getSustain() && arpSeq.isKeyInSeq(buttonID, eng)))
                engines[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, transpose_nn, buttonID, 127.f));
            arpSeq.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, nn, buttonID, 127.f));
        }
        else
            engines[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, transpose_nn, buttonID, 127.f));
    }
    else
    {
        if(!arpSeq.getSustain() && !arpSeq.checkNotePlaying(note - 60))
            engines[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, transpose_nn, buttonID, 127.f));
        if(arpSeq.getPlay(eng) || (arpSeq.getLatch() && !arpSeq.getSustain()))
            arpSeq.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, nn, buttonID, 127.f));
    }
}

// Panel buttons: KEY_27 is play, KEY_28 is loop (latch). The chompi key
// toggles between the chromatic and slice engines.
EXPORT void tempo_play(int rising) { arpSeq.setPlay(rising != 0); }
EXPORT void tempo_latch(int rising) { arpSeq.setLatch(rising != 0); }
EXPORT void tempo_engine(int slice_engine)
{
    fx.setEngine(slice_engine ? SLICE : CHROMATIC);
    arpSeq.setEngine(slice_engine != 0);
}
EXPORT int  tempo_get_engine() { return fx.getEngine(); }
EXPORT int  tempo_playing(int eng) { return arpSeq.getPlay(eng); }
EXPORT int  tempo_latched() { return arpSeq.getLatch(); }
EXPORT void tempo_sample(int eng, int slot) { engines[eng & 1]->setSample(slot); }
EXPORT int  tempo_valid_sample(int eng, int slot) { return engines[eng & 1]->isValidSample(slot); }
EXPORT void tempo_tempo(int bpm) { cManager.setTempo(bpm); }
EXPORT void tempo_pattern(int eng, int pattern) { arpSeq.setPattern(eng, pattern); }
EXPORT void tempo_rest_mode(int eng, int mode) { arpSeq.setRestMode(eng, mode); }
EXPORT void tempo_freeze() { fx.toggleGranularFreeze(); }

EXPORT void tempo_set(int eng, int id, float v)
{
    BaseEngine* e = engines[eng & 1];
    switch(id)
    {
        case 0: e->setGlobalPitchFree(v); break;
        case 1: e->setStartPoint(v); break;
        case 2: e->setEndPoint(v); break;
        case 3: e->setAttack(v); break;
        case 4: e->setRelease(v); break;
        case 5: e->setMasterCutoff(v); break;
        case 6: e->setMasterRes(v); break;
        case 7: e->setPan(v); break;
        case 8: e->setSampleReducer(v); break;
        case 9: e->setSampleVolume(v); break;
        case 10: e->setLoop(v); break;
        case 11: e->setSustain(v); break;
        case 20: fx.setGranularMain(v); break;
        case 21: fx.setGranularAlt(v); break;
        case 22: fx.setGranularMix(v, eng & 1); break;
        case 23: fx.setGranularFeedback(v); break;
        case 24: fx.setGain(v); break;
        case 25: fx.setFinalComp(v); break;
        case 26: arpSeq.setRandomness(v, eng & 1); break;
    }
}
