#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include "DspCommon.h"

namespace huldra::dsp
{
    // Multi-segment envelopes (MSEG 1-3) drawn on the Mod page. Times are 0..1 of the MSEG length parameter.
    constexpr int kNumMsegs = 3;
    constexpr int kMaxEnvPoints = 16;

    // One breakpoint: time 0..1, level 0..1, and the curve of the segment that ends here
    // (-1 slow start .. 0 linear .. +1 fast start).
    struct EnvPoint
    {
        float time = 0.0f, level = 0.0f, curve = 0.0f;
        bool operator== (const EnvPoint& o) const { return juce::exactlyEqual (time, o.time) && juce::exactlyEqual (level, o.level) && juce::exactlyEqual (curve, o.curve); }
    };

    struct EnvShape
    {
        std::array<EnvPoint, kMaxEnvPoints> points {};
        int numPoints = 0;

        bool operator== (const EnvShape& o) const
        {
            if (numPoints != o.numPoints) return false;
            for (int i = 0; i < numPoints; ++i)
                if (! (points[(size_t) i] == o.points[(size_t) i])) return false;
            return true;
        }

        static float shapeCurve (float x, float curve)
        {
            if (std::abs (curve) < 0.001f) return x;
            const float k = curve * 9.0f;
            return (1.0f - std::exp (-k * x)) / (1.0f - std::exp (-k));
        }

        // Level at a position 0..1
        float valueAt (float t) const
        {
            if (numPoints == 0) return 0.0f;
            if (t <= points[0].time) return points[0].level;
            for (int i = 1; i < numPoints; ++i)
            {
                const auto& b = points[(size_t) i];
                if (t < b.time)
                {
                    const auto& a = points[(size_t) i - 1];
                    const float len = b.time - a.time;
                    const float x = len > 0.0f ? (t - a.time) / len : 1.0f;
                    return a.level + (b.level - a.level) * shapeCurve (x, b.curve);
                }
            }
            return points[(size_t) numPoints - 1].level;
        }

        // First point at 0, last point at 1, times rising, values in range.
        void sanitise()
        {
            numPoints = std::clamp (numPoints, 2, kMaxEnvPoints);
            points[0].time = 0.0f;
            for (int i = 0; i < numPoints; ++i)
            {
                auto& p = points[(size_t) i];
                p.level = std::clamp (p.level, 0.0f, 1.0f);
                p.curve = std::clamp (p.curve, -1.0f, 1.0f);
                p.time = std::clamp (p.time, i == 0 ? 0.0f : points[(size_t) i - 1].time, 1.0f);
            }
            points[(size_t) numPoints - 1].time = 1.0f;
        }

        juce::String toString() const
        {
            juce::StringArray parts;
            for (int i = 0; i < numPoints; ++i)
            {
                const auto& p = points[(size_t) i];
                parts.add (juce::String (p.time, 4) + ":" + juce::String (p.level, 4) + ":" + juce::String (p.curve, 3));
            }
            return parts.joinIntoString (" ");
        }

        static EnvShape fromString (const juce::String& text, const EnvShape& fallback)
        {
            EnvShape s;
            for (const auto& token : juce::StringArray::fromTokens (text, " ", ""))
            {
                const auto v = juce::StringArray::fromTokens (token, ":", "");
                if (v.size() < 2 || s.numPoints >= kMaxEnvPoints) continue;
                s.points[(size_t) s.numPoints++] = { v[0].getFloatValue(), v[1].getFloatValue(), v.size() > 2 ? v[2].getFloatValue() : 0.0f };
            }
            if (s.numPoints < 2) return fallback;
            s.sanitise();
            return s;
        }

        static EnvShape make (std::initializer_list<EnvPoint> pts)
        {
            EnvShape s;
            for (const auto& p : pts)
                if (s.numPoints < kMaxEnvPoints)
                    s.points[(size_t) s.numPoints++] = p;
            s.sanitise();
            return s;
        }
    };

