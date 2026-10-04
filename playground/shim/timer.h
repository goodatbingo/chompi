#pragma once
#include <cstdint>
// TEMPO's clockManager reprograms a hardware timer for MIDI clock output.
// There is no MIDI out here, so the timer only records its period.
namespace daisy
{
class TimerHandle
{
  public:
    void     SetPeriod(uint32_t p) { period_ = p; }
    uint32_t GetPeriod() const { return period_; }

  private:
    uint32_t period_ = 0;
};
} // namespace daisy
