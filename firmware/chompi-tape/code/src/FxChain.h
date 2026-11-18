/** FxChain
 *  The main effects chain, moved out of DSPEngine:
 *
 *    fade -> dc block -> DJ filter -> saturation -> warble
 *         -> [transform] -> delay -> reverb
 *
 *  The optional transform stage is empty by default, which leaves the chain
 *  sounding exactly as before.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "Warble.h"
#include "reverb.h"
#include "InterpolatedDelayLine.h"
#include "Transforms.h"

static constexpr size_t kMaxDelayTime = 48128 * 2; // stereo, > 1 seconds at 48kHz

namespace daisy
{
    class FxChain
    {
    public:
        FxChain() {}
        ~FxChain() {}

        void Init(float samplerate,
                  daisysp::Reverb* reverb,
                  chompi::InterpolatedDelayLine::AudioSample* del)
        {
            reverb_ = reverb;

            reverb_->Init(samplerate);
            reverb_->SetAmount(0.f);
            reverb_->SetInputGain(.3f);
            reverb_->SetLowpass(1.f);

            del_.Init(del, kMaxDelayTime);
            del_.SetDelay(kMaxDelayTime * .5f);

            SetDelayTime(.5f);
            dly_time_ = dly_time_target_;

            filter_.Init(samplerate);
            filter_.SetControl(.5f);
            cutoff_target_ = .5f;
            res_ = res_target_ = 0.f;

            SetSaturate(0.f);

            warble_.Init(samplerate);
            warble_.SetFreq(.1f);

            dcblock_l_.Init(samplerate);
            dcblock_r_.Init(samplerate);

            env_ = env_target_ = 1.f;
            transform_ = nullptr;
        }

        void Process(float* outl, float* outr, size_t size)
        {
            for(size_t i = 0; i < size; i++)
            {
                // slew controls at audio rate
                daisysp::fonepole(cutoff_, cutoff_target_, .001f);
                daisysp::fonepole(res_, res_target_, .001f);
                daisysp::fonepole(saturate_amt_, saturate_amt_target_, .001f);

                // input gain
                daisysp::fonepole(env_, env_target_, .001f);
                outl[i] *= env_;
                outr[i] *= env_;

                filter_.SetControl(cutoff_);
                filter_.SetRes(res_);

                outl[i] = dcblock_l_.Process(outl[i]);
                outr[i] = dcblock_r_.Process(outr[i]);

                // filter
                filter_.Process(outl[i], outr[i], &outl[i], &outr[i]);

                // then clip
                outl[i] = daisysp::SoftClip(saturate_amt_ * outl[i]);
                outr[i] = daisysp::SoftClip(saturate_amt_ * outr[i]);

                // reduce amplitude to prevent LUFs from blowing up
                const float gain = 1.f - daisysp::SoftClip(.4f * (saturate_amt_ - 1.f)) * .7f;
                outl[i] *= gain;
                outr[i] *= gain;

                // wow and flutter
                warble_.Process(outl[i], outr[i], &outl[i], &outr[i]);
            }

            // optional transform stage, ahead of the time-based effects so
            // the delay and reverb tails carry the transformed sound
            if(transform_ != nullptr)
                transform_->Process(outl, outr, size);

            // delay, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                // dly_amt_ is a duplicate of reverb_amt_, but we want to fonepole it in this loop
                daisysp::fonepole(dly_feedback_, dly_feedback_target_, .001f);
                daisysp::fonepole(dly_time_, dly_time_target_, .001f);
                daisysp::fonepole(dly_amt_, dly_amt_target_, .001f);
                del_.SetDelay(dly_time_);

                // read
                const float del_vol = dly_feedback_ < .2f ? dly_feedback_ * 5.f : 1.f;
                chompi::InterpolatedDelayLine::AudioSample del_read = del_.Read();
                const float delsig_l = s162f(del_read.l) * del_vol;
                const float delsig_r = s162f(del_read.r) * del_vol;

                // write
                const float mono_sum = (outl[i] + outr[i]) * .5f;
                const float del_in = mono_sum + delsig_r * powf(dly_feedback_, .7f);
                const chompi::InterpolatedDelayLine::AudioSample del_write = {int16_t(f2s16(del_in)), int16_t(f2s16(delsig_l))};
                del_.Write(del_write);

                // dry/wet mix
                const float wet_mix = dly_feedback_ > .25f ? .5f : 2.f * dly_feedback_; // quickly to 50%
                const float dry_mix = dly_feedback_ > .83f ? .5f : (1 - .6f * dly_feedback_); // slowly to 50%

                outl[i] = outl[i] * dry_mix + delsig_l * wet_mix;
                outr[i] = outr[i] * dry_mix + delsig_r * wet_mix;
            }

            // reverb, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                daisysp::fonepole(reverb_amt_, reverb_amt_target_, .001f);
                daisysp::fonepole(reverb_time_, reverb_time_target_, .001f);
                reverb_->SetAmount(reverb_amt_ * reverb_amt_ * .8f);
                reverb_->SetTime(reverb_time_);
                reverb_->SetLowpass(reverb_amt_ * .6f + .4f);
                reverb_->SetDiffusion(reverb_amt_ * .6f);

                reverb_->Process(&outl[i], &outr[i]);
            }
        }

        /** Controls */
        inline void SetReverb(float val) { dly_amt_target_ = reverb_amt_target_ = 1.3f * logf(val + 1.f); }
        inline void SetDelayFeedback(float val) { dly_feedback_target_ = val * .9f; }
        inline void SetDelayTime(float val)
        {
            dly_time_target_ = .99f * powf(val, 3.f) * kMaxDelayTime + 450;

            reverb_time_target_ = val;
            reverb_time_target_ = daisysp::fclamp(reverb_time_target_, .05, .97);
        }

        inline void SetWarble(float val) { warble_.SetFreq(val); }

        inline void SetFilter(float val) { cutoff_target_ = val; }
        inline void SetFilterResonance(float val) { res_target_ = val; }
        inline void SetSaturate(float val)
        {
            val = logf(1.7f * val + 1.f);
            saturate_amt_target_ = val * 13.f + 1.f;
        }

        /** Install a real-time transform stage, or nullptr to bypass it. */
        inline void SetTransform(FxTransform* t) { transform_ = t; }

        /** Fade used to move the chain before/after the looper without clicks:
         *  fade out, swap position once faded, then fade back in. */
        inline void FadeOut() { env_target_ = 0.f; }
        inline void FadeIn() { env_target_ = 1.f; }
        inline bool IsFadedOut() const { return env_ < .01f; }

    private:
        DjFilter filter_;
        daisysp::Reverb* reverb_;
        chompi::InterpolatedDelayLine del_;
        Warble warble_;
        daisysp::DcBlock dcblock_l_;
        daisysp::DcBlock dcblock_r_;
        FxTransform* transform_;

        float dly_time_, dly_time_target_;
        float dly_feedback_, dly_feedback_target_;
        float reverb_amt_, reverb_amt_target_;
        float reverb_time_, reverb_time_target_;
        float dly_amt_, dly_amt_target_;
        float saturate_amt_, saturate_amt_target_;
        float cutoff_, cutoff_target_;
        float res_, res_target_;

        float env_, env_target_;

        /** No copy, no assign */
        FxChain(const FxChain &);
        void operator=(const FxChain &);
    };
} // namespace daisy
