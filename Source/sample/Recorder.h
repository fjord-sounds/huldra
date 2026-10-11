#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace huldra
{
    // Records the plugin input (side-chain / track input, or the microphone in the standalone app).
    // arm() allocates on the message thread; the audio thread only writes into the prepared buffer.
    class Recorder
    {
    public:
        enum class State { Idle, Waiting, Recording, Finished };
        static constexpr double kMaxSeconds = 120.0;

        // Message thread
        void arm (double sampleRate, bool waitForSignal, float thresholdDb = -40.0f)
        {
            state.store ((int) State::Idle);
            rate = sampleRate;
            const int frames = (int) (sampleRate * kMaxSeconds);
            if (buffer.getNumSamples() < frames) buffer.setSize (2, frames, false, false, true);
            buffer.clear();
            written.store (0);
            threshold = juce::Decibels::decibelsToGain (thresholdDb);
            state.store ((int) (waitForSignal ? State::Waiting : State::Recording));
        }

        void stop()
        {
            const auto s = getState();
            if (s == State::Recording) state.store ((int) State::Finished);
            else if (s == State::Waiting) state.store ((int) State::Idle);
        }

        void cancel() { state.store ((int) State::Idle); written.store (0); }

        // The take, trimmed of silence at both ends (empty when nothing was recorded)
        juce::AudioBuffer<float> takeRecording (double& sampleRateOut)
        {
            sampleRateOut = rate;
            const int n = written.load();
            state.store ((int) State::Idle);
            if (n < 64) return {};
            int start = 0, end = n;
            const float gate = 0.003f;
            while (start < n && std::abs (buffer.getSample (0, start)) < gate && std::abs (buffer.getSample (1, start)) < gate) ++start;
            while (end > start && std::abs (buffer.getSample (0, end - 1)) < gate && std::abs (buffer.getSample (1, end - 1)) < gate) --end;
            start = std::max (0, start - (int) (0.01 * rate));
            end = std::min (n, end + (int) (0.05 * rate));
            if (end - start < 64) return {};
            juce::AudioBuffer<float> out (2, end - start);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, buffer, ch, start, end - start);
            // fades against clicks
            const int f = std::min (out.getNumSamples() / 4, (int) (0.005 * rate));
            out.applyGainRamp (0, f, 0.0f, 1.0f);
            out.applyGainRamp (out.getNumSamples() - f, f, 1.0f, 0.0f);
            return out;
        }

        // Audio thread
        void push (const float* l, const float* r, int n)
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r != nullptr ? r[i] : l[i])));
            inputPeak.store (std::max (inputPeak.load (std::memory_order_relaxed), peak), std::memory_order_relaxed);

            // Monitor ring for the live input waveform
            int w = ringWrite.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) w] = 0.5f * (l[i] + (r != nullptr ? r[i] : l[i]));
                w = (w + 1) & (kRing - 1);
            }
            ringWrite.store (w, std::memory_order_release);

            auto s = getState();
            int start = 0;
            if (s == State::Waiting)
            {
                while (start < n && std::abs (l[start]) < threshold && std::abs (r != nullptr ? r[start] : 0.0f) < threshold) ++start;
                if (start >= n) return;
                state.store ((int) State::Recording);
                s = State::Recording;
            }
            if (s != State::Recording) return;
            int pos = written.load (std::memory_order_relaxed);
            const int count = std::min (n - start, buffer.getNumSamples() - pos);
            if (count > 0)
            {
                buffer.copyFrom (0, pos, l + start, count);
                buffer.copyFrom (1, pos, r != nullptr ? r + start : l + start, count);
                pos += count;
                written.store (pos, std::memory_order_release);
            }
            if (pos >= buffer.getNumSamples()) state.store ((int) State::Finished);
        }

        State getState() const { return (State) state.load(); }
        double recordedSeconds() const { return written.load() / std::max (1.0, rate); }
        float takePeak() { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }

        static constexpr int kRing = 8192;
        void copyRing (float* out, int n) const
        {
            const int end = ringWrite.load (std::memory_order_acquire);
            for (int i = 0; i < n; ++i)
                out[i] = ring[(size_t) ((end - n + i + kRing * 4) & (kRing - 1))];
        }

    private:
        juce::AudioBuffer<float> buffer;
        std::atomic<int> state { (int) State::Idle }, written { 0 }, ringWrite { 0 };
        std::atomic<float> inputPeak { 0.0f };
        std::array<float, kRing> ring {};
        double rate = 48000.0;
        float threshold = 0.01f;
    };
}
