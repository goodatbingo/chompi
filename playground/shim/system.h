#pragma once
#include <cstdint>

namespace daisy
{
// Time comes from the audio clock: the host advances it once per block.
class System
{
  public:
    static uint32_t GetNow() { return static_cast<uint32_t>(us_ / 1000); }
    static uint32_t GetUs() { return static_cast<uint32_t>(us_); }
    static uint32_t GetTick() { return static_cast<uint32_t>(us_ * 200); }
    static uint32_t GetTickFreq() { return 200000000; }
    static void     Delay(uint32_t ms) { us_ += uint64_t(ms) * 1000; }
    static void     DelayUs(uint32_t us) { us_ += us; }
    static void     DelayTicks(uint32_t t) { us_ += t / 200; }
    static void     Advance(uint64_t us) { us_ += us; }

  private:
    static inline uint64_t us_ = 0;
};
} // namespace daisy
