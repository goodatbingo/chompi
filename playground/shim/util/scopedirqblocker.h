#pragma once
// Single-threaded host: there are no interrupts to block.
namespace daisy
{
class ScopedIrqBlocker
{
  public:
    ScopedIrqBlocker() {}
    ~ScopedIrqBlocker() {}
};
} // namespace daisy
