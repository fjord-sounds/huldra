#pragma once
#include "Players.h"
#include "Envelope.h"
#include "Modulation.h"
#include "../state/InstrumentState.h"
#include "../motion/Scale.h"

namespace huldra::dsp
{
    enum class EngineType { Classic, Granular, Spectral, Vocal };

    // Shared, per block: everything a voice needs besides its own state.
    struct VoiceContext
    {
        const P::Values* values = nullptr;
        const Snapshot* snap = nullptr;
        const ModRouting* routing = nullptr;
        const float* globalSources = nullptr;       // kNumModSources (per-voice slots are overwritten by the voice)
        std::array<float, P::kNumLfos> globalLfo {};
        double bpm = 120.0;
        float hostRate = 48000.0f;
    };

    // Everything decided at note-on.
    struct VoiceStart
    {
        PlayRegion region;
        float startFrac = 0.0f;       // 0..1 inside the region
        float keyOffset = 0.0f;       // semitones away from the root (0 for chops)
        float zoneTune = 0.0f, zoneGain = 1.0f, zonePan = 0.0f;
        int engine = 0;
        KeyEdit key;
        int note = 60, channel = 1;
        float velocity = 0.8f;
        float glideFrom = -1.0f;      // note to glide from, < 0 = none
        bool isChop = false, oneShot = false;
        int choke = 0;
        uint32_t seed = 1;
    };

