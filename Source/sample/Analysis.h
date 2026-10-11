#pragma once
#include <juce_dsp/juce_dsp.h>
#include <numeric>
#include "SampleData.h"

namespace huldra::analysis
{
    // Everything here runs on a background thread when a sound is loaded, recorded or bounced.

    namespace detail
    {
        // Windowed-sinc low-pass (cutoff relative to the sample rate), used before decimating.
        inline std::vector<float> lowpassKernel (int taps, float cutoff)
        {
            std::vector<float> k ((size_t) taps);
            const int m = taps / 2;
            float sum = 0.0f;
            for (int i = 0; i < taps; ++i)
            {
                const float x = (float) (i - m);
                const float sinc = std::abs (x) < 1.0e-6f ? 2.0f * cutoff : std::sin (dsp::kTwoPi * cutoff * x) / (dsp::kPi * x);
                const float w = 0.42f - 0.5f * std::cos (dsp::kTwoPi * (float) i / (float) (taps - 1))
                                + 0.08f * std::cos (2.0f * dsp::kTwoPi * (float) i / (float) (taps - 1));
                k[(size_t) i] = sinc * w;
                sum += k[(size_t) i];
            }
            for (auto& v : k) v /= sum;
            return k;
        }

        // Low-pass and keep every 'factor'-th sample.
        inline std::vector<float> decimate (const std::vector<float>& in, int factor)
        {
            if (factor <= 1) return in;
            const auto k = lowpassKernel (factor * 16 + 1, 0.45f / (float) factor);
            const int m = (int) k.size() / 2;
            const int n = (int) in.size();
            std::vector<float> out ((size_t) (n / factor));
            for (int o = 0; o < (int) out.size(); ++o)
            {
                const int c = o * factor;
                float acc = 0.0f;
                for (int t = 0; t < (int) k.size(); ++t)
                {
                    const int idx = c + t - m;
                    if (idx >= 0 && idx < n) acc += in[(size_t) idx] * k[(size_t) t];
                }
                out[(size_t) o] = acc;
            }
            return out;
        }

        inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / 440.0f); }

        inline void hann (std::vector<float>& w)
        {
            const int n = (int) w.size();
            for (int i = 0; i < n; ++i)
                w[(size_t) i] = 0.5f - 0.5f * std::cos (dsp::kTwoPi * (float) i / (float) n);
        }

        inline std::vector<float> monoSum (const SampleData& s)
        {
            std::vector<float> m (s.left);
            if (! s.right.empty())
                for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (m[i] + s.right[i]);
            return m;
        }
    }

    // Half- and quarter-rate copies for playback far above the root (avoids aliasing).
    inline void buildMips (SampleData& s)
    {
        s.mipLeft[0] = detail::decimate (s.left, 2);
        s.mipLeft[1] = detail::decimate (s.mipLeft[0], 2);
        if (! s.right.empty())
        {
            s.mipRight[0] = detail::decimate (s.right, 2);
            s.mipRight[1] = detail::decimate (s.mipRight[0], 2);
        }
    }

    // Onset strength (log spectral flux), RMS envelope and spectral brightness per noveltyHop.
    inline void analyseNovelty (SampleData& s, const std::vector<float>& mono, std::vector<float>& brightness)
    {
        constexpr int order = 10, n = 1 << order, hop = SampleData::noveltyHop;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) n), buf ((size_t) n * 2), prev ((size_t) n / 2 + 1, 0.0f);
        detail::hann (win);
        const int frames = std::max (1, (int) mono.size() / hop);
        s.novelty.assign ((size_t) frames, 0.0f);
        s.energy.assign ((size_t) frames, 0.0f);
        brightness.assign ((size_t) frames, 0.0f);

        for (int f = 0; f < frames; ++f)
        {
            const int centre = f * hop;
            double e = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const int idx = centre + i - n / 2;
                const float x = idx >= 0 && idx < (int) mono.size() ? mono[(size_t) idx] : 0.0f;
                buf[(size_t) i] = x * win[(size_t) i];
                if (i >= n / 2 - hop / 2 && i < n / 2 + hop / 2) e += (double) x * x;
            }
            s.energy[(size_t) f] = (float) std::sqrt (e / hop);
            std::fill (buf.begin() + n, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            float flux = 0.0f, num = 0.0f, den = 0.0f;
            for (int k = 1; k <= n / 2; ++k)
            {
                const float mag = std::log1p (buf[(size_t) k] * 10.0f);
                flux += std::max (0.0f, mag - prev[(size_t) k]);
                prev[(size_t) k] = mag;
                num += (float) k * buf[(size_t) k];
                den += buf[(size_t) k];
            }
            s.novelty[(size_t) f] = flux;
            const float centroidHz = den > 1.0e-6f ? num / den * (float) s.sampleRate / (float) n : 0.0f;
            brightness[(size_t) f] = std::clamp ((std::log2 (std::max (100.0f, centroidHz)) - std::log2 (300.0f)) / 5.0f, 0.0f, 1.0f);
        }

        const float maxNov = *std::max_element (s.novelty.begin(), s.novelty.end());
        if (maxNov > 0.0f)
            for (auto& v : s.novelty) v /= maxNov;
    }

    // YIN pitch tracking on a 16 kHz copy, FFT-based difference function. Fills pitchCurve, rootNote, confidence.
    inline void analysePitch (SampleData& s, const std::vector<float>& mono)
    {
        const int factor = std::max (1, (int) std::round (s.sampleRate / 16000.0));
        const auto low = detail::decimate (mono, factor);
        const double sr = s.sampleRate / factor;
        constexpr int W = 512, order = 11, N = 1 << order;   // N >= 2W + W
        const int maxTau = std::min (W - 2, (int) (sr / 60.0)), minTau = std::max (2, (int) (sr / 1400.0));
        juce::dsp::FFT fft (order);
        std::vector<float> a ((size_t) N * 2), b ((size_t) N * 2), d ((size_t) W);
        const int frames = std::max (1, s.numFrames / SampleData::pitchHop);
        s.pitchCurve.assign ((size_t) frames, 0.0f);

        float peakAbs = 0.0f;
        for (float v : low) peakAbs = std::max (peakAbs, std::abs (v));
        const float gate = std::max (1.0e-4f, peakAbs * 0.02f);

        std::vector<float> weights;
        std::vector<float> notes;
        int voiced = 0, considered = 0;

        for (int f = 0; f < frames; ++f)
        {
            const int start = f * SampleData::pitchHop / factor - W;
            if (start < 0 || start + 2 * W >= (int) low.size()) continue;
            const float* x = low.data() + start;

            float rms = 0.0f;
            for (int i = 0; i < 2 * W; ++i) rms += x[i] * x[i];
            rms = std::sqrt (rms / (2.0f * W));
            if (rms < gate) continue;
            ++considered;

            // r(tau) = sum_j x[j] x[j+tau] via cross-correlation of the first W samples with the 2W window
            std::fill (a.begin(), a.end(), 0.0f);
            std::fill (b.begin(), b.end(), 0.0f);
            for (int i = 0; i < 2 * W; ++i) a[(size_t) i] = x[i];
            for (int i = 0; i < W; ++i) b[(size_t) i] = x[i];
            fft.performRealOnlyForwardTransform (a.data());
            fft.performRealOnlyForwardTransform (b.data());
            for (int k = 0; k <= N / 2; ++k)
            {
                const std::complex<float> A (a[(size_t) k * 2], a[(size_t) k * 2 + 1]), B (b[(size_t) k * 2], b[(size_t) k * 2 + 1]);
                const auto c = A * std::conj (B);
                a[(size_t) k * 2] = c.real();
                a[(size_t) k * 2 + 1] = c.imag();
            }
            fft.performRealOnlyInverseTransform (a.data());

            // Energies of the shifted windows
            double e0 = 0.0;
            for (int i = 0; i < W; ++i) e0 += (double) x[i] * x[i];
            double et = e0;
            d[0] = 1.0f;
            double running = 0.0;
            int best = -1;
            float bestVal = 1.0f;
            for (int tau = 1; tau < maxTau; ++tau)
            {
                et += (double) x[tau + W - 1] * x[tau + W - 1] - (double) x[tau - 1] * x[tau - 1];
                const float diff = (float) (e0 + et - 2.0 * a[(size_t) tau]);
                running += diff;
                d[(size_t) tau] = running > 0.0 ? diff * (float) tau / (float) running : 1.0f;
            }
            for (int tau = minTau; tau < maxTau - 1; ++tau)
                if (d[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < maxTau - 1 && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                    best = tau;
                    bestVal = d[(size_t) tau];
                    break;
                }
            if (best < 0)
            {
                for (int tau = minTau; tau < maxTau - 1; ++tau)
                    if (d[(size_t) tau] < bestVal) { bestVal = d[(size_t) tau]; best = tau; }
                if (bestVal > 0.3f) continue;
            }

            const float y0 = d[(size_t) best - 1], y1 = d[(size_t) best], y2 = d[(size_t) best + 1];
            const float denom = y0 - 2.0f * y1 + y2;
            const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (y0 - y2) / denom : 0.0f;
            const float hz = (float) sr / ((float) best + std::clamp (shift, -1.0f, 1.0f));
            const float note = detail::hzToMidi (hz);
            s.pitchCurve[(size_t) f] = note;
            notes.push_back (note);
            weights.push_back (rms * (1.0f - bestVal));
            ++voiced;
        }

        // Median filter (5) against octave errors, only where voiced
        auto& pc = s.pitchCurve;
        std::vector<float> filtered (pc);
        for (int i = 2; i + 2 < (int) pc.size(); ++i)
        {
            if (pc[(size_t) i] <= 0.0f) continue;
            std::array<float, 5> w {};
            int m = 0;
            for (int j = -2; j <= 2; ++j)
                if (pc[(size_t) (i + j)] > 0.0f) w[(size_t) m++] = pc[(size_t) (i + j)];
            std::sort (w.begin(), w.begin() + m);
            filtered[(size_t) i] = w[(size_t) m / 2];
        }
        pc = filtered;

        s.pitchConfidence = considered > 0 ? (float) voiced / (float) considered : 0.0f;
        if (! notes.empty() && s.pitchConfidence > 0.3f)
        {
            // Weighted median of the voiced notes
            std::vector<size_t> order (notes.size());
            std::iota (order.begin(), order.end(), 0);
            std::sort (order.begin(), order.end(), [&] (size_t i, size_t j) { return notes[i] < notes[j]; });
            float total = 0.0f;
            for (float w : weights) total += w;
            float acc = 0.0f;
            for (auto i : order)
            {
                acc += weights[i];
                if (acc >= total * 0.5f) { s.rootNote = notes[i]; break; }
            }
        }
        else
            s.rootNote = 60.0f;
    }

    // STFT frames for the spectral engine (Hann, 75 % overlap) with the true frequency of every bin.
    inline void analyseSpectrum (SampleData& s, const std::vector<float>& mono)
    {
        constexpr int n = SampleData::fftSize, hop = SampleData::specHop, bins = SampleData::numBins;
        const int maxFrames = (int) (90.0 * s.sampleRate / hop);   // the first 90 s are enough for textures
        const int frames = std::min (maxFrames, std::max (1, (int) mono.size() / hop + 1));
        juce::dsp::FFT fft (SampleData::fftOrder);
        std::vector<float> win ((size_t) n), buf ((size_t) n * 2), prevPhase ((size_t) bins, 0.0f);
        detail::hann (win);
        float winSum = 0.0f;
        for (float w : win) winSum += w;
        const float norm = 2.0f / winSum;

        s.numSpecFrames = frames;
        s.specMag.assign ((size_t) frames * bins, 0.0f);
        s.specFreq.assign ((size_t) frames * bins, 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            const int centre = f * hop;
            for (int i = 0; i < n; ++i)
            {
                const int idx = centre + i - n / 2;
                buf[(size_t) i] = (idx >= 0 && idx < (int) mono.size() ? mono[(size_t) idx] : 0.0f) * win[(size_t) i];
            }
            std::fill (buf.begin() + n, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data());
            auto* mag = s.specMag.data() + (size_t) f * bins;
            auto* freq = s.specFreq.data() + (size_t) f * bins;
            for (int k = 0; k < bins; ++k)
            {
                const float re = buf[(size_t) k * 2], im = buf[(size_t) k * 2 + 1];
                mag[k] = std::sqrt (re * re + im * im) * norm;
                const float phase = std::atan2 (im, re);
                const float expected = dsp::kTwoPi * (float) k * (float) hop / (float) n;
                float dphi = phase - prevPhase[(size_t) k] - expected;
                dphi -= dsp::kTwoPi * std::round (dphi / dsp::kTwoPi);
                prevPhase[(size_t) k] = phase;
                freq[k] = f == 0 ? (float) k : (float) k + dphi * (float) n / (dsp::kTwoPi * (float) hop);
            }
        }
    }

    inline void buildOverview (SampleData& s, const std::vector<float>& brightness)
    {
        const int n = SampleData::overviewSize;
        for (int b = 0; b < n; ++b)
        {
            const int from = (int) ((int64_t) b * s.numFrames / n);
            const int to = std::max (from + 1, (int) ((int64_t) (b + 1) * s.numFrames / n));
            float lo = 0.0f, hi = 0.0f;
            for (int i = from; i < to && i < s.numFrames; ++i)
            {
                const float l = s.left[(size_t) i];
                const float r = s.right.empty() ? l : s.right[(size_t) i];
                lo = std::min (lo, std::min (l, r));
                hi = std::max (hi, std::max (l, r));
            }
            s.overviewMin[(size_t) b] = lo;
            s.overviewMax[(size_t) b] = hi;
            const int nf = (int) brightness.size();
            s.overviewBright[(size_t) b] = nf > 0 ? brightness[(size_t) std::clamp ((from + to) / 2 / SampleData::noveltyHop, 0, nf - 1)] : 0.5f;
        }
    }

    // Full analysis of a freshly loaded sound.
    inline void analyse (SampleData& s)
    {
        s.numFrames = (int) s.left.size();
        s.peak = 0.0f;
        double sq = 0.0;
        for (int i = 0; i < s.numFrames; ++i)
        {
            const float l = s.left[(size_t) i], r = s.right.empty() ? l : s.right[(size_t) i];
            s.peak = std::max (s.peak, std::max (std::abs (l), std::abs (r)));
            sq += 0.5 * ((double) l * l + (double) r * r);
        }
        s.rms = s.numFrames > 0 ? (float) std::sqrt (sq / s.numFrames) : 0.0f;

        const auto mono = detail::monoSum (s);
        std::vector<float> brightness;
        buildMips (s);
        analyseNovelty (s, mono, brightness);
        analysePitch (s, mono);
        analyseSpectrum (s, mono);
        buildOverview (s, brightness);
    }

    // ---------------------------------------------------------------------------------------------
    // Slicing (fast, runs on the message thread whenever the slice settings change)

    struct Slice
    {
        int start = 0, end = 0;   // frames
        float pitch = 0.0f;       // median MIDI pitch, 0 when unpitched
    };

    inline int firstSound (const SampleData& s, float gate)
    {
        for (size_t f = 0; f < s.energy.size(); ++f)
            if (s.energy[f] > gate) return (int) f * SampleData::noveltyHop;
        return 0;
    }

    inline std::vector<int> pickTransients (const SampleData& s, float sensitivity)
    {
        std::vector<int> out;
        const auto& nov = s.novelty;
        const int n = (int) nov.size();
        const float gate = std::max (1.0e-4f, s.peak * 0.01f);
        const float delta = 0.03f + (1.0f - sensitivity) * 0.35f;
        const int minGap = std::max (1, (int) (0.07 * s.sampleRate / SampleData::noveltyHop));
        int last = -minGap;
        for (int f = 1; f + 1 < n; ++f)
        {
            if (nov[(size_t) f] < nov[(size_t) f - 1] || nov[(size_t) f] < nov[(size_t) f + 1]) continue;
            float mean = 0.0f;
            int cnt = 0;
            for (int j = std::max (0, f - 20); j < std::min (n, f + 20); ++j) { mean += nov[(size_t) j]; ++cnt; }
            mean /= (float) std::max (1, cnt);
            if (nov[(size_t) f] > mean + delta && f - last >= minGap && s.energy[(size_t) std::min (n - 1, f + 2)] > gate)
            {
                out.push_back (std::max (0, f * SampleData::noveltyHop - SampleData::noveltyHop));
                last = f;
            }
        }
        return out;
    }

    // Syllables: phrases of sound separated by dips in the loudness curve.
    inline std::vector<int> pickSyllables (const SampleData& s, float sensitivity)
    {
        std::vector<int> out;
        const int n = (int) s.energy.size();
        if (n < 3) return out;
        std::vector<float> db ((size_t) n);
        for (int i = 0; i < n; ++i)
            db[(size_t) i] = juce::Decibels::gainToDecibels (s.energy[(size_t) i], -90.0f);
        // Smooth over ~25 ms
        const int r = std::max (1, (int) (0.012 * s.sampleRate / SampleData::noveltyHop));
        std::vector<float> sm ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            int c = 0;
            for (int j = std::max (0, i - r); j <= std::min (n - 1, i + r); ++j) { acc += db[(size_t) j]; ++c; }
            sm[(size_t) i] = acc / (float) c;
        }
        const float top = *std::max_element (sm.begin(), sm.end());
        const float floorDb = top - 38.0f;
        const float dip = 9.0f - 6.0f * sensitivity;
        const int minGap = std::max (1, (int) (0.09 * s.sampleRate / SampleData::noveltyHop));

        bool inSound = false;
        float peakSince = -90.0f, valley = 0.0f;
        int valleyAt = 0, last = -minGap;
        for (int i = 0; i < n; ++i)
        {
            const float v = sm[(size_t) i];
            if (! inSound)
            {
                if (v > floorDb + 6.0f && i - last >= minGap)
                {
                    out.push_back (std::max (0, (i - r) * SampleData::noveltyHop));
                    last = i;
                    inSound = true;
                    peakSince = v;
                    valley = v;
                    valleyAt = i;
                }
                continue;
            }
            if (v < floorDb) { inSound = false; continue; }
            if (v > peakSince) { peakSince = v; valley = v; valleyAt = i; }
            else if (v < valley) { valley = v; valleyAt = i; }
            // Rising again after a clear dip: a new syllable starts at the valley
            if (peakSince - valley > dip && v - valley > dip * 0.6f && valleyAt - last >= minGap)
            {
                out.push_back (valleyAt * SampleData::noveltyHop);
                last = valleyAt;
                peakSince = v;
                valley = v;
            }
        }
        return out;
    }

    inline std::vector<Slice> makeSlices (const SampleData& s, int mode, float sensitivity, int gridCount,
                                          const std::vector<float>& manualPoints)
    {
        std::vector<int> starts;
        const float gate = std::max (1.0e-4f, s.peak * 0.01f);
        const int begin = firstSound (s, gate);
        switch (mode)
        {
            case 0: starts = pickTransients (s, sensitivity); break;
            case 1: starts = pickSyllables (s, sensitivity); break;
            case 2:
            {
                const int len = s.numFrames - begin;
                for (int i = 0; i < gridCount; ++i)
                    starts.push_back (begin + (int) ((int64_t) len * i / gridCount));
                break;
            }
            default:
                for (float p : manualPoints)
                    starts.push_back ((int) (std::clamp (p, 0.0f, 1.0f) * (float) s.numFrames));
                break;
        }
        if (starts.empty() || starts.front() > begin + (int) (0.05 * s.sampleRate))
            starts.insert (starts.begin(), mode == 3 ? 0 : begin);
        std::sort (starts.begin(), starts.end());
        starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
        if (starts.size() > 128) starts.resize (128);

        std::vector<Slice> slices;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            Slice sl;
            sl.start = starts[i];
            sl.end = i + 1 < starts.size() ? starts[i + 1] : s.numFrames;
            if (sl.end - sl.start < 64) continue;
            // Median pitch of the slice
            std::vector<float> p;
            for (int f = sl.start / SampleData::pitchHop; f < sl.end / SampleData::pitchHop && f < (int) s.pitchCurve.size(); ++f)
                if (s.pitchCurve[(size_t) f] > 0.0f) p.push_back (s.pitchCurve[(size_t) f]);
            if (p.size() > 3)
            {
                std::nth_element (p.begin(), p.begin() + (long) p.size() / 2, p.end());
                sl.pitch = p[p.size() / 2];
            }
            slices.push_back (sl);
        }
        return slices;
    }
}
