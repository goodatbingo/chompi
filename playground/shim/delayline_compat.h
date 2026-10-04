#pragma once
// Warble.h calls DelayLine::SetDelay(10u). On the Daisy's ARM toolchain size_t
// is unsigned int, so that picks the size_t overload; on wasm32 size_t is
// unsigned long and the call is ambiguous. This shim sits in namespace chompi,
// where Warble's unqualified DelayLine finds it before daisysp's, and adds the
// exact-match overload so the firmware header compiles unmodified.
#include "Utility/delayline.h"

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
