#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include "../dsp/DspCommon.h"
#include "../Parameters.h"

namespace huldra::fx
{
    using dsp::Smoother;
    using dsp::softClip;
    using dsp::kPi;
    using dsp::kTwoPi;
    using params::Values;
    namespace P = params;

    struct StereoBuffer
    {
        float* l;
        float* r;
        int n;
    };

    // ---------------------------------------------------------------------------------------------
    // Helpers (delay line and biquad from Fjord)

    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            int size = 1;
            while (size < maxSamples + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }

        float read (float delay) const
        {
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        int maxDelay() const { return mask - 4; }

    private:
        float at (int i) const { return buffer[(size_t) (i & mask)]; }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};

        float process (float x, int ch)
        {
            const float y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
            z2[(size_t) ch] = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1.fill (0.0f); z2.fill (0.0f); }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        void highpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void lowpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void bandpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (a, 0, -a, 1 + a, -2 * c, 1 - a);
        }

        void notch (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (1, -2 * c, 1, 1 + a, -2 * c, 1 - a);
        }

        void peak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
        }

        void shelf (double sr, double f, double gainDb, bool high)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w);
            const double a = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * a;
            if (high)
                set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
            else
                set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                     (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
        }

        // Magnitude response (for graphs)
        double magnitude (double freq, double sr) const
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / sr);
            const auto num = (double) b0 + (double) b1 * z + (double) b2 * z * z;
            const auto den = 1.0 + (double) a1 * z + (double) a2 * z * z;
            return std::abs (num / den);
        }
    };

    inline void equalPowerMix (float mix, float& dryGain, float& wetGain)
    {
        dryGain = std::cos (mix * kPi * 0.5f);
        wetGain = std::sin (mix * kPi * 0.5f);
    }

    inline float onePoleCoef (float hz, float sr) { return 1.0f - std::exp (-kTwoPi * std::min (hz, sr * 0.45f) / sr); }

    // ---------------------------------------------------------------------------------------------
    // Saturation: six transfer curves, bias, tilt tone, 1x / 2x / 4x oversampling

    class Saturation
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sampleRate = (float) sr;
            for (auto& o : oversamplers) o->initProcessing ((size_t) maxBlock);
            dry.setSize (2, maxBlock);
            mix.setTime (0.02f, sampleRate);
            reset();
        }

        void reset() { for (auto& o : oversamplers) o->reset(); tilt.fill (0.0f); lastGain = -1.0f; }

        // The transfer curve, shared with the UI graph.
        static float shape (int mode, float x, float bias)
        {
            const float b = bias * 0.5f;
            switch (mode)
            {
                case 0:  return std::tanh (x + b) - std::tanh (b);                                             // Tape
                case 1:  { const float y = x + b;                                                              // Tube: asymmetric
                           return (y >= 0.0f ? 1.0f - std::exp (-y) : -(1.0f - std::exp (1.4f * y)) / 1.4f)
                                  - (b >= 0.0f ? 1.0f - std::exp (-b) : -(1.0f - std::exp (1.4f * b)) / 1.4f); }
                case 2:  { const float y = x + b;                                                              // Diode: soft one side, hard the other
                           return (y > 0.0f ? std::tanh (y * 1.4f) / 1.4f * 1.2f : -std::min (0.55f, -y * 0.9f)) ; }
                case 3:  return std::sin ((x + b) * 1.5f) - std::sin (b * 1.5f);                               // Fold
                case 4:  return std::clamp (x + b, -1.0f, 1.0f) - std::clamp (b, -1.0f, 1.0f);                 // Hard
                default: { const float y = std::tanh (x);                                                      // Rectify (octave up)
                           return y * (0.5f - b * 0.5f) + std::abs (y) * (0.5f + b * 0.5f) - (0.25f + 0.25f * b); }
            }
        }

        static float driveGain (float drive) { return std::pow (10.0f, drive * 36.0f / 20.0f); }

        void process (StereoBuffer b, const Values& v)
        {
            dry.copyFrom (0, 0, b.l, b.n);
            dry.copyFrom (1, 0, b.r, b.n);

            const int mode = v.i (P::SatMode);
            const float bias = v[P::SatBias];
            const float gain = driveGain (v[P::SatDrive]);
            const float comp = 1.0f / (1.0f + v[P::SatDrive] * 2.2f);
            if (lastGain < 0.0f) lastGain = gain;

            const int os = std::clamp (v.i (P::SatOs), 0, 2);
            float* chans[] = { b.l, b.r };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) b.n);
            auto run = [&] (juce::dsp::AudioBlock<float> blk)
            {
                const auto n = blk.getNumSamples();
                const float step = (gain - lastGain) / (float) n;
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto* d = blk.getChannelPointer (ch);
                    float g = lastGain;
                    for (size_t i = 0; i < n; ++i)
                    {
                        g += step;
                        d[i] = shape (mode, d[i] * g, bias) * comp;
                    }
                }
            };
            if (os == 0)
                run (block);
            else
            {
                auto& o = *oversamplers[(size_t) os - 1];
                run (o.processSamplesUp (block));
                o.processSamplesDown (block);
            }
            lastGain = gain;

            // Tilt tone around 900 Hz, output level, dry/wet
            const float t = v[P::SatTone];
            const float lowG = 1.0f - std::max (0.0f, t) * 0.6f, highG = 1.0f + t * (t > 0.0f ? 1.0f : 0.8f);
            const float c = onePoleCoef (900.0f, sampleRate);
            const float out = juce::Decibels::decibelsToGain (v[P::SatOut]);
            float inPeak = 0.0f, outPeak = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                tilt[0] += (b.l[i] - tilt[0]) * c;
                tilt[1] += (b.r[i] - tilt[1]) * c;
                const float wl = (tilt[0] * lowG + (b.l[i] - tilt[0]) * highG) * out;
                const float wr = (tilt[1] * lowG + (b.r[i] - tilt[1]) * highG) * out;
                const float m = mix.process (v[P::SatMix]);
                const float dl = dry.getSample (0, i), dr = dry.getSample (1, i);
                inPeak = std::max (inPeak, std::max (std::abs (dl), std::abs (dr)));
                b.l[i] = dl * (1.0f - m) + wl * m;
                b.r[i] = dr * (1.0f - m) + wr * m;
                outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
            }
            lastIn = inPeak;
            lastOut = outPeak;
        }

        float lastIn = 0.0f, lastOut = 0.0f;

    private:
        float sampleRate = 44100.0f, lastGain = -1.0f;
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers {
            std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true),
            std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true) };
        juce::AudioBuffer<float> dry;
        std::array<float, 2> tilt {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Compressor: stereo linked, soft knee, parallel mix, sidechain high-pass (from Fjord)

    class Compressor
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; mix.setTime (0.02f, sampleRate); reset(); }
        void reset() { envDb = 0.0f; hpX = { 0.0f, 0.0f }; hpY = { 0.0f, 0.0f }; }

        static float reduction (float levelDb, float threshold, float ratio, float knee)
        {
            const float over = levelDb - threshold;
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            if (knee < 0.01f || over >= knee * 0.5f) return std::max (0.0f, over) * slope;
            if (over <= -knee * 0.5f) return 0.0f;
            const float k = over + knee * 0.5f;
            return slope * k * k / (2.0f * knee);
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float att = std::exp (-1.0f / (std::max (0.0001f, v[P::CompAttack] * 0.001f) * sampleRate));
            const float rel = std::exp (-1.0f / (std::max (0.005f, v[P::CompRelease] * 0.001f) * sampleRate));
            const float makeup = juce::Decibels::decibelsToGain (v[P::CompMakeup]);
            const float hpfHz = v[P::CompHpf];
            const bool hpf = hpfHz > 20.5f;
            const float hpA = 1.0f / (1.0f + kTwoPi * hpfHz / sampleRate);
            const float threshold = v[P::CompThresh], ratio = v[P::CompRatio], knee = v[P::CompKnee];
            float maxRed = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

            for (int i = 0; i < b.n; ++i)
            {
                float dl = b.l[i], dr = b.r[i];
                inPeak = std::max (inPeak, std::max (std::abs (dl), std::abs (dr)));
                if (hpf)
                {
                    hpY[0] = hpA * (hpY[0] + dl - hpX[0]); hpX[0] = dl; dl = hpY[0];
                    hpY[1] = hpA * (hpY[1] + dr - hpX[1]); hpX[1] = dr; dr = hpY[1];
                }
                const float level = juce::Decibels::gainToDecibels (std::max (std::abs (dl), std::abs (dr)), -120.0f);
                const float red = reduction (level, threshold, ratio, knee);
                envDb = red > envDb ? red + (envDb - red) * att : red + (envDb - red) * rel;
                maxRed = std::max (maxRed, envDb);
                const float g = juce::Decibels::decibelsToGain (-envDb) * makeup;
                const float m = mix.process (v[P::CompMix]);
                b.l[i] = b.l[i] * (1.0f - m) + b.l[i] * g * m;
                b.r[i] = b.r[i] * (1.0f - m) + b.r[i] * g * m;
                outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
            }
            lastReduction = maxRed;
            lastIn = inPeak;
            lastOut = outPeak;
        }

        float lastReduction = 0.0f, lastIn = 0.0f, lastOut = 0.0f;

    private:
        float sampleRate = 44100.0f, envDb = 0.0f;
        std::array<float, 2> hpX {}, hpY {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // EQ: low cut (24 dB), low shelf, two peaks, high shelf, high cut (24 dB). The design is shared
    // with the EQ graph so it shows exactly what the DSP does.

    struct EqDesign
    {
        static constexpr int kNumFilters = 8;
        std::array<Biquad, kNumFilters> filters;
        std::array<bool, kNumFilters> use {};
        std::array<int, kNumFilters> band {};   // 0 low cut, 1 low, 2 mid, 3 mid 2, 4 high, 5 high cut

        void design (double sr, const Values& v)
        {
            const double safe = sr * 0.45;
            constexpr double q24a = 0.5412, q24b = 1.3066;
            auto f = [safe] (float hz) { return std::min ((double) hz, safe); };
            filters[0].highpass (sr, f (v[P::EqLowCut]), q24a);
            filters[1].highpass (sr, f (v[P::EqLowCut]), q24b);
            filters[2].shelf (sr, f (v[P::EqLowFreq]), v[P::EqLowGain], false);
            filters[3].peak (sr, f (v[P::EqMidFreq]), v[P::EqMidQ], v[P::EqMidGain]);
            filters[4].peak (sr, f (v[P::EqMid2Freq]), v[P::EqMid2Q], v[P::EqMid2Gain]);
            filters[5].shelf (sr, f (v[P::EqHighFreq]), v[P::EqHighGain], true);
            filters[6].lowpass (sr, f (v[P::EqHighCut]), q24a);
            filters[7].lowpass (sr, f (v[P::EqHighCut]), q24b);
            const bool lowCut = v[P::EqLowCut] > 20.5f, highCut = v[P::EqHighCut] < 19900.0f;
            use = { lowCut, lowCut, std::abs (v[P::EqLowGain]) > 0.01f, std::abs (v[P::EqMidGain]) > 0.01f,
                    std::abs (v[P::EqMid2Gain]) > 0.01f, std::abs (v[P::EqHighGain]) > 0.01f, highCut, highCut };
            band = { 0, 0, 1, 2, 3, 4, 5, 5 };
        }

        double magnitudeDb (double freq, double sr, int onlyBand = -1) const
        {
            double db = 0.0;
            for (size_t k = 0; k < filters.size(); ++k)
                if (use[k] && (onlyBand < 0 || band[k] == onlyBand))
                    db += 20.0 * std::log10 (std::max (1.0e-9, filters[k].magnitude (freq, sr)));
            return db;
        }
    };

    class Eq
    {
    public:
        void prepare (double sr) { sampleRate = sr; reset(); lastKey = -1.0f; }
        void reset() { for (auto& f : eq.filters) f.reset(); }

        void process (StereoBuffer b, const Values& v)
        {
            float key = 0.0f;
            for (auto id : { P::EqLowCut, P::EqLowFreq, P::EqLowGain, P::EqMidFreq, P::EqMidGain, P::EqMidQ,
                             P::EqMid2Freq, P::EqMid2Gain, P::EqMid2Q, P::EqHighFreq, P::EqHighGain, P::EqHighCut })
                key = key * 1.0001f + v[id] * (float) (1 + (int) id);
            if (! juce::exactlyEqual (key, lastKey))
            {
                lastKey = key;
                eq.design (sampleRate, v);
            }
            for (int i = 0; i < b.n; ++i)
            {
                float l = b.l[i], r = b.r[i];
                for (size_t k = 0; k < eq.filters.size(); ++k)
                    if (eq.use[k]) { l = eq.filters[k].process (l, 0); r = eq.filters[k].process (r, 1); }
                b.l[i] = l;
                b.r[i] = r;
            }
        }

    private:
        double sampleRate = 44100.0;
        EqDesign eq;
        float lastKey = -1.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Pitch shifter: two crossfading read heads sweeping through a short delay (used for shimmer and grains)

    class PitchShifter
    {
    public:
        void prepare (double sr, float windowMs = 60.0f)
        {
            line.prepare ((int) (sr * 0.2));
            window = std::max (64.0f, windowMs * 0.001f * (float) sr);
            phase = 0.0f;
        }

        void clear() { line.clear(); phase = 0.0f; }

        float process (float x, float ratio)
        {
            line.push (x);
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);
            const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
            const float a = line.read (2.0f + phase * window), b = line.read (2.0f + p2 * window);
            const float wa = std::sin (kPi * phase), wb = std::sin (kPi * p2);
            return a * wa * wa + b * wb * wb;
        }

    private:
        DelayLine line;
        float window = 2048.0f, phase = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Reverb: 8-line FDN (Hadamard) with room / plate / hall voicings, a shimmer mode (octave-up in the
    // feedback) and a "cloud" mode (long, heavily modulated, diffuse). Input low cut and ducking.

    class Reverb
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            const float scale = sampleRate / 44100.0f;
            for (size_t i = 0; i < 8; ++i)
                lines[i].prepare ((int) (baseLengths[i] * 3.6f * scale) + 64);
            for (size_t c = 0; c < 2; ++c)
                for (size_t k = 0; k < 4; ++k)
                    diffusers[c][k].prepare ((int) (diffLengths[c][k] * 1.4f * scale) + 4);
            predelay.prepare ((int) (0.3 * sr));
            for (auto& p : shifters) p.prepare (sr, 80.0f);
            mix.setTime (0.03f, sampleRate);
            size.setTime (0.25f, sampleRate);
            size.reset (-1.0f);
            reset();
        }

        void reset()
        {
            for (auto& l : lines) l.clear();
            for (auto& c : diffusers) for (auto& d : c) d.clear();
            for (auto& p : shifters) p.clear();
            predelay.clear();
            damp.fill (0.0f);
            hp = { 0.0f, 0.0f };
            duckEnv = 0.0f;
            shimmerLp = { 0.0f, 0.0f };
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float scale = sampleRate / 44100.0f;
            const int mode = v.i (P::VerbMode);
            const float modeLen = mode == 0 ? 0.55f : mode == 1 ? 0.85f : mode == 4 ? 1.7f : 1.35f;
            const float modeDamp = mode == 1 ? 0.5f : 1.0f;            // plates are brighter
            const float diffusion = mode == 1 ? 0.72f : mode == 4 ? 0.78f : 0.62f;
            const float modDepth = mode == 4 ? 28.0f : (mode == 3 ? 9.0f : 5.0f);
            const float shimmer = mode == 3 ? v[P::VerbShimmer] : (mode == 4 ? v[P::VerbShimmer] * 0.25f : 0.0f);
            if (size.value < 0.0f) size.reset (v[P::VerbSize]);
            const float dampC = onePoleCoef (16000.0f * std::pow (0.08f, v[P::VerbDamp] * modeDamp), sampleRate);
            const float preSamples = std::max (1.0f, v[P::VerbPre] * sampleRate);
            const float t60 = std::max (0.1f, v[P::VerbDecay]);
            const float hpC = onePoleCoef (v[P::VerbLowCut], sampleRate);
            const float duckAmt = v[P::VerbDuck], width = v[P::VerbWidth];
            const float duckAtt = 1.0f - std::exp (-1.0f / (0.002f * sampleRate));
            const float duckRel = 1.0f - std::exp (-1.0f / (0.25f * sampleRate));
            const float shimLp = onePoleCoef (6000.0f, sampleRate);

            std::array<float, 8> decayGain {};
            for (size_t k = 0; k < 8; ++k)
                decayGain[k] = std::pow (10.0f, -3.0f * baseLengths[k] * (0.3f + 2.0f * size.value) * modeLen * scale / (t60 * sampleRate));

            for (int i = 0; i < b.n; ++i)
            {
                const float sz = size.process (v[P::VerbSize]);
                const float lenScale = (0.3f + 2.0f * sz) * modeLen * scale;

                float in = (b.l[i] + b.r[i]) * 0.5f;
                hp[0] += (in - hp[0]) * hpC;
                in -= hp[0];
                predelay.push (in);
                const float pre = predelay.read (preSamples);
                float x[2] = { pre, pre };
                for (size_t c = 0; c < 2; ++c)
                    for (size_t k = 0; k < 4; ++k)
                        x[c] = diffusers[c][k].process (x[c], diffusion);

                std::array<float, 8> y {};
                for (size_t k = 0; k < 8; ++k)
                {
                    const float len = baseLengths[k] * lenScale;
                    const float mod = modDepth * scale * std::sin (modPhase[k]);
                    modPhase[k] += modRates[k] * (mode == 4 ? 2.2f : 1.0f) * kTwoPi / sampleRate;
                    if (modPhase[k] > kTwoPi) modPhase[k] -= kTwoPi;
                    const float out = lines[k].read (std::max (4.0f, len + mod));
                    damp[k] += (out - damp[k]) * dampC;
                    y[k] = damp[k] * decayGain[k];
                }

                // Shimmer: an octave up fed back into two of the lines
                float shim[2] = { 0.0f, 0.0f };
                if (shimmer > 0.001f)
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float up = shifters[c].process (y[c * 4], 2.0f);
                        shimmerLp[c] += (up - shimmerLp[c]) * shimLp;
                        shim[c] = shimmerLp[c] * shimmer * 0.55f;
                    }

                std::array<float, 8> h = y;
                for (int len = 1; len < 8; len <<= 1)
                    for (int s = 0; s < 8; s += len * 2)
                        for (int j = s; j < s + len; ++j)
                        {
                            const float a = h[(size_t) j], c = h[(size_t) (j + len)];
                            h[(size_t) j] = a + c;
                            h[(size_t) (j + len)] = a - c;
                        }

                constexpr float norm = 0.35355339f;
                for (size_t k = 0; k < 8; ++k)
                    lines[k].push (softClip (h[k] * norm + x[k % 2] * 0.5f + (k == 1 ? shim[0] : (k == 6 ? shim[1] : 0.0f))));

                const float outL = (y[0] + y[2] + y[4] + y[6] - y[1] * 0.5f) * 0.45f;
                const float outR = (y[1] + y[3] + y[5] + y[7] - y[0] * 0.5f) * 0.45f;
                const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * width;

                const float level = std::max (std::abs (b.l[i]), std::abs (b.r[i]));
                duckEnv += (level - duckEnv) * (level > duckEnv ? duckAtt : duckRel);
                const float duck = 1.0f - duckAmt * std::min (1.0f, duckEnv * 3.0f);

                float dg, wg;
                equalPowerMix (mix.process (v[P::VerbMix]), dg, wg);
                b.l[i] = b.l[i] * dg + (mid + side) * wg * duck;
                b.r[i] = b.r[i] * dg + (mid - side) * wg * duck;
                tailLevel = tailLevel * 0.9995f + std::abs (mid) * 0.0005f;
            }
        }

        float tailLevel = 0.0f;   // for the visual

    private:
        struct Allpass
        {
            DelayLine line;
            int length = 1;
            void prepare (int len) { line.prepare (len + 4); length = len; }
            void clear() { line.clear(); }
            float process (float x, float g)
            {
                const float d = line.read ((float) length);
                const float v = x + g * d;
                line.push (v);
                return d - g * v;
            }
        };

        static constexpr float baseLengths[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr float diffLengths[2][4] = { { 142, 107, 379, 277 }, { 151, 113, 367, 283 } };
        static constexpr float modRates[8] = { 0.11f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f };

        float sampleRate = 44100.0f, duckEnv = 0.0f;
        std::array<DelayLine, 8> lines;
        std::array<std::array<Allpass, 4>, 2> diffusers;
        std::array<PitchShifter, 2> shifters;
        DelayLine predelay;
        std::array<float, 8> damp {}, modPhase { 0.0f, 0.8f, 1.6f, 2.4f, 3.2f, 4.0f, 4.8f, 5.6f };
        std::array<float, 2> hp {}, shimmerLp {};
        Smoother mix, size;
    };

    // ---------------------------------------------------------------------------------------------
    // Delay: clean, tape (wow, saturation, darker repeats) or grains (repeats scattered an octave up / down)

    inline double delayBeats (int index)
    {
        static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0 };
        return beats[std::clamp (index, 0, 10)];
    }

    class Delay
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (4.1 * sr));
            for (auto& p : shifters) p.prepare (sr, 90.0f);
            mix.setTime (0.03f, sampleRate);
            time.setTime (0.08f, sampleRate);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            for (auto& p : shifters) p.clear();
            lp.fill (0.0f);
            hp.fill (0.0f);
            duckEnv = 0.0f;
            time.reset (-1.0f);
            wow = 0.0f;
            grainFlip = 0;
        }

        void process (StereoBuffer b, const Values& v, double bpm)
        {
            const float seconds = v.b (P::DlySync) ? (float) (delayBeats (v.i (P::DlyBeats)) * 60.0 / std::max (20.0, bpm))
                                                   : v[P::DlyTime] * 0.001f;
            const float target = std::clamp (seconds * sampleRate, 16.0f, (float) lines[0].maxDelay() - 64.0f);
            if (time.value < 0.0f) time.reset (target);

            const int mode = v.i (P::DlyMode);
            const float lpc = onePoleCoef (18000.0f * std::pow (0.07f, std::min (1.0f, v[P::DlyDamp] + (mode == 1 ? 0.25f : 0.0f))), sampleRate);
            const float hpc = onePoleCoef (v[P::DlyLowCut], sampleRate);
            const float duckAtt = 1.0f - std::exp (-1.0f / (0.005f * sampleRate));
            const float duckRel = 1.0f - std::exp (-1.0f / (0.3f * sampleRate));
            const float fb = v[P::DlyFeedback], duckAmt = v[P::DlyDuck];
            const bool pingPong = v.b (P::DlyPingPong);

            for (int i = 0; i < b.n; ++i)
            {
                float d = time.process (target);
                if (mode == 1)
                {
                    wow += 0.6f / sampleRate;
                    if (wow > 1.0f) wow -= 1.0f;
                    d += (0.0018f * std::sin (kTwoPi * wow) + 0.0004f * std::sin (kTwoPi * wow * 7.3f)) * sampleRate;
                }
                float wl = lines[0].read (d), wr = lines[1].read (d);
                if (mode == 2)
                {
                    // grains: alternate the repeats between an octave up and a fifth down
                    if (++grainCounter > (int) (sampleRate * 0.09f)) { grainCounter = 0; grainFlip = (grainFlip + 1) % 3; }
                    const float ratio = grainFlip == 0 ? 2.0f : (grainFlip == 1 ? 1.0f : 0.667f);
                    wl = shifters[0].process (wl, ratio);
                    wr = shifters[1].process (wr, ratio);
                }

                auto colour = [&] (float x, int ch)
                {
                    lp[(size_t) ch] += (x - lp[(size_t) ch]) * lpc;
                    hp[(size_t) ch] += (lp[(size_t) ch] - hp[(size_t) ch]) * hpc;
                    const float y = lp[(size_t) ch] - hp[(size_t) ch];
                    return mode == 1 ? softClip (y * 1.4f) / 1.2f : softClip (y);
                };
                const float cl = colour (wl, 0), cr = colour (wr, 1);

                const float inL = b.l[i], inR = b.r[i];
                if (pingPong)
                {
                    lines[0].push ((inL + inR) * 0.5f + cr * fb);
                    lines[1].push (cl * fb);
                }
                else
                {
                    lines[0].push (inL + cl * fb);
                    lines[1].push (inR + cr * fb);
                }

                const float level = std::max (std::abs (inL), std::abs (inR));
                duckEnv += (level - duckEnv) * (level > duckEnv ? duckAtt : duckRel);
                const float duck = 1.0f - duckAmt * std::min (1.0f, duckEnv * 4.0f);

                float dg, wg;
                equalPowerMix (mix.process (v[P::DlyMix]), dg, wg);
                dg = std::max (dg, 0.5f);
                b.l[i] = inL * dg + cl * wg * duck;
                b.r[i] = inR * dg + cr * wg * duck;
            }
        }

    private:
        float sampleRate = 44100.0f, duckEnv = 0.0f, wow = 0.0f;
        int grainCounter = 0, grainFlip = 0;
        std::array<DelayLine, 2> lines;
        std::array<PitchShifter, 2> shifters;
        std::array<float, 2> lp {}, hp {};
        Smoother mix, time;
    };

    // ---------------------------------------------------------------------------------------------
    // Motion: chorus, ensemble (three voices), phaser, flanger or doubler

    class Motion
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (0.06 * sr));
            mix.setTime (0.02f, sampleRate);
            depth.setTime (0.05f, sampleRate);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            for (auto& s : allpass) s.fill (0.0f);
            last = { 0.0f, 0.0f };
        }

        void process (StereoBuffer b, const Values& v)
        {
            const int mode = v.i (P::ChrMode);
            const float inc = v[P::ChrRate] / sampleRate;
            const float fb = v[P::ChrFeedback];
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                const float dep = depth.process (v[P::ChrDepth]);
                const float m = mix.process (v[P::ChrMix]);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float ph = phase + (ch == 0 ? 0.0f : 0.25f);
                    const float lfo = 0.5f - 0.5f * std::cos (kTwoPi * ph);
                    const float x = io[ch][i];
                    float y;
                    switch (mode)
                    {
                        case 0:     // Chorus
                            lines[(size_t) ch].push (x + last[(size_t) ch] * fb * 0.4f);
                            y = lines[(size_t) ch].read ((0.007f + dep * 0.008f * lfo) * sampleRate);
                            break;
                        case 1:     // Ensemble: three taps, 120 degrees apart
                        {
                            lines[(size_t) ch].push (x);
                            y = 0.0f;
                            for (int t = 0; t < 3; ++t)
                            {
                                const float p = ph + (float) t / 3.0f;
                                const float l2 = 0.5f - 0.5f * std::cos (kTwoPi * p);
                                const float fast = 0.08f * std::sin (kTwoPi * (p * 7.0f));
                                y += lines[(size_t) ch].read ((0.012f + dep * 0.01f * (l2 + fast)) * sampleRate);
                            }
                            y *= 0.55f;
                            break;
                        }
                        case 2:     // Phaser: 6 allpass stages
                        {
                            const float fc = std::min (160.0f * std::exp2 (lfo * dep * 5.0f), sampleRate * 0.4f);
                            const float t = std::tan (kPi * fc / sampleRate);
                            const float coef = (t - 1.0f) / (t + 1.0f);
                            y = x + last[(size_t) ch] * fb * 0.85f;
                            for (auto& s : allpass[(size_t) ch])
                            {
                                const float out = coef * y + s;
                                s = y - coef * out;
                                y = out;
                            }
                            break;
                        }
                        case 3:     // Flanger
                            lines[(size_t) ch].push (softClip (x + last[(size_t) ch] * fb * 0.9f));
                            y = lines[(size_t) ch].read ((0.0004f + dep * 0.0055f * lfo) * sampleRate + 1.0f);
                            break;
                        default:    // Doubler: a second, slightly late and detuned take on each side
                        {
                            lines[(size_t) ch].push (x);
                            const float base = ch == 0 ? 0.019f : 0.027f;
                            y = lines[(size_t) ch].read ((base + dep * 0.0025f * lfo) * sampleRate);
                            break;
                        }
                    }
                    last[(size_t) ch] = softClip (y);
                    io[ch][i] = x * (1.0f - 0.5f * m) + y * (mode == 4 ? 0.8f : 0.5f) * m;
                }
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
            }
        }

        float getPhase() const { return phase; }

    private:
        float sampleRate = 44100.0f, phase = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<std::array<float, 6>, 2> allpass {};
        std::array<float, 2> last {};
        Smoother mix, depth;
    };

    // ---------------------------------------------------------------------------------------------
    // Lo-Fi: tape wow and flutter, bit depth, sample-rate reduction, hiss and age (darker, narrower)

    class Lofi
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (0.05 * sr));
            mix.setTime (0.02f, sampleRate);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            hold = { 0.0f, 0.0f };
            lp = { 0.0f, 0.0f };
            hp = { 0.0f, 0.0f };
            phase = 1.0f;
            rng.seed (91);
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float wowDepth = v[P::LofiWow] * 0.004f * sampleRate, flutterDepth = v[P::LofiFlutter] * 0.0006f * sampleRate;
            const float q = std::pow (2.0f, v[P::LofiBits] - 1.0f);
            const float step = 1.0f / std::max (1.0f, v[P::LofiRate]);
            const float age = v[P::LofiAge];
            const float lpc = onePoleCoef (16000.0f * std::pow (0.12f, age), sampleRate);
            const float hpc = onePoleCoef (20.0f + age * 180.0f, sampleRate);
            const float hiss = v[P::LofiNoise] * 0.02f;
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                wowPhase += 0.55f / sampleRate;
                flutterPhase += 9.0f / sampleRate;
                if (wowPhase > 1.0f) wowPhase -= 1.0f;
                if (flutterPhase > 1.0f) flutterPhase -= 1.0f;
                const float d = 4.0f + wowDepth * (1.0f + std::sin (kTwoPi * wowPhase)) + flutterDepth * (1.0f + std::sin (kTwoPi * flutterPhase));
                phase += step;
                const bool sampleNow = phase >= 1.0f;
                if (sampleNow) phase -= 1.0f;
                const float m = mix.process (v[P::LofiMix]);
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const float x = io[ch][i];
                    lines[ch].push (x);
                    float y = lines[ch].read (d);
                    if (sampleNow) hold[ch] = std::round (y * q) / q;
                    y = hold[ch];
                    lp[ch] += (y - lp[ch]) * lpc;
                    hp[ch] += (lp[ch] - hp[ch]) * hpc;
                    y = softClip ((lp[ch] - hp[ch]) * (1.0f + age * 0.6f)) + hiss * rng.nextBipolar();
                    io[ch][i] = x + (y - x) * m;
                }
                // Age narrows the image a little
                const float mid = 0.5f * (b.l[i] + b.r[i]), side = 0.5f * (b.l[i] - b.r[i]) * (1.0f - age * 0.5f * m);
                b.l[i] = mid + side;
                b.r[i] = mid - side;
            }
        }

        float getWow() const { return wowPhase; }

    private:
        float sampleRate = 44100.0f, phase = 1.0f, wowPhase = 0.0f, flutterPhase = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> hold {}, lp {}, hp {};
        dsp::Random rng;
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Squash: three-band upward + downward compression ("OTT" style) – makes quiet details loud.

    class Squash
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            for (auto& b : lowA) b.lowpass (sr, 120.0, 0.7071);
            for (auto& b : lowB) b.lowpass (sr, 120.0, 0.7071);
            for (auto& b : highA) b.highpass (sr, 2500.0, 0.7071);
            for (auto& b : highB) b.highpass (sr, 2500.0, 0.7071);
            reset();
        }

        void reset()
        {
            for (auto& b : lowA) b.reset();
            for (auto& b : lowB) b.reset();
            for (auto& b : highA) b.reset();
            for (auto& b : highB) b.reset();
            env.fill (0.0f);
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float depth = v[P::SqDepth], up = v[P::SqUpward];
            const float t = 0.2f + v[P::SqTime] * 2.0f;
            const float att = std::exp (-1.0f / (0.003f * t * (float) sampleRate));
            const float rel = std::exp (-1.0f / (0.09f * t * (float) sampleRate));
            const float out = juce::Decibels::decibelsToGain (v[P::SqOut]);
            float maxGr = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                float bands[3][2];
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const float x = ch == 0 ? b.l[i] : b.r[i];
                    const float lo = lowB[ch].process (lowA[ch].process (x, 0), 0);
                    const float hi = highB[ch].process (highA[ch].process (x, 0), 0);
                    bands[0][ch] = lo;
                    bands[2][ch] = hi;
                    bands[1][ch] = x - lo - hi;
                }
                float yl = 0.0f, yr = 0.0f;
                for (size_t k = 0; k < 3; ++k)
                {
                    const float level = std::max (std::abs (bands[k][0]), std::abs (bands[k][1]));
                    env[k] = level > env[k] ? level + (env[k] - level) * att : level + (env[k] - level) * rel;
                    const float db = juce::Decibels::gainToDecibels (env[k] + 1.0e-6f, -100.0f);
                    // Downward above -24 dB (4:1), upward below -36 dB (up to +18 dB)
                    float gainDb = 0.0f;
                    if (db > -24.0f) gainDb -= (db + 24.0f) * 0.75f * depth;
                    if (db < -36.0f && db > -80.0f) gainDb += std::min (18.0f, (-36.0f - db) * 0.6f) * depth * up;
                    maxGr = std::max (maxGr, std::abs (gainDb));
                    const float g = juce::Decibels::decibelsToGain (gainDb) * (k == 1 ? 1.0f : 1.1f);
                    yl += bands[k][0] * g;
                    yr += bands[k][1] * g;
                }
                b.l[i] = yl * out;
                b.r[i] = yr * out;
            }
            lastActivity = maxGr;
        }

        float lastActivity = 0.0f;

    private:
        double sampleRate = 44100.0;
        std::array<Biquad, 2> lowA, lowB, highA, highB;
        std::array<float, 3> env {};
    };

    // ---------------------------------------------------------------------------------------------
    // Width: mid/side, with everything below the crossover kept mono

    class Width
    {
    public:
        void prepare (double sr) { sampleRate = sr; width.setTime (0.03f, (float) sr); width.reset (1.0f); reset(); lastFreq = -1.0f; }
        void reset() { for (auto& f : hp) f.reset(); }

        void process (StereoBuffer b, const Values& v)
        {
            if (! juce::exactlyEqual (v[P::WidthMono], lastFreq))
            {
                lastFreq = v[P::WidthMono];
                hp[0].highpass (sampleRate, lastFreq, 0.5412);
                hp[1].highpass (sampleRate, lastFreq, 1.3066);
            }
            for (int i = 0; i < b.n; ++i)
            {
                const float w = width.process (v[P::WidthAmount]);
                const float mid = 0.5f * (b.l[i] + b.r[i]);
                float side = 0.5f * (b.l[i] - b.r[i]);
                side = hp[1].process (hp[0].process (side, 0), 0) * w;
                b.l[i] = mid + side;
                b.r[i] = mid - side;
            }
        }

    private:
        double sampleRate = 44100.0;
        float lastFreq = -1.0f;
        std::array<Biquad, 2> hp;
        Smoother width;
    };

    // ---------------------------------------------------------------------------------------------
    // Limiter at the very end: soft, hard or tape clipping with drive and ceiling (not reorderable)

    class Clipper
    {
    public:
        static float shape (int mode, float x)
        {
            switch (mode)
            {
                case 1:  return std::clamp (x, -1.0f, 1.0f);
                case 2:  return std::tanh (x);
                default: return softClip (x * 1.15f) / softClip (1.15f);
            }
        }

        void process (StereoBuffer b, const Values& v)
        {
            const int mode = v.i (P::LimMode);
            const float drive = juce::Decibels::decibelsToGain (v[P::LimDrive]);
            const float ceil = juce::Decibels::decibelsToGain (v[P::LimCeiling]);
            float red = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                const float xl = b.l[i] * drive / ceil, xr = b.r[i] * drive / ceil;
                b.l[i] = std::clamp (shape (mode, xl), -1.0f, 1.0f) * ceil;
                b.r[i] = std::clamp (shape (mode, xr), -1.0f, 1.0f) * ceil;
                red = std::max (red, std::max (std::abs (xl), std::abs (xr)));
            }
            lastOver = red;
        }

        float lastOver = 0.0f;
    };
}
