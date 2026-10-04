#pragma once
#include <cstdint>
#include <cstddef>

// External SDRAM and the tightly-coupled RAMs are plain memory here.
#define DSY_SDRAM_BSS
#define DSY_SDRAM_DATA
#define DSY_DTCMRAM_BSS
#define DSY_SRAM_BSS
#define FORCE_INLINE inline __attribute__((always_inline))

namespace daisy
{
inline float   s162f(int16_t x) { return static_cast<float>(x) * 3.0517578125e-05f; }
inline int16_t f2s16(float x)
{
    x = x <= -1.f ? -1.f : x >= 1.f ? 1.f : x;
    return static_cast<int16_t>(x * 32767.f);
}
inline float s242f(int32_t x) { return static_cast<float>((x ^ 0x800000) - 0x800000) * 1.192092896e-07f; }
inline float s322f(int32_t x) { return static_cast<float>(x) * 4.6566129e-10f; }
} // namespace daisy
