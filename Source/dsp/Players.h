#pragma once
#include <juce_dsp/juce_dsp.h>
#include "DspCommon.h"
#include "../sample/SampleData.h"

#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wshadow-field"
 #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wfloat-conversion"
 #pragma clang diagnostic ignored "-Wimplicit-float-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wcast-align"
 #pragma clang diagnostic ignored "-Wextra-semi"
 #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma clang diagnostic ignored "-Wold-style-cast"
 #pragma clang diagnostic ignored "-Wdeprecated"
 #pragma clang diagnostic ignored "-Wunused-variable"
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor"
 #pragma clang diagnostic ignored "-Wswitch-enum"
#endif
#include "signalsmith-stretch.h"
#if defined(__clang__)
 #pragma clang diagnostic pop
#endif

namespace huldra::dsp
{
    // The part of a sample a voice plays: a whole zone or one slice, with its loop.
    struct PlayRegion
    {
        const SampleData* s = nullptr;
        int start = 0, end = 0;               // frames, end exclusive
        int loopStart = 0, loopEnd = 0;       // frames
        int loopMode = 0;                     // 0 off, 1 forward, 2 ping-pong
        int xfade = 0;                        // crossfade length (frames), forward loops
        bool reverse = false;

        int length() const { return std::max (1, end - start); }
        bool loops() const { return loopMode != 0 && loopEnd - loopStart > 16; }
    };