    // Shapes of a fresh "Init" sound: a swell, a ramp down and a gentle wave.
    inline EnvShape defaultMseg (int index)
    {
        switch (index)
        {
            case 0:  return EnvShape::make ({ { 0.0f, 0.0f, 0.0f }, { 0.45f, 1.0f, -0.4f }, { 1.0f, 0.0f, 0.4f } });
            case 1:  return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.5f } });
            default: return EnvShape::make ({ { 0.0f, 0.5f, 0.0f }, { 0.25f, 1.0f, 0.3f }, { 0.5f, 0.5f, -0.3f },
                                              { 0.75f, 0.0f, 0.3f }, { 1.0f, 0.5f, -0.3f } });
        }
    }

    // Lock-free hand-over of the MSEG shapes from the message thread to the audio thread (sequence lock).
    class EnvelopeStore
    {
    public:
        EnvelopeStore()
        {
            for (int e = 0; e < kNumMsegs; ++e)
                write (e, defaultMseg (e));
        }

        void write (int e, const EnvShape& s)
        {
            auto& slot = slots[(size_t) e];
            slot.seq.fetch_add (1, std::memory_order_acq_rel);
            slot.count.store (s.numPoints, std::memory_order_relaxed);
            for (int i = 0; i < kMaxEnvPoints; ++i)
            {
                slot.t[(size_t) i].store (s.points[(size_t) i].time, std::memory_order_relaxed);
                slot.l[(size_t) i].store (s.points[(size_t) i].level, std::memory_order_relaxed);
                slot.c[(size_t) i].store (s.points[(size_t) i].curve, std::memory_order_relaxed);
            }
            slot.seq.fetch_add (1, std::memory_order_acq_rel);
        }

        EnvShape read (int e) const
        {
            const auto& slot = slots[(size_t) e];
            EnvShape s;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = slot.seq.load (std::memory_order_acquire);
                s.numPoints = std::clamp (slot.count.load (std::memory_order_relaxed), 2, kMaxEnvPoints);
                for (int i = 0; i < kMaxEnvPoints; ++i)
                    s.points[(size_t) i] = { slot.t[(size_t) i].load (std::memory_order_relaxed),
                                             slot.l[(size_t) i].load (std::memory_order_relaxed),
                                             slot.c[(size_t) i].load (std::memory_order_relaxed) };
                if ((before & 1u) == 0 && slot.seq.load (std::memory_order_acquire) == before)
                    break;
            }
            return s;
        }

        uint32_t version() const
        {
            uint32_t v = 0;
            for (const auto& s : slots) v += s.seq.load (std::memory_order_relaxed);
            return v;
        }

    private:
        struct Slot
        {
            std::atomic<uint32_t> seq { 0 };
            std::atomic<int> count { 0 };
            std::array<std::atomic<float>, kMaxEnvPoints> t {}, l {}, c {};
        };
        std::array<Slot, kNumMsegs> slots;
    };

    // Plays an MSEG at control rate: one-shot (holds the last level) or looping.
    class MsegPlayer
    {
    public:
        void start() { pos = 0.0f; }

        // Advances by 'seconds' and returns the level. length = MSEG length in seconds.
        float advance (const EnvShape& shape, float seconds, float length, bool loop)
        {
            const float v = shape.valueAt (pos);
            pos += seconds / std::max (0.01f, length);
            if (pos >= 1.0f)
                pos = loop ? pos - std::floor (pos) : 1.0f;
            return v;
        }

        float position() const { return pos; }

    private:
        float pos = 0.0f;
    };

    // Linear-segment ADSR with exponential-ish decay/release, evaluated per sample.
    class Adsr
    {
    public:
        enum class Stage { Idle, Attack, Decay, Sustain, Release };

        void setSampleRate (float sr) { sampleRate = sr; }

        void noteOn (bool legato = false)
        {
            if (! legato || stage == Stage::Idle) { stage = Stage::Attack; if (! legato) value = std::min (value, 1.0f); }
        }

        void noteOff() { if (stage != Stage::Idle) stage = Stage::Release; }
        void kill() { stage = Stage::Idle; value = 0.0f; }

        void setParams (float a, float d, float s, float r)
        {
            attackInc = 1.0f / std::max (1.0f, a * sampleRate);
            decayCoef = std::exp (-4.6f / std::max (1.0f, d * sampleRate));
            sustain = s;
            releaseCoef = std::exp (-6.9f / std::max (1.0f, r * sampleRate));
        }

        float next()
        {
            switch (stage)
            {
                case Stage::Attack:
                    value += attackInc;
                    if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
                    break;
                case Stage::Decay:
                    value = sustain + (value - sustain) * decayCoef;
                    if (std::abs (value - sustain) < 1.0e-4f) { value = sustain; stage = Stage::Sustain; }
                    break;
                case Stage::Sustain:
                    value += (sustain - value) * 0.001f;
                    break;
                case Stage::Release:
                    value *= releaseCoef;
                    if (value < 1.0e-4f) { value = 0.0f; stage = Stage::Idle; }
                    break;
                case Stage::Idle:
                default: break;
            }
            return value;
        }

        bool isActive() const { return stage != Stage::Idle; }
        bool isReleasing() const { return stage == Stage::Release; }
        float current() const { return value; }
        Stage getStage() const { return stage; }

    private:
        float sampleRate = 48000.0f, value = 0.0f, attackInc = 1.0f, decayCoef = 0.0f, sustain = 1.0f, releaseCoef = 0.0f;
        Stage stage = Stage::Idle;
    };
}
