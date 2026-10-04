// Tape Bench: TAPE "lofi" + "filter" FX stage, compiled to WebAssembly.
//
// The DSP blocks below are the firmware's own headers, included straight out of
// firmware/chompi-tape/code/src. Edit DJFilter.h, Warble.h, BasicMMF.h (or the
// vendored DaisySP), rebuild, and the browser plays the edited code.
//
// The per-sample chain in tape_fx_process() and the knob mappings in the
// setters mirror DSPEngine::ApplyFx() and DSPEngine::SetSaturate() etc. in
// firmware/chompi-tape/code/src/DSPEngine.h. DSPEngine.h pulls in libDaisy
// hardware drivers, so it can't be compiled here directly. If you change the
// chain or a mapping there, change it here too.

#include <cstddef>
#include <cstdint>

#include "dsp.h"       // DaisySP: fonepole, fclamp, SoftClip
#include "delayline.h" // DaisySP: DelayLine (used by Warble)
#include "dcblock.h"   // DaisySP: DcBlock

// Warble.h calls DelayLine::SetDelay(10u). On the Daisy's ARM toolchain size_t
// is unsigned int, so that picks the size_t overload; on wasm32 size_t is
// unsigned long and the call is ambiguous. This shim sits in namespace chompi,
// where Warble's unqualified DelayLine finds it before daisysp's, and adds the
// exact-match overload so the firmware header compiles unmodified.
namespace chompi
{
template <typename T, size_t max_size>
class DelayLine : public daisysp::DelayLine<T, max_size>
{
  public:
    using daisysp::DelayLine<T, max_size>::SetDelay;
    void SetDelay(unsigned int delay) { SetDelay(static_cast<size_t>(delay)); }
};
} // namespace chompi

#include "BasicMMF.h"
#include "DJFilter.h"
#include "Warble.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
// Must match the firmware's audio rate: Warble hard-codes 1/48000.
constexpr float  kSampleRate = 48000.f;
constexpr size_t kMaxBlock   = 128; // one Web Audio render quantum

float in_l[kMaxBlock], in_r[kMaxBlock];
float out_l[kMaxBlock], out_r[kMaxBlock];

DjFilter        filter_;
chompi::Warble  warble_;
daisysp::DcBlock dcblock_fx_l_, dcblock_fx_r_;

float cutoff_, cutoff_target_;
float res_, res_target_;
float saturate_amt_, saturate_amt_target_;

bool bypass_ = false;
} // namespace

EXPORT float *tape_fx_in_l() { return in_l; }
EXPORT float *tape_fx_in_r() { return in_r; }
EXPORT float *tape_fx_out_l() { return out_l; }
EXPORT float *tape_fx_out_r() { return out_r; }
EXPORT int    tape_fx_max_block() { return kMaxBlock; }
EXPORT float  tape_fx_sample_rate() { return kSampleRate; }

// DSPEngine::SetFilter
EXPORT void tape_fx_set_filter(float val) { cutoff_target_ = val; }

// DSPEngine::SetFilterResonance
EXPORT void tape_fx_set_resonance(float val) { res_target_ = val; }

// DSPEngine::SetSaturate
EXPORT void tape_fx_set_saturate(float val)
{
    val                  = logf(1.7f * val + 1.f);
    saturate_amt_target_ = val * 13.f + 1.f;
}

// DSPEngine::SetWarble
EXPORT void tape_fx_set_warble(float val) { warble_.SetFreq(val); }

EXPORT void tape_fx_set_bypass(int on) { bypass_ = on != 0; }

// Mirrors the relevant part of DSPEngine::Init(), with the panel's
// "magic wand" reset values from ui.h enc_defaults.
EXPORT void tape_fx_init()
{
    filter_.Init(kSampleRate);
    filter_.SetControl(.5f);
    cutoff_ = cutoff_target_ = .5f;
    res_ = res_target_ = 0.f;

    tape_fx_set_saturate(0.f);
    saturate_amt_ = saturate_amt_target_;

    warble_.Init(kSampleRate);
    warble_.SetFreq(0.f);

    dcblock_fx_l_.Init(kSampleRate);
    dcblock_fx_r_.Init(kSampleRate);
}

// First loop of DSPEngine::ApplyFx(). fx_env_ is left out (it is 1 outside
// of record/resample fades); delay and reverb are later loops.
EXPORT void tape_fx_process(int size)
{
    if(size > static_cast<int>(kMaxBlock))
        size = kMaxBlock;

    for(int i = 0; i < size; i++)
    {
        float l = in_l[i];
        float r = in_r[i];

        // slew controls at audio rate
        fonepole(cutoff_, cutoff_target_, .001f);
        fonepole(res_, res_target_, .001f);
        fonepole(saturate_amt_, saturate_amt_target_, .001f);

        filter_.SetControl(cutoff_);
        filter_.SetRes(res_);

        l = dcblock_fx_l_.Process(l);
        r = dcblock_fx_r_.Process(r);

        // filter
        filter_.Process(l, r, &l, &r);

        // then clip
        l = daisysp::SoftClip(saturate_amt_ * l);
        r = daisysp::SoftClip(saturate_amt_ * r);

        // reduce amplitude to prevent LUFs from blowing up
        const float gain = 1.f - daisysp::SoftClip(.4f * (saturate_amt_ - 1.f)) * .7f;
        l *= gain;
        r *= gain;

        // wow and flutter
        warble_.Process(l, r, &l, &r);

        // Keep the engine running while bypassed so A/B switches are seamless.
        out_l[i] = bypass_ ? in_l[i] : l;
        out_r[i] = bypass_ ? in_r[i] : r;
    }
}
