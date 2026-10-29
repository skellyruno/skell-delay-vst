#pragma once

#include <cstdint>

// Rhythmic gate patterns: 16 steps = one bar of 4/4 (each step is a 1/16 note). Bit n set = step n open.
constexpr std::uint16_t gateBits (const char* s)
{
    std::uint16_t v = 0;
    for (int i = 0; i < 16 && s[i] != 0; ++i)
        if (s[i] == 'x')
            v = static_cast<std::uint16_t> (v | (1u << i));
    return v;
}

inline constexpr int kNumGatePatterns = 7;
inline constexpr const char* kGateNames[kNumGatePatterns] = { "Off", "1/8", "1/16", "Offbeat", "Dotted", "Trance", "Sync" };
inline constexpr std::uint16_t kGateMasks[kNumGatePatterns] = {
    0xFFFF,
    gateBits ("xx..xx..xx..xx.."),   // 1/8
    gateBits ("x.x.x.x.x.x.x.x."),   // 1/16
    gateBits ("..xx..xx..xx..xx"),   // offbeat
    gateBits ("xx.xx.xx.xx.xx.."),   // dotted (3-3-3-3-4)
    gateBits ("xxx.xxx.xxx.xxx."),   // trance
    gateBits ("x.xx..x.x.xx..x.")    // syncopated
};
