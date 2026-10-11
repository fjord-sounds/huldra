#pragma once
#include "Analysis.h"
#include <thread>

namespace huldra
{
    // Eight synthesized source sounds so that every preset can be heard before the user loads or records
    // anything: sung vowels (formant synthesis), breath, a small choir, glass, a bowed string and a sung phrase.
    // They are made once per process on a background thread (shared by all plugin instances).
    class DemoVoices
    {
    public:
        static constexpr int kCount = 8;
        static inline const juce::StringArray names { "Aah", "Ooh", "Hum", "Breath", "Choir", "Glass", "Bowed", "Phrase" };

        static DemoVoices& get()
        {
            static DemoVoices instance;
            return instance;
        }

        // nullptr until generated
        SamplePtr voice (int index) const
        {
            if (! ready.load (std::memory_order_acquire)) return nullptr;
            return voices[(size_t) std::clamp (index, 0, kCount - 1)];
        }

        bool isReady() const { return ready.load (std::memory_order_acquire); }

        void waitUntilReady() const
        {
            while (! ready.load (std::memory_order_acquire))
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }

    private:
        DemoVoices()
        {
            worker = std::thread ([this]
            {
                for (int i = 0; i < kCount; ++i)
                    voices[(size_t) i] = make (i);
                ready.store (true, std::memory_order_release);
            });
        }

        ~DemoVoices() { if (worker.joinable()) worker.join(); }

        static constexpr double sr = 48000.0;

        // Vowel formants (frequency, bandwidth, gain) for a soprano/alto voice
        struct Formant { float f, bw, g; };
        using Vowel = std::array<Formant, 5>;
        static Vowel vowel (char v)
        {
            switch (v)
            {
                case 'a': return { { { 800, 80, 1.0f }, { 1150, 90, 0.5f }, { 2900, 120, 0.025f }, { 3900, 130, 0.1f }, { 4950, 140, 0.003f } } };
                case 'e': return { { { 350, 60, 1.0f }, { 2000, 100, 0.1f }, { 2800, 120, 0.18f }, { 3600, 150, 0.01f }, { 4950, 200, 0.0016f } } };
                case 'i': return { { { 270, 60, 1.0f }, { 2140, 90, 0.25f }, { 2950, 100, 0.05f }, { 3900, 120, 0.05f }, { 4950, 120, 0.006f } } };
                case 'o': return { { { 450, 70, 1.0f }, { 800, 80, 0.28f }, { 2830, 100, 0.08f }, { 3800, 130, 0.1f }, { 4950, 135, 0.003f } } };
                case 'm': return { { { 250, 50, 1.0f }, { 1000, 200, 0.03f }, { 2200, 200, 0.01f }, { 3000, 200, 0.004f }, { 4000, 200, 0.001f } } };
                default:  return { { { 325, 50, 1.0f }, { 700, 60, 0.16f }, { 2530, 170, 0.018f }, { 3500, 180, 0.01f }, { 4950, 200, 0.001f } } };   // u
            }
        }

        // Two-pole resonator bank
        struct Resonator
        {
            float y1 = 0, y2 = 0;
            float process (float x, float f, float bw)
            {
                const float r = std::exp (-dsp::kPi * bw / (float) sr);
                const float c = 2.0f * r * std::cos (dsp::kTwoPi * f / (float) sr);
                const float y = (1.0f - r) * x + c * y1 - r * r * y2;
                y2 = y1; y1 = y;
                return y;
            }
        };

        struct Singer
        {
            std::array<Resonator, 5> res;
            float phase = 0.0f, vibPhase = 0.0f, lastSrc = 0.0f;
            dsp::Random rng;

            // One sample of a sung vowel. breath 0..1 mixes aspiration noise.
            float sample (float hz, const Vowel& a, const Vowel& b, float morph, float breath, float vibDepth)
            {
                vibPhase += 5.2f / (float) sr;
                const float f = hz * std::exp2 (vibDepth * std::sin (dsp::kTwoPi * vibPhase) / 12.0f) * (1.0f + 0.002f * rng.nextBipolar());
                phase += f / (float) sr;
                if (phase >= 1.0f) phase -= 1.0f;
                // Glottal pulse: smooth band-limited saw-like derivative (Rosenberg-ish)
                float glottal = phase < 0.6f ? 0.5f - 0.5f * std::cos (dsp::kPi * phase / 0.6f)
                                             : std::cos (dsp::kPi * (phase - 0.6f) / 0.8f);
                glottal = std::max (0.0f, glottal);
                const float src = glottal - lastSrc;   // derivative = flow derivative (bright source)
                lastSrc = glottal;
                const float noise = rng.nextBipolar() * (0.15f + 0.85f * glottal);
                const float x = src * (1.0f - breath) * 6.0f + noise * breath * 0.25f;
                float y = 0.0f;
                for (size_t k = 0; k < 5; ++k)
                {
                    const float ff = a[k].f + (b[k].f - a[k].f) * morph;
                    const float bw = a[k].bw + (b[k].bw - a[k].bw) * morph;
                    const float g = a[k].g + (b[k].g - a[k].g) * morph;
                    y += res[k].process (x, ff, bw) * std::sqrt (g);
                }
                return y;
            }
        };

