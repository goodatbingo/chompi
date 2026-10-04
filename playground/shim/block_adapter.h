#pragma once
// Runs firmware audio callbacks at the hardware's block size.
//
// The Daisy Seed calls the audio callback with 48-frame blocks (libDaisy's
// default), and the firmware depends on it: TEMPO's FxEngine has fixed
// 48-sample buffers and its clock counts blocks. Web Audio renders 128-frame
// quanta, so this adapter buffers one hardware block in each direction and
// calls the engine every 48 frames, adding 1 ms of latency.
#include <cstddef>
#include <cstdint>
#include "system.h"

namespace tapebench
{
constexpr size_t kHardwareBlock = 48;
constexpr size_t kHostQuantum   = 128;

class BlockAdapter
{
  public:
    using Callback = void (*)(float** in, float** out, size_t size);

    explicit BlockAdapter(Callback cb) : cb_(cb)
    {
        for(int c = 0; c < 4; c++)
        {
            in_ptrs_[c]  = blk_in_[c];
            out_ptrs_[c] = blk_out_[c];
        }
    }

    float* HostIn(int ch) { return host_in_[ch & 3]; }
    float* HostOut(int ch) { return host_out_[ch & 3]; }

    void Render(size_t frames)
    {
        if(frames > kHostQuantum)
            frames = kHostQuantum;
        for(size_t i = 0; i < frames; i++)
        {
            if(pos_ == kHardwareBlock)
            {
                cb_(in_ptrs_, out_ptrs_, kHardwareBlock);
                daisy::System::Advance(1000); // 48 frames at 48 kHz
                pos_ = 0;
            }
            for(int c = 0; c < 4; c++)
            {
                blk_in_[c][pos_] = host_in_[c][i];
                host_out_[c][i]  = blk_out_[c][pos_];
            }
            pos_++;
        }
    }

  private:
    Callback cb_;
    size_t   pos_ = 0;
    float    blk_in_[4][kHardwareBlock]  = {};
    float    blk_out_[4][kHardwareBlock] = {};
    float*   in_ptrs_[4];
    float*   out_ptrs_[4];
    float    host_in_[4][kHostQuantum]  = {};
    float    host_out_[4][kHostQuantum] = {};
};
} // namespace tapebench
