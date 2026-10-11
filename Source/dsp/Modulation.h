#pragma once
#include "DspCommon.h"
#include "../Parameters.h"

namespace huldra::dsp
{
    namespace P = params;

    // LFO note lengths in quarter notes (order of the "lfoN_beats" choices)
    inline double lfoBeats (int index)
    {
        static const double beats[] = { 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 3, 1.5, 0.75 };
        return beats[std::clamp (index, 0, 15)];
    }

    // Low-frequency oscillator, bipolar output -1..1.
    struct Lfo
    {
        float phase = 0.0f, held = 0.0f, from = 0.0f, to = 0.0f;
        Random rng;

        void reset (float startPhase, uint32_t seed)
        {
            phase = startPhase;
            rng.seed (seed);
            held = rng.nextBipolar();
            from = rng.nextBipolar();
            to = rng.nextBipolar();
        }

        // Advances by 'inc' cycles; returns true when the cycle wrapped
        bool advance (float inc)
        {
            phase += inc;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                held = rng.nextBipolar();
                from = to;
                to = rng.nextBipolar();
                return true;
            }
            return false;
        }

        void setPhase (float p)
        {
            const float wrapped = p - std::floor (p);
            if (wrapped < phase - 0.5f) { held = rng.nextBipolar(); from = to; to = rng.nextBipolar(); }
            phase = wrapped;
        }

        float value (int shape) const
        {
            const float p = phase;
            switch (shape)
            {
                case 0:  return std::sin (kTwoPi * p);
                case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f) ;
                case 2:  return 2.0f * p - 1.0f;
                case 3:  return 1.0f - 2.0f * p;
                case 4:  return p < 0.5f ? 1.0f : -1.0f;
                case 5:  return held;
                default: { const float t = 0.5f - 0.5f * std::cos (kPi * p); return from + (to - from) * t; }
            }
        }
    };

    // Rate of an LFO in cycles per second
    inline float lfoRate (const P::Values& v, int lfo, double bpm)
    {
        if (v.b (P::lfoParam (lfo, 2)))
            return (float) (bpm / 60.0 / lfoBeats (v.i (P::lfoParam (lfo, 3))));
        return v[P::lfoParam (lfo, 1)];
    }

    // The active rows of the mod matrix, prepared once per block.
    struct ModRouting
    {
        struct Slot { int src = 0; P::Id dst = P::MasterGain; float amt = 0.0f; };
        std::array<Slot, P::kNumModSlots> slots {};
        int numSlots = 0;
        std::array<bool, P::kNumModSources> used {};

        void prepare (const P::Values& v)
        {
            numSlots = 0;
            used.fill (false);
            const auto& targets = P::modTargets();
            for (int s = 0; s < P::kNumModSlots; ++s)
            {
                const int src = v.i (P::modSrc (s));
                const int dst = v.i (P::modDst (s));
                const float amt = v[P::modAmt (s)];
                if (src <= 0 || dst <= 0 || dst > (int) targets.size() || std::abs (amt) < 1.0e-4f) continue;
                slots[(size_t) numSlots++] = { src, targets[(size_t) dst - 1], amt };
                used[(size_t) src] = true;
            }
        }
    };

    // Applies the routing to a set of values in normalised space. Only targets inside [first, last] are touched.
    template <typename GetBase, typename SetValue>
    inline void applyModulation (const ModRouting& routing, const float* sources, int first, int last, GetBase&& base, SetValue&& set)
    {
        std::array<float, P::Count> offset;
        std::array<P::Id, P::kNumModSlots> touched {};
        int numTouched = 0;
        for (int i = 0; i < routing.numSlots; ++i)
        {
            const auto& s = routing.slots[(size_t) i];
            if (s.dst < first || s.dst > last) continue;
            bool seen = false;
            for (int t = 0; t < numTouched; ++t) seen |= touched[(size_t) t] == s.dst;
            if (! seen) { touched[(size_t) numTouched++] = s.dst; offset[(size_t) s.dst] = 0.0f; }
            offset[(size_t) s.dst] += s.amt * sources[s.src];
        }
        for (int t = 0; t < numTouched; ++t)
        {
            const auto id = touched[(size_t) t];
            const auto& spec = P::spec (id);
            const float n = P::toNormalised (spec, base (id)) + offset[(size_t) id];
            set (id, P::fromNormalised (spec, std::clamp (n, 0.0f, 1.0f)));
        }
    }
}
