#pragma once
#include "Voice.h"

namespace huldra::dsp
{
    // A note event for the synth (from MIDI, the on-screen keyboard or the motion engine).
    struct NoteEvent
    {
        enum Type { On, Off, AllOff };
        int sample = 0;
        Type type = On;
        int note = 60;
        float velocity = 0.8f;
        int channel = 1;
    };

    // Live values for the displays (written by the audio thread, read by the UI).
    struct VoiceDisplay
    {
        static constexpr int kSlots = 16;
        std::array<std::atomic<float>, kSlots> playhead {}, level {};
        std::array<std::atomic<int>, kSlots> note {};
        std::array<std::atomic<const void*>, kSlots> sample {};
        static constexpr int kGrains = 96;
        std::array<std::atomic<float>, kGrains> grainPos {}, grainAge {}, grainPitch {}, grainPan {};
        std::atomic<int> numGrains { 0 };
        std::array<std::atomic<float>, P::kNumModSources> sources {};
        std::atomic<int> activeVoices { 0 };
        std::atomic<uint32_t> noteCounter { 0 };
        std::atomic<float> lastVelocity { 0.0f };
    };

    class Synth
    {
    public:
        static constexpr int kVoicePool = 40;   // polyphony (max 32) + room for voices that fade out

        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            for (auto& v : voices) v.prepare (sampleRate, maxBlock);
            for (int i = 0; i < P::kNumLfos; ++i) globalLfos[(size_t) i].reset (0.0f, 777u + (uint32_t) i);
            lastNote = -1;
            heldCount = 0;
            held.fill (false);
            sustainPedal = false;
            for (auto& c : channels) c = {};
        }

        void stopAll()
        {
            for (auto& v : voices) if (v.isActive()) v.kill();
            held.fill (false);
            heldCount = 0;
        }

