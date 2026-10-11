#pragma once
#include "Effects.h"

namespace huldra::fx
{
    // Reorderable effect modules. Indices are stored in "fxOrder" – append only.
    enum class FxType { Saturation, Lofi, Compressor, Squash, Eq, Motion, Delay, Reverb, Width, Count };
    constexpr int kNumFx = (int) FxType::Count;
    using Order = std::array<int, kNumFx>;

    inline const juce::StringArray& fxNames()
    {
        static const juce::StringArray names { "Saturation", "Lo-Fi", "Compressor", "Squash", "EQ", "Motion", "Delay", "Reverb", "Width" };
        return names;
    }

    // The limiter sits after the chain (not reorderable) but is shown as a tenth module in the UI.
    constexpr int kLimiterType = kNumFx;

    inline P::Id fxOnParam (int type)
    {
        static const P::Id ids[] = { P::SatOn, P::LofiOn, P::CompOn, P::SqOn, P::EqOn, P::ChrOn, P::DlyOn, P::VerbOn, P::WidthOn, P::LimOn };
        return ids[type];
    }

    inline juce::String moduleName (int type) { return type == kLimiterType ? juce::String ("Limiter") : fxNames()[type]; }

    // Colour first, then dynamics, then movement and space.
    inline Order defaultOrder()
    {
        return { (int) FxType::Saturation, (int) FxType::Lofi, (int) FxType::Eq, (int) FxType::Compressor, (int) FxType::Squash,
                 (int) FxType::Motion, (int) FxType::Delay, (int) FxType::Reverb, (int) FxType::Width };
    }

    inline juce::String orderToString (const Order& o)
    {
        juce::StringArray s;
        for (int t : o) s.add (juce::String (t));
        return s.joinIntoString (",");
    }

    // Accepts partial / broken strings: missing modules are appended in their default position.
    inline Order orderFromString (const juce::String& text)
    {
        Order o {};
        int n = 0;
        std::array<bool, kNumFx> used {};
        for (const auto& t : juce::StringArray::fromTokens (text, ",", ""))
        {
            const int v = t.getIntValue();
            if (t.trim().containsOnly ("0123456789") && t.trim().isNotEmpty() && v >= 0 && v < kNumFx && ! used[(size_t) v] && n < kNumFx)
            {
                o[(size_t) n++] = v;
                used[(size_t) v] = true;
            }
        }
        for (int t : defaultOrder())
            if (! used[(size_t) t] && n < kNumFx)
                o[(size_t) n++] = t;
        return o;
    }

    // Lock-free values for the live visuals on the effect tiles.
    struct FxMeters
    {
        std::array<std::atomic<float>, kNumFx> in {}, out {};
        std::atomic<float> compReduction { 0.0f }, squash { 0.0f }, reverbTail { 0.0f }, motionPhase { 0.0f }, lofiWow { 0.0f }, clipOver { 0.0f };
    };

    class FxChain
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sat.prepare (sr, maxBlock);
            lofi.prepare (sr);
            comp.prepare (sr);
            squash.prepare (sr);
            eq.prepare (sr);
            motion.prepare (sr);
            delay.prepare (sr);
            reverb.prepare (sr);
            width.prepare (sr);
            wasOn.fill (false);
        }

        void reset()
        {
            sat.reset(); lofi.reset(); comp.reset(); squash.reset(); eq.reset();
            motion.reset(); delay.reset(); reverb.reset(); width.reset();
        }

        void process (StereoBuffer b, const Values& v, const Order& order, double bpm, FxMeters* meters = nullptr)
        {
            for (int type : order)
            {
                const bool on = v.b (fxOnParam (type));
                if (on && ! wasOn[(size_t) type]) resetModule (type);
                wasOn[(size_t) type] = on;
                if (! on) continue;

                float inPeak = 0.0f;
                if (meters != nullptr)
                    for (int i = 0; i < b.n; ++i) inPeak = std::max (inPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));

                switch ((FxType) type)
                {
                    case FxType::Saturation: sat.process (b, v); break;
                    case FxType::Lofi:       lofi.process (b, v); break;
                    case FxType::Compressor: comp.process (b, v); break;
                    case FxType::Squash:     squash.process (b, v); break;
                    case FxType::Eq:         eq.process (b, v); break;
                    case FxType::Motion:     motion.process (b, v); break;
                    case FxType::Delay:      delay.process (b, v, bpm); break;
                    case FxType::Reverb:     reverb.process (b, v); break;
                    case FxType::Width:      width.process (b, v); break;
                    case FxType::Count:
                    default: break;
                }

                if (meters != nullptr)
                {
                    float outPeak = 0.0f;
                    for (int i = 0; i < b.n; ++i) outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
                    meters->in[(size_t) type].store (inPeak, std::memory_order_relaxed);
                    meters->out[(size_t) type].store (outPeak, std::memory_order_relaxed);
                }
            }

            if (v.b (P::LimOn))
                clipper.process (b, v);
            else
                clipper.lastOver = 0.0f;

            if (meters != nullptr)
            {
                meters->compReduction.store (v.b (P::CompOn) ? comp.lastReduction : 0.0f, std::memory_order_relaxed);
                meters->squash.store (v.b (P::SqOn) ? squash.lastActivity : 0.0f, std::memory_order_relaxed);
                meters->reverbTail.store (reverb.tailLevel, std::memory_order_relaxed);
                meters->motionPhase.store (motion.getPhase(), std::memory_order_relaxed);
                meters->lofiWow.store (lofi.getWow(), std::memory_order_relaxed);
                meters->clipOver.store (clipper.lastOver, std::memory_order_relaxed);
            }
        }

    private:
        void resetModule (int type)
        {
            switch ((FxType) type)
            {
                case FxType::Saturation: sat.reset(); break;
                case FxType::Lofi:       lofi.reset(); break;
                case FxType::Compressor: comp.reset(); break;
                case FxType::Squash:     squash.reset(); break;
                case FxType::Eq:         eq.reset(); break;
                case FxType::Motion:     motion.reset(); break;
                case FxType::Delay:      delay.reset(); break;
                case FxType::Reverb:     reverb.reset(); break;
                case FxType::Width:      width.reset(); break;
                case FxType::Count:
                default: break;
            }
        }

        Saturation sat;
        Lofi lofi;
        Compressor comp;
        Squash squash;
        Eq eq;
        Motion motion;
        Delay delay;
        Reverb reverb;
        Width width;
        Clipper clipper;
        std::array<bool, kNumFx> wasOn {};
    };
}
