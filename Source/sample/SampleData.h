#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <atomic>
#include <memory>
#include "../dsp/DspCommon.h"

namespace huldra
{
    // One loaded sound with everything derived from it. Immutable once it is published to the audio thread
    // (only the 'users' counter changes), so voices can read it without locks.
    struct SampleData
    {
        juce::String uid, name;
        juce::File file;                 // empty for recordings, bounces and demo voices
        bool isDemo = false;

        double sampleRate = 48000.0;
        int numFrames = 0;
        int numChannels = 1;
        std::vector<float> left, right;  // right is empty for mono sounds

        // Mip levels (half and quarter rate, low-passed) for clean playback when pitched far up
        std::array<std::vector<float>, 2> mipLeft, mipRight;

        // ---- Analysis ----
        float peak = 0.0f, rms = 0.0f;
        float rootNote = 60.0f;          // detected fundamental (MIDI note, fractional); 60 when unpitched
        float pitchConfidence = 0.0f;    // share of voiced frames
        static constexpr int pitchHop = 256;
        std::vector<float> pitchCurve;   // MIDI note per pitchHop frames, 0 = unvoiced
        static constexpr int noveltyHop = 256;
        std::vector<float> novelty;      // spectral flux (onset strength), normalised 0..1
        std::vector<float> energy;       // RMS envelope per noveltyHop (linear)

        // STFT frames of the mono sum for the spectral engine (magnitude + true frequency in bins)
        static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, numBins = fftSize / 2 + 1, specHop = 512;
        int numSpecFrames = 0;
        std::vector<float> specMag, specFreq;   // numSpecFrames * numBins

        // Overview for the displays: min/max and spectral brightness per bucket
        static constexpr int overviewSize = 2048;
        std::array<float, overviewSize> overviewMin {}, overviewMax {}, overviewBright {};

        // Number of voices playing this sample right now (keeps it alive after it was replaced)
        mutable std::atomic<int> users { 0 };

        float seconds() const { return (float) numFrames / (float) sampleRate; }
        const float* channel (int ch) const { return ch == 1 && ! right.empty() ? right.data() : left.data(); }

        // Pitch (MIDI) at a frame, interpolated; 0 when unvoiced
        float pitchAt (double frame) const
        {
            if (pitchCurve.empty()) return 0.0f;
            const double p = frame / pitchHop;
            const int i = std::clamp ((int) p, 0, (int) pitchCurve.size() - 1);
            const int j = std::min (i + 1, (int) pitchCurve.size() - 1);
            const float a = pitchCurve[(size_t) i], b = pitchCurve[(size_t) j];
            if (a <= 0.0f || b <= 0.0f) return a > 0.0f ? a : b;
            const float f = (float) (p - i);
            return a + (b - a) * std::clamp (f, 0.0f, 1.0f);
        }

        const float* specFrameMag (int frame) const { return specMag.data() + (size_t) std::clamp (frame, 0, numSpecFrames - 1) * numBins; }
        const float* specFrameFreq (int frame) const { return specFreq.data() + (size_t) std::clamp (frame, 0, numSpecFrames - 1) * numBins; }
    };

    using SamplePtr = std::shared_ptr<SampleData>;

    // Keeps a sample alive while a voice plays it.
    struct SampleUse
    {
        const SampleData* s = nullptr;
        void set (const SampleData* n)
        {
            if (n == s) return;
            if (s != nullptr) s->users.fetch_sub (1, std::memory_order_acq_rel);
            s = n;
            if (s != nullptr) s->users.fetch_add (1, std::memory_order_acq_rel);
        }
        ~SampleUse() { set (nullptr); }
    };
}