        // ---- Expression (per MIDI channel, MPE friendly) ----
        void handleExpression (const juce::MidiMessage& m)
        {
            const int ch = std::clamp (m.getChannel(), 1, 16) - 1;
            auto& c = channels[(size_t) ch];
            if (m.isPitchWheel()) c.bend = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
            else if (m.isChannelPressure()) c.pressure = (float) m.getChannelPressureValue() / 127.0f;
            else if (m.isAftertouch()) polyPressure[(size_t) (m.getNoteNumber() & 127)] = (float) m.getAfterTouchValue() / 127.0f;
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();
                const float value = (float) m.getControllerValue() / 127.0f;
                if (cc == 1) modWheel = value;
                else if (cc == 74) c.slide = value;
                else if (cc == 64)
                {
                    sustainPedal = value >= 0.5f;
                    if (! sustainPedal)
                        for (auto& v : voices)
                            if (v.isActive() && ! held[(size_t) v.getNote()] && ! v.isReleased()) v.release();
                }
            }
        }

        // ---- Rendering ----
        // events sorted by sample position
        void process (float* outL, float* outR, int n, const P::Values& v, const Snapshot& snap,
                      const NoteEvent* events, int numEvents, double bpm, double ppq, bool playing,
                      float motionLane, VoiceDisplay* display)
        {
            routing.prepare (v);
            updateGlobalSources (v, bpm, ppq, playing, n, motionLane);

            VoiceContext ctx;
            ctx.values = &v;
            ctx.snap = &snap;
            ctx.routing = &routing;
            ctx.globalSources = globalSources.data();
            ctx.globalLfo = globalLfoValues;
            ctx.bpm = bpm;
            ctx.hostRate = (float) sr;

            // Per-note expression
            for (auto& voice : voices)
                if (voice.isActive())
                {
                    const auto& c = channels[(size_t) std::clamp (voice.getChannel() - 1, 0, 15)];
                    voice.setExpression (std::max (c.pressure, polyPressure[(size_t) (voice.getNote() & 127)]), c.slide);
                }
            const auto& master = channels[0];
            juce::ignoreUnused (master);

            int pos = 0, e = 0;
            while (pos < n)
            {
                while (e < numEvents && events[e].sample <= pos)
                    handleEvent (events[e++], v, snap, ctx);
                const int next = e < numEvents ? std::min (n, std::max (pos + 1, events[e].sample)) : n;
                for (auto& voice : voices)
                    if (voice.isActive())
                        voice.render (outL + pos, outR + pos, next - pos, ctx);
                pos = next;
            }
            while (e < numEvents) handleEvent (events[e++], v, snap, ctx);

            if (display != nullptr) publishDisplay (*display);
        }

        // Values with global modulation applied (effects, motion)
        void applyGlobalModulation (P::Values& v) const
        {
            applyModulation (routing, globalSources.data(), P::VoiceLast + 1, P::Count - 1,
                             [&v] (P::Id id) { return v[id]; },
                             [&v] (P::Id id, float x) { v[id] = x; });
        }

        int activeVoiceCount() const
        {
            int n = 0;
            for (const auto& v : voices) n += v.isActive() && ! v.isFading() ? 1 : 0;
            return n;
        }

        float getGlobalSource (P::ModSource s) const { return globalSources[(size_t) s]; }

    private:
        struct Channel { float bend = 0.0f, pressure = 0.0f, slide = 0.0f; };

        void updateGlobalSources (const P::Values& v, double bpm, double ppq, bool playing, int n, float motionLane)
        {
            const float dt = (float) n / (float) sr;
            for (int i = 0; i < P::kNumLfos; ++i)
            {
                auto& lfo = globalLfos[(size_t) i];
                if (v.b (P::lfoParam (i, 2)) && playing)
                    lfo.setPhase ((float) std::fmod (ppq / lfoBeats (v.i (P::lfoParam (i, 3))), 1.0));
                else
                    lfo.advance (lfoRate (v, i, bpm) * dt);
                globalLfoValues[(size_t) i] = lfo.value (v.i (P::lfoParam (i, 0)));
            }

            auto& g = globalSources;
            g.fill (0.0f);
            g[(size_t) P::ModSource::ModWheel] = modWheel;
            g[(size_t) P::ModSource::PitchBend] = channels[0].bend;
            for (int m = 0; m < P::kNumMacros; ++m)
                g[(size_t) P::ModSource::Macro1 + (size_t) m] = v[P::macro (m)];
            g[(size_t) P::ModSource::MotionLane] = motionLane;
            g[(size_t) P::ModSource::PadX] = v[P::PadX];
            g[(size_t) P::ModSource::PadY] = v[P::PadY];
            for (int i = 0; i < P::kNumLfos; ++i)
                g[(size_t) P::ModSource::Lfo1 + (size_t) i] = globalLfoValues[(size_t) i];

            // Per-note sources for global targets (effects): taken from the newest voice
            const Voice* newest = nullptr;
            for (const auto& voice : voices)
                if (voice.isActive() && ! voice.isFading() && (newest == nullptr || voice.getAge() < newest->getAge()))
                    newest = &voice;
            if (newest != nullptr)
                for (auto s : { P::ModSource::Velocity, P::ModSource::Key, P::ModSource::Random, P::ModSource::Pressure, P::ModSource::Slide,
                                P::ModSource::AmpEnv, P::ModSource::FilterEnv, P::ModSource::Mseg1, P::ModSource::Mseg2, P::ModSource::Mseg3 })
                    g[(size_t) s] = newest->getSource (s);
            else
                g[(size_t) P::ModSource::Velocity] = lastVelocity;
        }

        void handleEvent (const NoteEvent& ev, const P::Values& v, const Snapshot& snap, const VoiceContext& ctx)
        {
            if (ev.type == NoteEvent::AllOff)
            {
                for (auto& voice : voices) if (voice.isActive()) voice.release();
                held.fill (false);
                heldCount = 0;
                return;
            }
            const int note = std::clamp (ev.note, 0, 127);
            if (ev.type == NoteEvent::Off)
            {
                if (held[(size_t) note]) { held[(size_t) note] = false; --heldCount; }
                const int mode = v.i (P::VoiceMode);
                if (mode != 0 && heldCount > 0)
                {
                    // Mono: fall back to the last still-held note
                    for (int k = (int) noteStack.size(); --k >= 0;)
                        if (held[(size_t) noteStack[(size_t) k]] && noteStack[(size_t) k] != note)
                        {
                            for (auto& voice : voices)
                                if (voice.isActive() && ! voice.isFading() && voice.getNote() == note)
                                    voice.retrigger (noteStack[(size_t) k], voice.getVelocity(), (float) note);
                            return;
                        }
                }
                if (! sustainPedal)
                    for (auto& voice : voices)
                        if (voice.isActive() && voice.getNote() == note && voice.getChannel() == ev.channel)
                            voice.release();
                return;
            }

            // Note on
            if (! held[(size_t) note]) { held[(size_t) note] = true; ++heldCount; }
            pushNoteStack (note);
            lastVelocity = ev.velocity;
            ++noteCounter;
            const auto& key = snap.keys[(size_t) note];
            if (key.mute) return;

            const int mode = v.i (P::VoiceMode);
            const float glideFrom = (v[P::Glide] > 0.001f && lastNote >= 0) ? (float) lastNote : -1.0f;

            if (mode == 2)
            {
                // Legato: keep the running voice and only move the pitch
                for (auto& voice : voices)
                    if (voice.isActive() && ! voice.isFading() && ! voice.isReleased())
                    {
                        voice.retrigger (note, ev.velocity, (float) voice.getNote());
                        lastNote = note;
                        return;
                    }
            }
            if (mode != 0)
                for (auto& voice : voices) if (voice.isActive()) voice.kill();

            const bool chops = v.i (P::PlayMode) == 1;
            const int globalEngine = v.i (P::Engine);
            if (chops)
            {
                if (snap.chopSample == nullptr || snap.slices.empty()) return;
                int index = key.slice >= 0 ? key.slice : note - v.i (P::ChopBase);
                if (! juce::isPositiveAndBelow (index, (int) snap.slices.size())) return;
                const auto& slice = snap.slices[(size_t) index];
                if (v.b (P::ChopChoke))
                    for (auto& voice : voices)
                        if (voice.isActive() && voice.isChop()) voice.kill();

                VoiceStart vs;
                vs.region = makeRegion (*snap.chopSample, slice.start, slice.end, v, key);
                vs.keyOffset = 0.0f;
                vs.isChop = true;
                vs.oneShot = v.i (P::ChopPlay) == 0;
                vs.engine = key.engine >= 0 ? key.engine : globalEngine;
                startVoice (vs, ev, key, glideFrom, v, ctx);
            }
            else
            {
                for (const auto& z : snap.zones)
                {
                    if (note < z.lo || note > z.hi) continue;
                    const int vel = juce::roundToInt (ev.velocity * 127.0f);
                    if (vel < z.vlo || vel > z.vhi) continue;
                    VoiceStart vs;
                    vs.region = makeRegion (*z.sample, 0, z.sample->numFrames, v, key);
                    vs.keyOffset = (float) note - z.root;
                    vs.zoneTune = z.tune;
                    vs.zoneGain = z.gain;
                    vs.zonePan = z.pan;
                    vs.engine = key.engine >= 0 ? key.engine : (z.engine >= 0 ? z.engine : globalEngine);
                    startVoice (vs, ev, key, glideFrom, v, ctx);
                }
            }
            lastNote = note;
        }

        PlayRegion makeRegion (const SampleData& s, int from, int to, const P::Values& v, const KeyEdit& key) const
        {
            PlayRegion r;
            r.s = &s;
            const int len = std::max (16, to - from);
            r.start = from;
            r.end = from + std::max (16, (int) (len * std::clamp (v[P::SmpEnd], 0.01f, 1.0f)));
            r.end = std::min (r.end, s.numFrames);
            const int rl = std::max (16, r.end - r.start);
            r.loopMode = v.i (P::SmpLoopMode);
            r.loopStart = r.start + (int) (rl * std::clamp (v[P::SmpLoopStart], 0.0f, 0.98f));
            r.loopEnd = std::min (r.end, r.loopStart + std::max (16, (int) (rl * v[P::SmpLoopLen])));
            r.xfade = (int) ((r.loopEnd - r.loopStart) * 0.5f * v[P::SmpXfade]);
            r.reverse = v.b (P::SmpReverse) != key.reverse;
            return r;
        }

        void startVoice (VoiceStart& vs, const NoteEvent& ev, const KeyEdit& key, float glideFrom, const P::Values& v, const VoiceContext& ctx)
        {
            vs.key = key;
            vs.note = ev.note;
            vs.channel = ev.channel;
            vs.velocity = ev.velocity;
            vs.glideFrom = glideFrom;
            vs.choke = key.choke;
            vs.seed = ++seedCounter * 2654435761u;
            Random r;
            r.seed (vs.seed);
            vs.startFrac = std::clamp (v[P::SmpStart] + key.start + v[P::SmpStartRand] * r.next01() * (1.0f - v[P::SmpStart]), 0.0f, 0.999f);

            if (vs.choke > 0)
                for (auto& voice : voices)
                    if (voice.isActive() && voice.getChoke() == vs.choke) voice.kill();

            auto* voice = findVoice (std::clamp (v.i (P::Polyphony), 1, 32));
            if (voice != nullptr)
                voice->start (vs, ctx);
        }

        Voice* findVoice (int polyphony)
        {
            int playing = 0;
            for (auto& v : voices) playing += v.isActive() && ! v.isFading() ? 1 : 0;
            if (playing >= polyphony)
            {
                // Steal: the oldest released voice, otherwise the oldest
                Voice* victim = nullptr;
                for (auto& v : voices)
                    if (v.isActive() && ! v.isFading() && v.isReleased() && (victim == nullptr || v.getAge() > victim->getAge()))
                        victim = &v;
                if (victim == nullptr)
                    for (auto& v : voices)
                        if (v.isActive() && ! v.isFading() && (victim == nullptr || v.getAge() > victim->getAge()))
                            victim = &v;
                if (victim != nullptr) victim->kill();
            }
            for (auto& v : voices)
                if (! v.isActive()) return &v;
            // Pool exhausted (many fading voices): reuse the quietest
            Voice* quietest = nullptr;
            for (auto& v : voices)
                if (quietest == nullptr || v.getLevel() < quietest->getLevel()) quietest = &v;
            return quietest;
        }

        void pushNoteStack (int note)
        {
            auto it = std::find (noteStack.begin(), noteStack.begin() + stackSize, note);
            if (it != noteStack.begin() + stackSize)
            {
                std::rotate (it, it + 1, noteStack.begin() + stackSize);
                noteStack[(size_t) stackSize - 1] = note;
                return;
            }
            if (stackSize < (int) noteStack.size()) noteStack[(size_t) stackSize++] = note;
            else { std::rotate (noteStack.begin(), noteStack.begin() + 1, noteStack.end()); noteStack.back() = note; }
        }

        void publishDisplay (VoiceDisplay& d)
        {
            int slot = 0, grains = 0;
            for (const auto& v : voices)
            {
                if (! v.isActive() || slot >= VoiceDisplay::kSlots) continue;
                d.playhead[(size_t) slot].store (v.playhead(), std::memory_order_relaxed);
                d.level[(size_t) slot].store (v.getLevel(), std::memory_order_relaxed);
                d.note[(size_t) slot].store (v.getNote(), std::memory_order_relaxed);
                d.sample[(size_t) slot].store (v.getSample(), std::memory_order_relaxed);
                if (v.getEngine() == EngineType::Granular)
                    v.getGranular().forEachGrain ([&] (float p, float a, float pitch, float panPos)
                    {
                        if (grains >= VoiceDisplay::kGrains) return;
                        const auto& region = v.getRegion();
                        const float abs = region.s != nullptr ? (float) (region.start + p * region.length()) / (float) region.s->numFrames : p;
                        d.grainPos[(size_t) grains].store (abs, std::memory_order_relaxed);
                        d.grainAge[(size_t) grains].store (a, std::memory_order_relaxed);
                        d.grainPitch[(size_t) grains].store (pitch, std::memory_order_relaxed);
                        d.grainPan[(size_t) grains].store (panPos, std::memory_order_relaxed);
                        ++grains;
                    });
                ++slot;
            }
            for (int s = slot; s < VoiceDisplay::kSlots; ++s)
                d.level[(size_t) s].store (-1.0f, std::memory_order_relaxed);
            d.numGrains.store (grains, std::memory_order_relaxed);
            d.activeVoices.store (activeVoiceCount(), std::memory_order_relaxed);
            for (size_t i = 0; i < globalSources.size(); ++i)
                d.sources[i].store (globalSources[i], std::memory_order_relaxed);
            d.noteCounter.store (noteCounter, std::memory_order_relaxed);
            d.lastVelocity.store (lastVelocity, std::memory_order_relaxed);
        }

        double sr = 48000.0;
        std::array<Voice, kVoicePool> voices;
        ModRouting routing;
        std::array<float, P::kNumModSources> globalSources {};
        std::array<Lfo, P::kNumLfos> globalLfos;
        std::array<float, P::kNumLfos> globalLfoValues {};
        std::array<Channel, 16> channels {};
        std::array<float, 128> polyPressure {};
        std::array<bool, 128> held {};
        std::array<int, 16> noteStack {};
        int stackSize = 0, heldCount = 0, lastNote = -1;
        float modWheel = 0.0f, lastVelocity = 0.0f;
        bool sustainPedal = false;
        uint32_t seedCounter = 0, noteCounter = 0;
    };
}