    // Per-voice filter: SVF modes, a vowel formant bank, a comb and a ladder.
    class VoiceFilter
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            for (auto& c : comb) c.assign (4096, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& s : svf) s = {};
            for (auto& s : svf2) s = {};
            for (auto& v : vowel) for (auto& s : v) s = {};
            for (auto& l : ladder) l.fill (0.0f);
            for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
            combPos = 0;
        }

        // Called at control rate
        void set (int filterType, float cutoffHz, float resonance, float vowelPos)
        {
            type = filterType;
            cutoff = std::clamp (cutoffHz, 16.0f, sr * 0.45f);
            reso = resonance;
            g = std::tan (kPi * cutoff / sr);
            k = 2.0f - 1.96f * std::clamp (reso, 0.0f, 1.0f);
            if (type == 5)
            {
                // A E I O U formants (F1..F3), shifted by the cutoff (1 kHz = neutral)
                static const float f[5][3] = { { 800, 1150, 2900 }, { 400, 2000, 2800 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const int a = std::clamp ((int) vowelPos, 0, 4), b = std::min (a + 1, 4);
                const float t = std::clamp (vowelPos - (float) a, 0.0f, 1.0f);
                const float shift = std::pow (cutoff / 1000.0f, 0.5f);
                for (int i = 0; i < 3; ++i)
                {
                    const float hz = std::clamp ((f[a][i] + (f[b][i] - f[a][i]) * t) * shift, 40.0f, sr * 0.45f);
                    vg[(size_t) i] = std::tan (kPi * hz / sr);
                }
                vk = 0.25f - 0.2f * reso;
            }
        }

        void process (float& l, float& r)
        {
            switch (type)
            {
                case 0: l = svfRun (svf[0], l, g, k, 0); r = svfRun (svf[1], r, g, k, 0); break;
                case 1: l = svfRun (svf2[0], svfRun (svf[0], l, g, k, 0), g, k, 0); r = svfRun (svf2[1], svfRun (svf[1], r, g, k, 0), g, k, 0); break;
                case 2: l = svfRun (svf[0], l, g, k, 1); r = svfRun (svf[1], r, g, k, 1); break;
                case 3: l = svfRun (svf[0], l, g, k, 2); r = svfRun (svf[1], r, g, k, 2); break;
                case 4: l = svfRun (svf[0], l, g, k, 3); r = svfRun (svf[1], r, g, k, 3); break;
                case 5:
                {
                    float ol = 0.0f, orr = 0.0f;
                    const float gains[] = { 1.0f, 0.6f, 0.35f };
                    for (size_t i = 0; i < 3; ++i)
                    {
                        ol += svfRun (vowel[0][i], l, vg[i], vk, 2) * gains[i];
                        orr += svfRun (vowel[1][i], r, vg[i], vk, 2) * gains[i];
                    }
                    l = ol * 2.2f; r = orr * 2.2f;
                    break;
                }
                case 6:
                {
                    const float delay = std::clamp (sr / cutoff, 2.0f, 4000.0f);
                    const float fb = 0.5f + reso * 0.48f;
                    float* io[] = { &l, &r };
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float rp = (float) combPos - delay;
                        const int i0 = (int) std::floor (rp);
                        const float fr = rp - (float) i0;
                        const auto& buf = comb[c];
                        const float d = buf[(size_t) (i0 & 4095)] * (1.0f - fr) + buf[(size_t) ((i0 + 1) & 4095)] * fr;
                        const float y = *io[c] + d * fb;
                        comb[c][(size_t) combPos] = softClip (y);
                        *io[c] = y * (1.0f - fb * 0.5f);
                    }
                    combPos = (combPos + 1) & 4095;
                    break;
                }
                default:
                {
                    const float gg = 1.0f - std::exp (-kTwoPi * cutoff / sr);
                    float* io[] = { &l, &r };
                    for (size_t c = 0; c < 2; ++c)
                    {
                        auto& s = ladder[c];
                        float in = std::tanh (*io[c] - reso * 3.9f * s[3]);
                        for (size_t i = 0; i < 4; ++i)
                        {
                            s[i] += gg * (std::tanh (in) - std::tanh (s[i]));
                            in = s[i];
                        }
                        *io[c] = s[3] * (1.0f + reso * 1.4f);
                    }
                    break;
                }
            }
        }

    private:
        struct SvfState { float ic1 = 0.0f, ic2 = 0.0f; };

        static float svfRun (SvfState& s, float x, float g, float k, int mode)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1, a3 = g * a2;
            const float v3 = x - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            switch (mode)
            {
                case 1:  return x - k * v1 - v2;
                case 2:  return v1 * k;
                case 3:  return x - k * v1;
                default: return v2;
            }
        }

        float sr = 48000.0f, cutoff = 1000.0f, reso = 0.0f, g = 0.1f, k = 2.0f, vk = 0.2f;
        int type = 0;
        std::array<SvfState, 2> svf {}, svf2 {};
        std::array<std::array<SvfState, 3>, 2> vowel {};
        std::array<float, 3> vg {};
        std::array<std::array<float, 4>, 2> ladder {};
        std::array<std::vector<float>, 2> comb;
        int combPos = 0;
    };

    // Small chorus that makes one spectral / vocal stream sound like up to four unison voices.
    class UnisonSpread
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; buf.assign (8192, 0.0f); pos = 0; phase = 0.0f; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }

        void process (float& l, float& r, float mono, int voices, float detuneCents, float spread)
        {
            buf[(size_t) pos] = mono;
            float ol = mono, orr = mono;
            phase += 0.37f / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            for (int v = 1; v < voices; ++v)
            {
                const float ph = phase + (float) v * 0.29f;
                const float depthMs = 0.4f + detuneCents * 0.06f;
                const float d = (6.0f + 5.0f * (float) v + depthMs * std::sin (kTwoPi * ph)) * 0.001f * sr;
                const float rp = (float) pos - d;
                const int i0 = (int) std::floor (rp);
                const float fr = rp - (float) i0;
                const float y = buf[(size_t) (i0 & 8191)] * (1.0f - fr) + buf[(size_t) ((i0 + 1) & 8191)] * fr;
                const float side = (v % 2 == 1 ? -1.0f : 1.0f) * spread;
                const auto gains = panGains (side);
                ol += y * gains[0] * 0.7f;
                orr += y * gains[1] * 0.7f;
            }
            const float norm = 1.0f / std::sqrt ((float) voices);
            l = ol * norm;
            r = orr * norm;
            pos = (pos + 1) & 8191;
        }

    private:
        float sr = 48000.0f, phase = 0.0f;
        std::vector<float> buf;
        int pos = 0;
    };

    class Voice
    {
    public:
        static constexpr int kControlBlock = 32;

        void prepare (double sampleRate, int maxBlock)
        {
            sr = (float) sampleRate;
            granular.prepare (sampleRate);
            spectral.prepare (sampleRate);
            vocal.prepare (sampleRate, kControlBlock);
            filter.prepare (sr);
            unisonFx.prepare (sr);
            amp.setSampleRate (sr);
            fenv.setSampleRate (sr);
            juce::ignoreUnused (maxBlock);
            active = false;
        }

        void start (const VoiceStart& vs, const VoiceContext& ctx)
        {
            st = vs;
            use.set (vs.region.s);
            const auto& v = *ctx.values;
            active = true;
            released = false;
            fade = -1;
            age = 0;
            ++serial;
            rng.seed (vs.seed);
            randomSource = rng.nextBipolar();
            pressure = slide = 0.0f;
            engine = (EngineType) vs.engine;
            glideOffset = vs.glideFrom >= 0.0f ? vs.glideFrom - (float) vs.note : 0.0f;
            retune = 0.0f;
            timeSinceStart = 0.0f;
            for (int i = 0; i < P::kNumLfos; ++i) lfos[(size_t) i].reset (0.0f, vs.seed * 31u + (uint32_t) i);
            for (auto& m : msegs) m.start();
            filter.reset();
            unisonFx.reset();
            loadBaseValues (v);
            current = base;
            updateControl (ctx, true);

            const auto& region = st.region;
            const double len = region.length();
            const double startFrame = region.reverse ? region.end - 1 - st.startFrac * (len - 1) : region.start + st.startFrac * (len - 1);
            const int uni = engine == EngineType::Classic ? std::clamp (v.i (P::UniVoices), 1, kMaxUnison) : 1;
            numClassic = uni;
            for (int u = 0; u < uni; ++u)
                classic[(size_t) u].start (region, startFrame, ratio);
            if (engine == EngineType::Granular) granular.start (region, st.startFrac, grainParams (v), vs.seed);
            if (engine == EngineType::Spectral) spectral.start (region, st.startFrac, spectralParams (ctx), vs.seed);
            if (engine == EngineType::Vocal)    vocal.start (region, startFrame);

            amp.setParams (val (P::AmpAttack), val (P::AmpDecay), val (P::AmpSustain), val (P::AmpRelease));
            fenv.setParams (val (P::FenvAttack), val (P::FenvDecay), val (P::FenvSustain), val (P::FenvRelease));
            amp.noteOn();
            fenv.noteOn();
        }

        // Legato: new pitch without restarting the sample or the envelopes
        void retrigger (int note, float velocity, float glideFromNote)
        {
            // keep sounding where the glide currently is, then glide on to the new note
            glideOffset += glideFromNote - (float) note;
            st.keyOffset += (float) (note - st.note);
            st.note = note;
            st.velocity = velocity;
            released = false;
        }

        void release()
        {
            if (! active || released) return;
            released = true;
            if (st.oneShot) return;     // one-shot chops play to their end
            amp.noteOff();
            fenv.noteOff();
        }

        // Fast fade (stolen or choked)
        void kill() { if (active && fade < 0) fade = (int) (0.004f * sr) + 1; fadeLen = fade; }

        void setExpression (float press, float slideValue) { pressure = press; slide = slideValue; }

        void render (float* outL, float* outR, int n, const VoiceContext& ctx)
        {
            int done = 0;
            while (done < n && active)
            {
                const int chunk = std::min (kControlBlock - controlCounter, n - done);
                if (controlCounter == 0)
                    updateControl (ctx, false);
                renderChunk (outL + done, outR + done, chunk, ctx);
                done += chunk;
                controlCounter = (controlCounter + chunk) % kControlBlock;
            }
        }

        // ---- State for the synth and the displays ----
        bool isActive() const { return active; }
        bool isReleased() const { return released; }
        bool isFading() const { return fade >= 0; }
        int getNote() const { return st.note; }
        int getChannel() const { return st.channel; }
        int getChoke() const { return st.choke; }
        bool isChop() const { return st.isChop; }
        uint64_t getAge() const { return age; }
        uint32_t getSerial() const { return serial; }
        float getLevel() const { return amp.current(); }
        EngineType getEngine() const { return engine; }
        const SampleData* getSample() const { return st.region.s; }
        float getVelocity() const { return st.velocity; }
        float getSource (P::ModSource s) const { return sources[(size_t) s]; }

        // Playhead 0..1 of the whole sample
        float playhead() const
        {
            if (st.region.s == nullptr || st.region.s->numFrames <= 0) return 0.0f;
            double pos = 0.0;
            switch (engine)
            {
                case EngineType::Classic:  pos = classic[0].position(); break;
                case EngineType::Vocal:    pos = vocal.position(); break;
                case EngineType::Spectral: pos = spectral.currentFrame() * SampleData::specHop; break;
                case EngineType::Granular: pos = st.region.start + granular.scanPosition() * st.region.length(); break;
            }
            return (float) (pos / st.region.s->numFrames);
        }

        const GranularPlayer& getGranular() const { return granular; }
        const PlayRegion& getRegion() const { return st.region; }

    private:
        float val (P::Id id) const { return current[(size_t) (id - P::VoiceFirst)]; }

        void loadBaseValues (const P::Values& v)
        {
            for (int i = 0; i < P::kNumVoiceParams; ++i)
                base[(size_t) i] = v.v[(size_t) (P::VoiceFirst + i)];
        }

        GrainParams grainParams (const P::Values& v) const
        {
            GrainParams p;
            p.position = val (P::GrnPos);
            p.scan = val (P::GrnScan);
            p.spray = val (P::GrnSpray);
            p.sizeMs = val (P::GrnSize);
            p.density = val (P::GrnDensity);
            p.pitchRand = val (P::GrnPitchRand);
            p.octaves = val (P::GrnOctaves);
            p.pan = val (P::GrnPan);
            p.reverse = val (P::GrnReverse);
            p.jitter = val (P::GrnJitter);
            p.window = juce::roundToInt (val (P::GrnWindow));
            p.freeze = val (P::GrnFreeze) > 0.5f;
            p.unison = std::clamp (v.i (P::UniVoices), 1, kMaxUnison);
            p.detuneCents = val (P::UniDetune);
            return p;
        }

        SpectralParams spectralParams (const VoiceContext& ctx) const
        {
            SpectralParams p;
            p.position = val (P::SpcPos);
            p.scan = val (P::SpcScan);
            p.freeze = val (P::SpcFreeze) > 0.5f;
            p.blur = val (P::SpcBlur);
            p.smear = val (P::SpcSmear);
            p.shiftHz = val (P::SpcShift);
            p.stretch = val (P::SpcStretch);
            p.tilt = val (P::SpcTilt);
            p.harm = val (P::SpcHarm);
            p.gate = val (P::SpcGate);
            p.maskAmount = val (P::SpcMask);
            p.mask = ctx.snap != nullptr ? ctx.snap->mask.data() : nullptr;
            p.maskBands = kMaskBands;
            return p;
        }

        // Mod sources, modulated parameters, pitch, filter and envelope settings (every kControlBlock samples)
        void updateControl (const VoiceContext& ctx, bool atStart)
        {
            const auto& v = *ctx.values;
            const float dt = (float) kControlBlock / sr;
            if (! atStart)
            {
                loadBaseValues (v);
                timeSinceStart += dt;
            }

            // Sources
            std::copy (ctx.globalSources, ctx.globalSources + P::kNumModSources, sources.begin());
            sources[(size_t) P::ModSource::Velocity] = st.velocity;
            sources[(size_t) P::ModSource::Key] = ((float) st.note - 60.0f) / 60.0f;
            sources[(size_t) P::ModSource::Random] = randomSource;
            sources[(size_t) P::ModSource::Pressure] = pressure;
            sources[(size_t) P::ModSource::Slide] = slide;
            sources[(size_t) P::ModSource::AmpEnv] = amp.current();
            sources[(size_t) P::ModSource::FilterEnv] = fenv.current();
            for (int i = 0; i < P::kNumLfos; ++i)
            {
                float value;
                if (v.b (P::lfoParam (i, 4)))   // retriggered per note
                {
                    if (! atStart) lfos[(size_t) i].advance (lfoRate (v, i, ctx.bpm) * dt);
                    value = lfos[(size_t) i].value (v.i (P::lfoParam (i, 0)));
                }
                else
                    value = ctx.globalLfo[(size_t) i];
                const float fadeTime = v[P::lfoParam (i, 5)];
                if (fadeTime > 0.001f) value *= std::min (1.0f, timeSinceStart / fadeTime);
                sources[(size_t) P::ModSource::Lfo1 + (size_t) i] = value;
            }
            if (ctx.snap != nullptr)
                for (int m = 0; m < kNumMsegs; ++m)
                    sources[(size_t) P::ModSource::Mseg1 + (size_t) m] =
                        msegs[(size_t) m].advance (ctx.snap->msegs[(size_t) m], atStart ? 0.0f : dt, v[P::msegTime (m)], v.b (P::msegLoop (m)));

            // Modulated voice parameters
            current = base;
            applyModulation (*ctx.routing, sources.data(), P::VoiceFirst, P::VoiceLast,
                             [this] (P::Id id) { return base[(size_t) (id - P::VoiceFirst)]; },
                             [this] (P::Id id, float x) { current[(size_t) (id - P::VoiceFirst)] = x; });

            // Pitch
            if (std::abs (glideOffset) > 0.001f)
            {
                const float glide = std::max (0.001f, v[P::Glide]);
                glideOffset *= std::exp (-dt / (glide * 0.3f));
            }
            else glideOffset = 0.0f;

            const float bend = sources[(size_t) P::ModSource::PitchBend] * (float) v.i (P::BendRange);
            float semis = (st.keyOffset + glideOffset) * val (P::KeyTrack) + val (P::Tune) + val (P::Fine) * 0.01f
                        + st.zoneTune + st.key.tune + bend;

            // Retune: pulls the sample's own melody to the scale (or the nearest semitone)
            const float retuneAmt = v[P::RetuneAmt];
            if (retuneAmt > 0.001f && st.region.s != nullptr && engine != EngineType::Granular && engine != EngineType::Spectral)
            {
                const double pos = engine == EngineType::Vocal ? vocal.position() : classic[0].position();
                const float detected = st.region.s->pitchAt (pos);
                if (detected > 0.0f)
                {
                    const int root = v.i (P::SclRoot), type = v.b (P::SclOn) ? v.i (P::SclType) : 11;
                    const float target = motion::snapToScale (detected, root, type);
                    const float want = (target - detected) * retuneAmt;
                    const float speed = 1.0f - std::exp (-dt / (0.002f + (1.0f - v[P::RetuneSpeed]) * 0.15f));
                    retune += (want - retune) * speed;
                }
            }
            semis += retune;

            const auto* s = st.region.s;
            const double rateFix = s != nullptr ? s->sampleRate / sr : 1.0;
            ratio = std::exp2 (semis / 12.0f) * rateFix;
            detune = val (P::UniDetune);

            // Filter
            filterOn = val (P::FltOn) > 0.5f;
            if (filterOn)
            {
                float cutoff = val (P::FltCutoff);
                cutoff *= std::exp2 ((float) (st.note - 60) / 12.0f * val (P::FltKey));
                cutoff *= std::exp2 (val (P::FltVel) * (st.velocity - 1.0f) * 4.0f);
                cutoff *= std::exp2 (val (P::FltEnv) * fenv.current() + st.key.cutoff);
                filter.set (juce::roundToInt (val (P::FltType)), cutoff, val (P::FltReso), val (P::FltVowel));
            }

            if (! atStart && ! released)
            {
                amp.setParams (val (P::AmpAttack), val (P::AmpDecay), val (P::AmpSustain), val (P::AmpRelease));
                fenv.setParams (val (P::FenvAttack), val (P::FenvDecay), val (P::FenvSustain), val (P::FenvRelease));
            }
            else if (released)
            {
                amp.setParams (val (P::AmpAttack), val (P::AmpDecay), val (P::AmpSustain), val (P::AmpRelease));
            }

            // Output gain and pan
            const float velAmt = val (P::VelAmp);
            const float velGain = 1.0f - velAmt + velAmt * st.velocity * st.velocity;
            targetGain = val (P::Level) * velGain * st.zoneGain * juce::Decibels::decibelsToGain (st.key.gainDb) * (st.key.mute ? 0.0f : 1.0f);
            pan = std::clamp (val (P::Pan) + st.zonePan + st.key.pan, -1.0f, 1.0f);
            drive = val (P::Drive);

            // Loop points can move while the note plays
            if (atStart) gain = targetGain;
        }

        void renderChunk (float* outL, float* outR, int n, const VoiceContext& ctx)
        {
            float* bl = bufL.data();
            float* br = bufR.data();
            const auto& v = *ctx.values;

            switch (engine)
            {
                case EngineType::Classic:
                {
                    if (numClassic <= 1)
                        classic[0].render (bl, br, n, ratio);
                    else
                    {
                        std::fill (bl, bl + n, 0.0f);
                        std::fill (br, br + n, 0.0f);
                        const float spread = val (P::UniSpread);
                        for (int u = 0; u < numClassic; ++u)
                        {
                            const float x = (float) u / (float) (numClassic - 1) * 2.0f - 1.0f;
                            const double r = ratio * std::exp2 (x * detune * 0.01f / 12.0f);
                            classic[(size_t) u].render (tmpL.data(), tmpR.data(), n, r);
                            const auto g = panGains (x * spread);
                            const float norm = 1.0f / std::sqrt ((float) numClassic);
                            for (int i = 0; i < n; ++i)
                            {
                                bl[i] += tmpL[(size_t) i] * g[0] * norm;
                                br[i] += tmpR[(size_t) i] * g[1] * norm;
                            }
                        }
                    }
                    if (classic[0].finished()) startEndFade();
                    break;
                }
                case EngineType::Granular:
                    granular.render (bl, br, n, ratio, grainParams (v));
                    break;
                case EngineType::Spectral:
                {
                    spectral.render (tmpL.data(), n, ratio, spectralParams (ctx));
                    spreadMono (bl, br, n, v);
                    if (spectral.finished()) startEndFade();
                    break;
                }
                case EngineType::Vocal:
                {
                    VocalParams p;
                    p.speed = val (P::VocSpeed);
                    p.formant = val (P::VocFormant) + st.key.formant;
                    p.keepFormants = val (P::VocKeep) > 0.5f;
                    p.tonality = val (P::VocTonal);
                    vocal.render (tmpL.data(), tmpR.data(), n, ratio, p);
                    spreadMono (bl, br, n, v);
                    if (vocal.finished()) startEndFade();
                    break;
                }
            }

            const auto panG = panGains (pan);
            const float driveGain = 1.0f + drive * 8.0f;
            const float driveNorm = 1.0f / (1.0f + drive * 2.5f);
            const float gainStep = (targetGain - gain) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                float l = bl[i], r = br[i];
                if (drive > 0.001f) { l = softClip (l * driveGain) * driveNorm; r = softClip (r * driveGain) * driveNorm; }
                if (filterOn) filter.process (l, r);
                float e = amp.next();
                fenv.next();
                gain += gainStep;
                if (fade >= 0)
                {
                    e *= (float) fade / (float) std::max (1, fadeLen);
                    if (--fade <= 0) { stop(); return; }
                }
                outL[i] += l * e * gain * panG[0];
                outR[i] += r * e * gain * panG[1];
            }
            age += (uint64_t) n;
            if (! amp.isActive()) stop();
        }

        void spreadMono (float* bl, float* br, int n, const P::Values& v)
        {
            const int uni = std::clamp (v.i (P::UniVoices), 1, kMaxUnison);
            const float spread = val (P::UniSpread);
            for (int i = 0; i < n; ++i)
            {
                const float m = tmpL[(size_t) i];
                if (uni > 1) unisonFx.process (bl[i], br[i], m, uni, detune, spread);
                else { bl[i] = m; br[i] = m; }
            }
        }

        void startEndFade()
        {
            if (fade < 0) { fade = (int) (0.01f * sr) + 1; fadeLen = fade; }
        }

        void stop()
        {
            active = false;
            use.set (nullptr);
        }

        float sr = 48000.0f;
        bool active = false, released = false, filterOn = false;
        int fade = -1, fadeLen = 1, controlCounter = 0, numClassic = 1;
        uint64_t age = 0;
        uint32_t serial = 0;
        VoiceStart st;
        SampleUse use;
        EngineType engine = EngineType::Classic;
        Random rng;
        float randomSource = 0.0f, pressure = 0.0f, slide = 0.0f, glideOffset = 0.0f, retune = 0.0f, timeSinceStart = 0.0f;
        double ratio = 1.0;
        float detune = 0.0f, targetGain = 0.0f, gain = 0.0f, pan = 0.0f, drive = 0.0f;
        std::array<float, P::kNumVoiceParams> base {}, current {};
        std::array<float, P::kNumModSources> sources {};
        std::array<Lfo, P::kNumLfos> lfos;
        std::array<MsegPlayer, kNumMsegs> msegs;
        std::array<ClassicPlayer, kMaxUnison> classic;
        GranularPlayer granular;
        SpectralPlayer spectral;
        VocalPlayer vocal;
        VoiceFilter filter;
        UnisonSpread unisonFx;
        Adsr amp, fenv;
        std::array<float, kControlBlock> bufL {}, bufR {}, tmpL {}, tmpR {};
    };
}