        static float midiHz (float n) { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }

        static void normalise (std::vector<float>& l, std::vector<float>& r, float target)
        {
            float pk = 0.0f;
            for (size_t i = 0; i < l.size(); ++i) pk = std::max (pk, std::max (std::abs (l[i]), r.empty() ? 0.0f : std::abs (r[i])));
            if (pk <= 0.0f) return;
            const float g = target / pk;
            for (auto& v : l) v *= g;
            for (auto& v : r) v *= g;
        }

        static float swell (int i, int n, float attack, float release)
        {
            const float t = (float) i / (float) sr, total = (float) n / (float) sr;
            return std::min (1.0f, t / attack) * std::min (1.0f, (total - t) / release);
        }

        static SamplePtr make (int index)
        {
            auto s = std::make_shared<SampleData>();
            s->uid = "demo:" + names[index];
            s->name = names[index];
            s->isDemo = true;
            s->sampleRate = sr;
            const float c4 = 60.0f;
            std::vector<float> l, r;
            dsp::Random rng;
            rng.seed (1234u + (uint32_t) index);

            auto sung = [&] (const juce::String& vowels, float seconds, float note, float breath, float vib)
            {
                const int n = (int) (seconds * sr);
                l.assign ((size_t) n, 0.0f);
                Singer singer;
                singer.rng.seed (99u + (uint32_t) index);
                for (int i = 0; i < n; ++i)
                {
                    const float pos = (float) i / (float) n * (float) (vowels.length() - 1);
                    const int k = std::min ((int) pos, vowels.length() - 2 >= 0 ? vowels.length() - 2 : 0);
                    const float morph = vowels.length() > 1 ? std::clamp (pos - (float) k, 0.0f, 1.0f) : 0.0f;
                    const auto va = vowel ((char) vowels[k]), vb = vowel ((char) vowels[std::min (k + 1, vowels.length() - 1)]);
                    const float vibAmount = vib * std::min (1.0f, (float) i / (float) (0.6 * sr));   // vibrato fades in
                    l[(size_t) i] = singer.sample (midiHz (note), va, vb, morph, breath, vibAmount) * swell (i, n, 0.08f, 0.35f);
                }
                r = l;
            };

            switch (index)
            {
                case 0: sung ("a", 4.0f, c4, 0.08f, 0.35f); break;            // Aah
                case 1: sung ("u", 4.0f, c4, 0.06f, 0.3f); break;             // Ooh
                case 2: sung ("m", 4.0f, c4 - 12.0f, 0.03f, 0.15f); break;    // Hum (an octave lower)
                case 3:                                                         // Breath: whispered "haa" swells
                {
                    const int n = (int) (4.0 * sr);
                    l.assign ((size_t) n, 0.0f);
                    std::array<Resonator, 5> res;
                    const auto v = vowel ('a');
                    for (int i = 0; i < n; ++i)
                    {
                        const float t = (float) i / (float) sr;
                        const float env = std::pow (std::sin (dsp::kPi * std::fmod (t, 2.0f) / 2.0f), 1.5f);
                        const float x = rng.nextBipolar() * env;
                        float y = 0.0f;
                        for (size_t k = 0; k < 5; ++k) y += res[k].process (x, v[k].f * 1.1f, v[k].bw * 2.5f) * std::sqrt (v[k].g);
                        l[(size_t) i] = y + x * 0.04f;
                    }
                    r = l;
                    break;
                }
                case 4:                                                         // Choir: six voices, detuned, spread
                {
                    const int n = (int) (4.5 * sr);
                    l.assign ((size_t) n, 0.0f);
                    r.assign ((size_t) n, 0.0f);
                    const float notes[] = { c4, c4, c4 - 12.0f, c4, c4 + 12.0f, c4 };
                    const char vow[] = { 'a', 'o', 'o', 'a', 'u', 'o' };
                    for (int v = 0; v < 6; ++v)
                    {
                        Singer singer;
                        singer.rng.seed (500u + (uint32_t) v);
                        singer.vibPhase = 0.17f * (float) v;
                        const float detune = 0.08f * (float) (v - 2.5f) / 2.5f;
                        const float pan = -0.8f + 1.6f * (float) v / 5.0f;
                        const auto g = dsp::panGains (pan);
                        const auto va = vowel (vow[v]);
                        for (int i = 0; i < n; ++i)
                        {
                            const float y = singer.sample (midiHz (notes[v] + detune), va, va, 0.0f, 0.12f, 0.25f) * swell (i, n, 0.4f, 0.6f);
                            l[(size_t) i] += y * g[0];
                            r[(size_t) i] += y * g[1];
                        }
                    }
                    break;
                }
                case 5:                                                         // Glass: inharmonic bell partials
                {
                    const int n = (int) (4.0 * sr);
                    l.assign ((size_t) n, 0.0f);
                    const float ratios[] = { 1.0f, 2.76f, 5.4f, 8.93f, 13.34f, 0.5f };
                    const float decays[] = { 2.6f, 1.6f, 0.9f, 0.5f, 0.3f, 3.0f };
                    const float gains[]  = { 1.0f, 0.5f, 0.3f, 0.18f, 0.1f, 0.2f };
                    for (int p = 0; p < 6; ++p)
                    {
                        const float f = midiHz (c4) * ratios[p];
                        for (int i = 0; i < n; ++i)
                        {
                            const float t = (float) i / (float) sr;
                            l[(size_t) i] += gains[p] * std::sin (dsp::kTwoPi * f * t) * std::exp (-t / decays[p])
                                             * std::min (1.0f, t / 0.002f);
                        }
                    }
                    r = l;
                    break;
                }
                case 6:                                                         // Bowed: saw + bow noise through body resonances
                {
                    const int n = (int) (4.0 * sr);
                    l.assign ((size_t) n, 0.0f);
                    std::array<Resonator, 5> body;
                    const float bodyF[] = { 280, 460, 1100, 2400, 3300 };
                    const float bodyBw[] = { 60, 90, 160, 300, 400 };
                    float phase = 0.0f, vib = 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        vib += 5.5f / (float) sr;
                        const float f = midiHz (c4 - 12.0f) * std::exp2 (0.12f * std::sin (dsp::kTwoPi * vib) * std::min (1.0f, (float) i / (float) sr) / 12.0f);
                        phase += f / (float) sr;
                        if (phase >= 1.0f) phase -= 1.0f;
                        const float saw = 2.0f * phase - 1.0f - dsp::polyBlep (phase, f / (float) sr);
                        const float x = saw * 0.6f + rng.nextBipolar() * 0.08f;
                        float y = x * 0.2f;
                        for (size_t k = 0; k < 5; ++k) y += body[k].process (x, bodyF[k], bodyBw[k]) * 0.5f;
                        l[(size_t) i] = y * swell (i, n, 0.25f, 0.4f);
                    }
                    r = l;
                    break;
                }
                default:                                                        // Phrase: "la da i o ma u" with a melody
                {
                    const struct { char v; float note, len; bool consonant; } notes[] = {
                        { 'a', c4, 0.42f, true }, { 'a', c4 + 2, 0.36f, true }, { 'i', c4 + 4, 0.48f, false },
                        { 'o', c4 + 7, 0.62f, true }, { 'a', c4 + 4, 0.38f, true }, { 'u', c4 + 2, 0.7f, false },
                        { 'e', c4 + 9, 0.45f, true }, { 'o', c4 + 7, 0.85f, true } };
                    for (const auto& nt : notes)
                    {
                        const int n = (int) (nt.len * sr);
                        const int gap = (int) (0.07 * sr);
                        Singer singer;
                        singer.rng.seed ((uint32_t) (nt.note * 31.0f));
                        const auto va = vowel (nt.v);
                        for (int i = 0; i < n; ++i)
                        {
                            const float t = (float) i / (float) sr;
                            float y = singer.sample (midiHz (nt.note), va, va, 0.0f, 0.07f, 0.2f);
                            // consonant burst at the start of a syllable
                            if (nt.consonant && t < 0.03f) y += rng.nextBipolar() * 0.25f * (1.0f - t / 0.03f);
                            l.push_back (y * std::min (1.0f, t / 0.015f) * std::min (1.0f, (nt.len - t) / 0.06f));
                        }
                        for (int i = 0; i < gap; ++i) l.push_back (0.0f);
                    }
                    r = l;
                    break;
                }
            }

            normalise (l, r, 0.7f);
            s->left = std::move (l);
            s->right = std::move (r);
            s->numChannels = 2;
            analysis::analyse (*s);
            if (index == 5 || index == 7) s->rootNote = c4;   // bell partials / a melody in C: the root is C4
            return s;
        }

        std::array<SamplePtr, kCount> voices;
        std::atomic<bool> ready { false };
        std::thread worker;
    };
}
