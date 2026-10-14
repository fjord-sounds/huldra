#pragma once
#include <fjord_base/core/Dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace huldra::dsp
{
    using namespace fjord::base::dsp;   // softClip, Random, Smoother, Svf, polyBlep, panGains, DelayLine, Biquad ...

    constexpr int kMaxUnison = 4;
    constexpr int kMaxVoices = 32;

}