    // 4-point Hermite read of one channel at a fractional frame (zero outside).
    inline float readHermite (const float* d, int n, double pos)
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - i);
        auto at = [d, n] (int k) { return k >= 0 && k < n ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    // ---------------------------------------------------------------------------------------------
    // Classic: resampling playback with forward / ping-pong loops, crossfades, reverse and mip levels.

    class ClassicPlayer
    {
    public:
        void start (const PlayRegion& r, double startFrame, double baseRatio)
        {
            region = r;
            // Mip level by the pitch at the start of the note (2 = half rate, 4 = quarter rate)
            mip = baseRatio > 3.0 && ! r.s->mipLeft[1].empty() ? 2 : (baseRatio > 1.6 && ! r.s->mipLeft[0].empty() ? 1 : 0);
            pos = startFrame;
            dir = r.reverse ? -1.0 : 1.0;
            done = false;
        }

        // Renders n samples, ratio = source frames per output sample (can change every call).
        void render (float* outL, float* outR, int n, double ratio)
        {
            const SampleData& s = *region.s;
            const double scale = mip == 0 ? 1.0 : (mip == 1 ? 0.5 : 0.25);
            const float* l = mip == 0 ? s.left.data() : s.mipLeft[(size_t) mip - 1].data();
            const float* r = s.right.empty() ? l : (mip == 0 ? s.right.data() : s.mipRight[(size_t) mip - 1].data());
            const int len = mip == 0 ? s.numFrames : (int) s.mipLeft[(size_t) mip - 1].size();
            const bool loops = region.loops();
            const double ls = region.loopStart, le = region.loopEnd, loopLen = le - ls;
            const double xf = std::min<double> ({ (double) region.xfade, loopLen * 0.5, region.reverse ? (double) (region.end - le) : ls - region.start });

            for (int i = 0; i < n; ++i)
            {
                if (done) { outL[i] = outR[i] = 0.0f; continue; }
                const double p = pos * scale;
                float a = readHermite (l, len, p), b = readHermite (r, len, p);

                // Forward loop crossfade: fade towards the audio just before the loop start
                if (loops && region.loopMode == 1 && xf > 1.0)
                {
                    if (dir > 0.0 && pos > le - xf)
                    {
                        const float t = std::clamp ((float) ((pos - (le - xf)) / xf), 0.0f, 1.0f);
                        const double q = (pos - loopLen) * scale;
                        a = a * std::sqrt (1.0f - t) + readHermite (l, len, q) * std::sqrt (t);
                        b = b * std::sqrt (1.0f - t) + readHermite (r, len, q) * std::sqrt (t);
                    }
                    else if (dir < 0.0 && pos < ls + xf)
                    {
                        const float t = std::clamp ((float) (((ls + xf) - pos) / xf), 0.0f, 1.0f);
                        const double q = (pos + loopLen) * scale;
                        a = a * std::sqrt (1.0f - t) + readHermite (l, len, q) * std::sqrt (t);
                        b = b * std::sqrt (1.0f - t) + readHermite (r, len, q) * std::sqrt (t);
                    }
                }
                outL[i] = a;
                outR[i] = b;

                pos += dir * ratio;
                if (loops)
                {
                    if (region.loopMode == 1)
                    {
                        if (dir > 0.0 && pos >= le) pos -= loopLen;
                        else if (dir < 0.0 && pos < ls) pos += loopLen;
                    }
                    else
                    {
                        if (dir > 0.0 && pos >= le) { pos = 2.0 * le - pos; dir = -1.0; }
                        else if (dir < 0.0 && pos < ls) { pos = 2.0 * ls - pos; dir = 1.0; }
                    }
                }
                if (pos >= region.end || pos < region.start)
                    done = true;
            }
        }

        bool finished() const { return done; }
        double position() const { return pos; }

    private:
        PlayRegion region;
        double pos = 0.0, dir = 1.0;
        int mip = 0;
        bool done = true;
    };

    // ---------------------------------------------------------------------------------------------
    // Granular: up to 64 grains per voice from a moving (or frozen) position.

    struct GrainParams
    {
        float position = 0.3f, scan = 0.0f, spray = 0.1f, sizeMs = 90.0f, density = 24.0f, pitchRand = 0.0f,
              octaves = 0.0f, pan = 0.4f, reverse = 0.0f, jitter = 0.0f;
        int window = 0;
        bool freeze = false;
        int unison = 1;
        float detuneCents = 0.0f;
    };

    class GranularPlayer
    {
    public:
        static constexpr int kMaxGrains = 64;

        void prepare (double hostRate) { sr = hostRate; windowTable(); }

        void start (const PlayRegion& r, float startOffset, const GrainParams& p, uint32_t seed)
        {
            region = r;
            rng.seed (seed);
            windowTable();
            for (auto& g : grains) g.active = false;
            scan = std::clamp (p.position + startOffset, 0.0f, 1.0f);
            untilNext = 0.0;
        }

        // ratio = pitch as source frames per output sample
        void render (float* outL, float* outR, int n, double ratio, const GrainParams& p)
        {
            const SampleData& s = *region.s;
            const double srcPerSec = s.sampleRate;
            const int regionLen = region.length();
            const double grainLen = std::max (8.0, p.sizeMs * 0.001 * sr);
            const double interval = sr / std::max (0.5f, p.density);
            const float overlap = std::max (1.0f, (float) (grainLen / interval));
            const float norm = 1.0f / std::sqrt (overlap);
            const float* l = s.left.data();
            const float* r = s.right.empty() ? l : s.right.data();

            for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }

            int i = 0;
            while (i < n)
            {
                if (untilNext <= 0.0)
                {
                    spawn (p, ratio, grainLen, regionLen);
                    untilNext += interval * (1.0 + p.jitter * 0.9 * rng.nextBipolar());
                }
                const int chunk = std::min (n - i, std::max (1, (int) std::ceil (untilNext)));
                renderGrains (outL + i, outR + i, chunk, l, r, s.numFrames, norm);
                untilNext -= chunk;
                if (! p.freeze)
                {
                    scan += (float) (p.scan * chunk * srcPerSec / sr / regionLen);
                    scan -= std::floor (scan);
                }
                i += chunk;
            }
        }

        // For the grain cloud display: positions (0..1 of the region) and ages of the active grains
        template <typename Fn> void forEachGrain (Fn&& fn) const
        {
            for (const auto& g : grains)
                if (g.active)
                    fn ((float) ((g.pos - region.start) / region.length()), (float) (g.age / g.length), g.pitch, g.panR - g.panL);
        }

        float scanPosition() const { return scan; }

    private:
        struct Grain
        {
            bool active = false;
            double pos = 0.0, inc = 1.0, age = 0.0, length = 1.0;
            float panL = 1.0f, panR = 1.0f, pitch = 0.0f;
            int window = 0;
        };

        static constexpr int kWindowSize = 1024;

        static const std::array<std::array<float, kWindowSize + 1>, 5>& windowTable()
        {
            static const auto table = []
            {
                std::array<std::array<float, kWindowSize + 1>, 5> t {};
                for (int shape = 0; shape < 5; ++shape)
                    for (int i = 0; i <= kWindowSize; ++i)
                        t[(size_t) shape][(size_t) i] = windowShape (shape, (float) i / (float) kWindowSize);
                return t;
            }();
            return table;
        }

        static float windowShape (int shape, float x)
        {
            switch (shape)
            {
                case 1:  { const float d = (x - 0.5f) * 4.0f; return std::exp (-d * d * 0.5f); }               // Gauss
                case 2:  return std::min (1.0f, std::min (x, 1.0f - x) * 8.0f);                                 // Flat (Tukey)
                case 3:  return std::pow (x, 2.0f) * std::min (1.0f, (1.0f - x) * 12.0f);                       // Swell
                case 4:  return std::min (1.0f, x * 60.0f) * std::pow (1.0f - x, 3.0f);                         // Pluck
                default: return 0.5f - 0.5f * std::cos (kTwoPi * x);                                            // Soft (Hann)
            }
        }

        void spawn (const GrainParams& p, double ratio, double grainLen, int regionLen)
        {
            for (auto& g : grains)
            {
                if (g.active) continue;
                float semis = p.pitchRand * rng.nextBipolar();
                if (p.octaves > 0.0f && rng.next01() < p.octaves) semis += rng.next01() < 0.7f ? 12.0f : -12.0f;
                if (p.unison > 1)
                {
                    const int u = (int) (rng.next01() * (float) p.unison);
                    semis += p.detuneCents * 0.01f * ((float) u / (float) (p.unison - 1) * 2.0f - 1.0f);
                }
                g.inc = ratio * std::exp2 (semis / 12.0f);
                const bool rev = rng.next01() < p.reverse;
                if (rev) g.inc = -g.inc;
                const float at = std::clamp (scan + p.spray * 0.5f * rng.nextBipolar(), 0.0f, 1.0f);
                g.pos = region.start + at * (float) regionLen;
                // Keep reversed grains inside the sample
                if (rev) g.pos = std::max (g.pos, (double) region.start + grainLen * std::abs (g.inc));
                g.length = grainLen;
                g.age = 0.0;
                g.window = p.window;
                g.pitch = semis;
                const auto gains = panGains (p.pan * rng.nextBipolar());
                g.panL = gains[0];
                g.panR = gains[1];
                g.active = true;
                return;
            }
        }

        void renderGrains (float* outL, float* outR, int n, const float* l, const float* r, int frames, float norm)
        {
            if (n <= 0) return;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const auto& table = windowTable()[(size_t) std::clamp (g.window, 0, 4)];
                const double scale = kWindowSize / g.length;
                for (int i = 0; i < n; ++i)
                {
                    const float w = table[(size_t) std::min (kWindowSize, (int) (g.age * scale))] * norm;
                    outL[i] += readHermite (l, frames, g.pos) * w * g.panL;
                    outR[i] += readHermite (r, frames, g.pos) * w * g.panR;
                    g.pos += g.inc;
                    g.age += 1.0;
                    if (g.age >= g.length || g.pos < 0.0 || g.pos >= frames) { g.active = false; break; }
                }
            }
        }

        PlayRegion region;
        std::array<Grain, kMaxGrains> grains {};
        double sr = 48000.0, untilNext = 0.0;
        float scan = 0.0f;
        Random rng;
    };

    // ---------------------------------------------------------------------------------------------
    // Spectral: phase-vocoder resynthesis from the analysed STFT frames. Position and pitch are independent;
    // the spectrum can be frozen, blurred, smeared, shifted, stretched, tilted, gated, harmonised and masked.

    struct SpectralParams
    {
        float position = 0.0f, scan = 1.0f, blur = 0.0f, smear = 0.0f, shiftHz = 0.0f, stretch = 1.0f,
              tilt = 0.0f, harm = 0.0f, gate = 0.0f, maskAmount = 1.0f;
        bool freeze = false;
        const float* mask = nullptr;   // kMaskBands gains, may be null
        int maskBands = 32;
    };

    class SpectralPlayer
    {
    public:
        static constexpr int N = SampleData::fftSize, bins = SampleData::numBins, hop = SampleData::specHop;

        void prepare (double hostRate)
        {
            sr = hostRate;
            fft = std::make_unique<juce::dsp::FFT> (SampleData::fftOrder);
            buffer.assign ((size_t) N * 2, 0.0f);
            ola.assign ((size_t) N, 0.0f);
            window.assign ((size_t) N, 0.0f);
            for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
            phase.assign ((size_t) bins, 0.0f);
            smoothMag.assign ((size_t) bins, 0.0f);
            outMag.assign ((size_t) bins, 0.0f);
            outFreq.assign ((size_t) bins, 0.0f);
            binMask.assign ((size_t) bins, 1.0f);
        }

        void start (const PlayRegion& r, float startOffset, const SpectralParams& p, uint32_t seed)
        {
            region = r;
            rng.seed (seed);
            const float startFrac = std::clamp (p.position + startOffset, 0.0f, 1.0f);
            framePos = (region.start + startFrac * (float) region.length()) / (double) hop;
            std::fill (ola.begin(), ola.end(), 0.0f);
            std::fill (smoothMag.begin(), smoothMag.end(), 0.0f);
            for (auto& ph : phase) ph = rng.next01() * kTwoPi;
            readPos = 0;
            endCount = 0;
            first = true;
            done = false;
            lastFrame = 0.0f;
        }

        void render (float* out, int n, double pitchRatio, const SpectralParams& p)
        {
            for (int i = 0; i < n; ++i)
            {
                if (readPos == 0)
                    synthesise (pitchRatio, p);
                out[i] = ola[(size_t) readPos];
                ola[(size_t) readPos] = 0.0f;
                readPos = (readPos + 1) % hop;
                if (readPos == 0)
                {
                    // shift the overlap-add buffer by one hop
                    std::memmove (ola.data(), ola.data() + hop, sizeof (float) * (size_t) (N - hop));
                    std::fill (ola.end() - hop, ola.end(), 0.0f);
                }
            }
        }

        bool finished() const { return done; }
        float currentFrame() const { return lastFrame; }        // for the display
        const std::vector<float>& lastSpectrum() const { return smoothMag; }

    private:
        void synthesise (double pitchRatio, const SpectralParams& p)
        {
            const SampleData& s = *region.s;
            if (s.numSpecFrames <= 1) { done = true; return; }
            const int firstFrame = region.start / hop, lastFrameIdx = std::min (s.numSpecFrames - 1, region.end / hop);
            const bool loops = region.loops();
            const double loopA = region.loopStart / (double) hop, loopB = region.loopEnd / (double) hop;

            // Interpolate between neighbouring frames
            const int f0 = std::clamp ((int) framePos, 0, s.numSpecFrames - 1);
            const int f1 = std::min (f0 + 1, s.numSpecFrames - 1);
            const float t = (float) (framePos - std::floor (framePos));
            const float* m0 = s.specFrameMag (f0);
            const float* m1 = s.specFrameMag (f1);
            const float* fr = s.specFrameFreq (t < 0.5f ? f0 : f1);
            lastFrame = (float) framePos;

            const float map = (float) pitchRatio;   // source frames per output sample = bin mapping
            const float shiftBins = p.shiftHz * (float) N / (float) sr;
            const float tiltRef = 64.0f;
            std::fill (outMag.begin(), outMag.end(), 0.0f);
            std::fill (outFreq.begin(), outFreq.end(), 0.0f);

            float frameMax = 0.0f;
            for (int k = 1; k < bins; ++k) frameMax = std::max (frameMax, m0[k]);
            const float gateLevel = frameMax * p.gate * p.gate * 0.5f;

            // Mask gains per output bin (log-spaced bands from 40 Hz to 20 kHz)
            const bool useMask = p.mask != nullptr && p.maskAmount > 0.001f;
            if (useMask)
                for (int j = 0; j < bins; ++j)
                {
                    const float hz = (float) j * (float) sr / (float) N;
                    const float b = std::clamp (std::log2 (std::max (40.0f, hz) / 40.0f) / std::log2 (500.0f) * (float) p.maskBands, 0.0f, (float) p.maskBands - 1.001f);
                    const int bi = (int) b;
                    const float g = p.mask[bi] + (p.mask[bi + 1 < p.maskBands ? bi + 1 : bi] - p.mask[bi]) * (b - (float) bi);
                    binMask[(size_t) j] = 1.0f + (g - 1.0f) * p.maskAmount;
                }

            auto place = [&] (float f, float m)
            {
                const int j = (int) std::lround (f);
                if (j <= 0 || j >= bins) return;
                if (m > outMag[(size_t) j]) outFreq[(size_t) j] = f;
                outMag[(size_t) j] += m;
            };

            for (int k = 1; k < bins; ++k)
            {
                float m = m0[k] + (m1[k] - m0[k]) * t;
                if (m < gateLevel) continue;
                float f = fr[k] * map;
                if (std::abs (p.stretch - 1.0f) > 0.001f)
                    f = tiltRef * std::pow (std::max (0.01f, f) / tiltRef, p.stretch);
                f += shiftBins;
                if (std::abs (p.tilt) > 0.001f)
                    m *= std::pow (std::max (1.0f, f) / tiltRef, p.tilt * 1.2f);
                place (f, m);
                if (p.harm > 0.001f)
                {
                    place (f * 1.4983f, m * p.harm * 0.7f);   // fifth
                    place (f * 2.0f, m * p.harm * 0.5f);      // octave
                }
            }

            // Blur: smooth magnitudes over time
            const float follow = 1.0f - p.blur * 0.985f;
            const float phaseStep = kTwoPi * (float) hop / (float) N;
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int j = 1; j < bins - 1; ++j)
            {
                auto& sm = smoothMag[(size_t) j];
                sm += (outMag[(size_t) j] - sm) * (first ? 1.0f : follow);
                const float mag = sm * (useMask ? binMask[(size_t) j] : 1.0f);
                const float f = outFreq[(size_t) j] > 0.0f ? outFreq[(size_t) j] : (float) j;
                auto& ph = phase[(size_t) j];
                ph += phaseStep * f + p.smear * kPi * rng.nextBipolar();
                ph -= kTwoPi * std::floor (ph / kTwoPi);
                const float amp = mag * (float) N * 0.5f;
                buffer[(size_t) j * 2] = amp * std::cos (ph);
                buffer[(size_t) j * 2 + 1] = amp * std::sin (ph);
            }
            first = false;
            fft->performRealOnlyInverseTransform (buffer.data());
            constexpr float olaGain = 1.0f / 1.5f * 0.5f;   // Hann^2 at 75 % overlap sums to 1.5; lobe energy ~2 bins
            for (int i = 0; i < N; ++i)
                ola[(size_t) i] += buffer[(size_t) i] * window[(size_t) i] * olaGain;

            // Advance through the sound
            if (! p.freeze)
            {
                framePos += p.scan * s.sampleRate / sr;
                if (loops)
                {
                    if (framePos >= loopB) framePos = loopA + std::fmod (framePos - loopA, std::max (1.0, loopB - loopA));
                    else if (framePos < loopA && p.scan < 0.0f) framePos = loopB - std::fmod (loopA - framePos, std::max (1.0, loopB - loopA));
                }
                else if (framePos >= lastFrameIdx || framePos < firstFrame)
                {
                    framePos = std::clamp (framePos, (double) firstFrame, (double) lastFrameIdx);
                    endCount++;
                    if (endCount > N / hop + 1) done = true;
                }
            }
        }

        PlayRegion region;
        double sr = 48000.0, framePos = 0.0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> buffer, ola, window, phase, smoothMag, outMag, outFreq, binMask;
        int readPos = 0, endCount = 0;
        bool first = true, done = false;
        float lastFrame = 0.0f;
        Random rng;
    };

    // ---------------------------------------------------------------------------------------------
    // Vocal: Signalsmith Stretch – pitch and time independent, formants kept or shifted. The first few
    // milliseconds come from the classic player (the stretcher needs a moment), then it crossfades over.

    struct VocalParams
    {
        float speed = 1.0f, formant = 0.0f, tonality = 0.0f;
        bool keepFormants = true;
    };

    class VocalPlayer
    {
    public:
        void prepare (double hostRate, int maxBlock)
        {
            sr = hostRate;
            stretch.configure (1, (int) (sr * 0.07), (int) (sr * 0.0175));
            in.assign ((size_t) (maxBlock * 16 + 64), 0.0f);
            out.assign ((size_t) maxBlock + 16, 0.0f);
            classicL.assign ((size_t) maxBlock + 16, 0.0f);
            classicR.assign ((size_t) maxBlock + 16, 0.0f);
        }

        void start (const PlayRegion& r, double startFrame)
        {
            region = r;
            stretch.reset();
            pos = startFrame;
            dir = r.reverse ? -1.0 : 1.0;
            inputDebt = 0.0;
            produced = 0;
            tailCount = 0;
            done = false;
            preroll.start (r, startFrame, 1.0);
            // Hand the stretcher the audio just before the start so it begins in time
            const int lat = stretch.inputLatency();
            const int count = std::min ((int) in.size(), lat);
            for (int i = 0; i < count; ++i)
            {
                const double p = startFrame + dir * (i - count);
                in[(size_t) i] = monoAt (p);
            }
            float* ins[] = { in.data() };
            stretch.seek (ins, count, 1.0);
            outLatency = stretch.outputLatency();
        }

        void render (float* outL, float* outR, int n, double pitchRatio, const VocalParams& p)
        {
            const SampleData& s = *region.s;
            const double rateFix = s.sampleRate / sr;
            const float factor = (float) (pitchRatio);   // transposition relative to the source pitch
            stretch.setTransposeFactor (factor, p.tonality > 0.001f ? (float) ((1.0f - p.tonality * 0.85f) * 8000.0 / sr) : 0.0f);
            stretch.setFormantSemitones (p.formant, p.keepFormants);

            // Feed the input: 'speed' source seconds per output second
            inputDebt += n * p.speed * rateFix;
            int count = std::min ((int) in.size(), (int) inputDebt);
            inputDebt -= count;
            for (int i = 0; i < count; ++i)
            {
                in[(size_t) i] = monoAt (pos);
                advance (1.0);
            }
            float* ins[] = { in.data() };
            float* outs[] = { out.data() };
            stretch.process (ins, count, outs, n);

            // Crossfade from the pre-roll (classic player) into the stretcher output
            const int fadeStart = outLatency, fadeLen = (int) (0.02 * sr);
            if (produced < fadeStart + fadeLen)
                preroll.render (classicL.data(), classicR.data(), n, pitchRatio);
            for (int i = 0; i < n; ++i)
            {
                const int t = produced + i;
                float y = out[(size_t) i];
                if (t < fadeStart + fadeLen)
                {
                    const float x = std::clamp ((float) (t - fadeStart) / (float) fadeLen, 0.0f, 1.0f);
                    const float c = 0.5f * (classicL[(size_t) i] + classicR[(size_t) i]);
                    y = c * std::sqrt (1.0f - x) + y * std::sqrt (x);
                }
                outL[i] = y;
                outR[i] = y;
            }
            produced += n;
        }

        bool finished() const { return done; }
        double position() const { return pos; }

    private:
        float monoAt (double p) const
        {
            const SampleData& s = *region.s;
            const float a = readHermite (s.left.data(), s.numFrames, p);
            return s.right.empty() ? a : 0.5f * (a + readHermite (s.right.data(), s.numFrames, p));
        }

        void advance (double step)
        {
            pos += dir * step;
            if (region.loops())
            {
                const double ls = region.loopStart, le = region.loopEnd, len = le - ls;
                if (region.loopMode == 1)
                {
                    if (dir > 0.0 && pos >= le) pos -= len;
                    else if (dir < 0.0 && pos < ls) pos += len;
                }
                else
                {
                    if (dir > 0.0 && pos >= le) { pos = 2.0 * le - pos; dir = -1.0; }
                    else if (dir < 0.0 && pos < ls) { pos = 2.0 * ls - pos; dir = 1.0; }
                }
            }
            else if (pos >= region.end || pos < region.start)
            {
                pos = std::clamp (pos, (double) region.start, (double) region.end);
                ++tailCount;
                if (tailCount > (int) (sr * 0.3)) done = true;
            }
        }

        PlayRegion region;
        signalsmith::stretch::SignalsmithStretch<float> stretch;
        ClassicPlayer preroll;
        std::vector<float> in, out, classicL, classicR;
        double sr = 48000.0, pos = 0.0, dir = 1.0, inputDebt = 0.0;
        int produced = 0, outLatency = 0, tailCount = 0;
        bool done = false;
    };
}
