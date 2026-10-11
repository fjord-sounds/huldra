#pragma once
#include "Pattern.h"
#include "Scale.h"
#include "../dsp/Synth.h"

namespace huldra::motion
{
    namespace P = params;
    using dsp::NoteEvent;

    // Step length in quarter notes ("mot_rate" order)
    inline double stepBeats (int index)
    {
        static const double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 2.0, 4.0 };
        return beats[std::clamp (index, 0, 7)];
    }

    // Between the incoming notes and the synth: scale snap, chords (with strum), and the motion modes
    // (arpeggiator, step pattern, generative autoplay). Runs on the audio thread, never allocates.
    class MotionEngine
    {
    public:
        static constexpr int kMaxOut = 512;

        struct Clock
        {
            double ppq = 0.0, beatsPerSample = 0.0;
            bool hostPlaying = false;
        };

        void prepare (double sampleRate) { sr = sampleRate; reset(); }

        void reset()
        {
            physical.fill (false);
            for (auto& c : chordOf) c.size = 0;
            numPending = 0;
            numHeld = 0;
            latched = 0;
            freePpq = 0.0;
            lastStep = -1;
            running = false;
            numActive = 0;
            lane = 0.0f;
            sampleClock = 0;
            walk = 0;
        }

        // in: note events of this block (sorted). out: events for the synth (sorted). Returns the count.
        int process (const NoteEvent* in, int numIn, NoteEvent* out, int n, const P::Values& v, const Clock& clock,
                     const Pattern& pattern, bool chopsMode)
        {
            numOut = 0;
            outBuf = out;
            const int mode = v.i (P::MotMode);
            const bool autoplay = v.b (P::MotFree);
            if (mode != lastMode) { flushActive (0); lastMode = mode; if (mode != 0) numHeld = 0; }

            // 1) incoming notes: scale snap and chords, then either straight out or into the held list
            for (int i = 0; i < numIn; ++i)
            {
                const auto& e = in[i];
                if (e.type == NoteEvent::AllOff) { emit (e); flushActive (e.sample); numHeld = 0; physical.fill (false); continue; }
                const int src = std::clamp (e.note, 0, 127);
                if (e.type == NoteEvent::On)
                {
                    physical[(size_t) src] = true;
                    int base = src;
                    if (v.b (P::SclOn) && ! chopsMode) base = snapNote (src, v.i (P::SclRoot), v.i (P::SclType));
                    auto chord = chordFor (v.i (P::ChordType), base, v.i (P::SclRoot), v.i (P::SclType));
                    voiceChord (chord, v.i (P::ChordInv), v[P::ChordSpread]);
                    auto& stored = chordOf[(size_t) src];
                    stored.size = 0;
                    for (int k = 0; k < chord.size; ++k)
                    {
                        const int note = std::clamp (base + chord.notes[(size_t) k], 0, 127);
                        stored.notes[(size_t) stored.size++] = note;
                        if (mode == 0)
                        {
                            const int delay = (int) (v[P::ChordStrum] * 0.001 * sr * k);
                            schedule ({ e.sample + delay, NoteEvent::On, note, e.velocity, e.channel });
                        }
                        else
                            addHeld (note, e.velocity, src);
                    }
                    if (mode != 0 && ! running) startFreeClock();
                }
                else
                {
                    physical[(size_t) src] = false;
                    auto& stored = chordOf[(size_t) src];
                    for (int k = 0; k < stored.size; ++k)
                    {
                        if (mode == 0) schedule ({ e.sample, NoteEvent::Off, stored.notes[(size_t) k], 0.0f, e.channel });
                        else if (! v.b (P::MotLatch)) removeHeld (stored.notes[(size_t) k]);
                    }
                    stored.size = 0;
                }
            }

            // Latch: a new chord replaces the latched one when all keys were up
            if (mode != 0 && v.b (P::MotLatch))
            {
                bool any = false;
                for (bool b : physical) any |= b;
                latchArmed = ! any;
            }

            // 2) motion steps inside this block
            if (mode != 0)
            {
                const bool haveNotes = numHeld > 0 || autoplay;
                if (! haveNotes)
                {
                    running = false;
                    flushActive (0);
                }
                else
                {
                    if (! running) startFreeClock();
                    runSteps (n, v, clock, pattern, mode, chopsMode);
                }
            }

            // 3) due note-offs and strummed notes
            releasePending (n);
            sampleClock += n;

            std::stable_sort (out, out + numOut, [] (const NoteEvent& a, const NoteEvent& b) { return a.sample < b.sample; });
            return numOut;
        }

        int getCurrentStep() const { return lastStep; }
        float getLane() const { return lane; }

        // Recently played notes (for the constellation display): note, velocity, time in samples
        struct Played { int note = 0; float velocity = 0.0f; int64_t time = 0; };
        static constexpr int kHistory = 64;
        std::array<Played, kHistory> history {};
        std::atomic<int> historyWrite { 0 };
        int64_t now() const { return sampleClock; }

    private:
        struct Held { int note; float velocity; int source; };
        struct Pending { int64_t time; NoteEvent e; };
        struct ChordNotes { int size = 0; std::array<int, 6> notes {}; };

        void emit (const NoteEvent& e)
        {
            if (numOut < kMaxOut) outBuf[numOut++] = e;
        }

        // Absolute-time queue for strums and note-offs
        void schedule (NoteEvent e)
        {
            if (numPending >= (int) pending.size()) { emit (e); return; }
            pending[(size_t) numPending++] = { sampleClock + e.sample, e };
        }

        void releasePending (int n)
        {
            for (int i = 0; i < numPending;)
            {
                const auto& p = pending[(size_t) i];
                if (p.time < sampleClock + n)
                {
                    auto e = p.e;
                    e.sample = (int) std::max<int64_t> (0, p.time - sampleClock);
                    emit (e);
                    if (e.type == NoteEvent::On) noteHistory (e.note, e.velocity, sampleClock + e.sample);
                    if (e.type == NoteEvent::Off) forgetActive (e.note);
                    pending[(size_t) i] = pending[(size_t) --numPending];
                }
                else ++i;
            }
        }

        void noteHistory (int note, float velocity, int64_t time)
        {
            const int w = historyWrite.load (std::memory_order_relaxed);
            history[(size_t) w] = { note, velocity, time };
            historyWrite.store ((w + 1) % kHistory, std::memory_order_release);
        }

        void addHeld (int note, float velocity, int source)
        {
            if (latchArmed && latched > 0) { numHeld = 0; latched = 0; }
            latchArmed = false;
            for (int i = 0; i < numHeld; ++i) if (held[(size_t) i].note == note) return;
            if (numHeld < (int) held.size()) { held[(size_t) numHeld++] = { note, velocity, source }; ++latched; }
        }

        void removeHeld (int note)
        {
            for (int i = 0; i < numHeld; ++i)
                if (held[(size_t) i].note == note)
                {
                    for (int j = i; j + 1 < numHeld; ++j) held[(size_t) j] = held[(size_t) j + 1];
                    --numHeld;
                    return;
                }
        }

        void startFreeClock()
        {
            running = true;
            freePpq = 0.0;
            lastStep = -1;
            arpIndex = 0;
            arpDir = 1;
        }

        void playNote (int at, int note, float velocity, int lengthSamples)
        {
            note = std::clamp (note, 0, 127);
            emit ({ at, NoteEvent::On, note, std::clamp (velocity, 0.05f, 1.0f), 1 });
            noteHistory (note, velocity, sampleClock + at);
            if (numActive < (int) active.size()) active[(size_t) numActive++] = note;
            schedule ({ at + std::max (8, lengthSamples), NoteEvent::Off, note, 0.0f, 1 });
        }

        void forgetActive (int note)
        {
            for (int i = 0; i < numActive; ++i)
                if (active[(size_t) i] == note) { active[(size_t) i] = active[(size_t) --numActive]; return; }
        }

        void flushActive (int at)
        {
            // Release everything the motion engine started and drop pending events
            for (int i = 0; i < numPending;)
            {
                if (pending[(size_t) i].e.type == NoteEvent::Off)
                {
                    auto e = pending[(size_t) i].e;
                    e.sample = at;
                    emit (e);
                }
                pending[(size_t) i] = pending[(size_t) --numPending];
            }
            numActive = 0;
        }

        void runSteps (int n, const P::Values& v, const Clock& clock, const Pattern& pattern, int mode, bool chopsMode)
        {
            const double len = stepBeats (v.i (P::MotRate));
            const double bps = clock.beatsPerSample > 0.0 ? clock.beatsPerSample : v[P::MotBpm] / 60.0 / sr;
            const double startPpq = clock.hostPlaying ? clock.ppq : freePpq;
            const double endPpq = startPpq + bps * n;
            if (! clock.hostPlaying) freePpq = endPpq;
            const float swing = v[P::MotSwing];

            // Step boundaries in [startPpq, endPpq)
            long long first = (long long) std::floor (startPpq / len);
            for (long long step = first; ; ++step)
            {
                double at = (double) step * len;
                if (step % 2 == 1) at += swing * len * 0.5;
                if (at >= endPpq) break;
                if (at < startPpq) continue;
                if (step == lastStep) continue;
                lastStep = (int) step;
                const int offset = std::clamp ((int) ((at - startPpq) / bps), 0, n - 1);
                const int stepSamples = (int) (len / bps);
                doStep (step, offset, stepSamples, v, pattern, mode, chopsMode);
            }
        }

        int baseNote (const P::Values& v, bool chopsMode) const
        {
            if (numHeld > 0)
            {
                int lowest = 127;
                for (int i = 0; i < numHeld; ++i) lowest = std::min (lowest, held[(size_t) i].note);
                return lowest;
            }
            return chopsMode ? v.i (P::ChopBase) : 48 + v.i (P::SclRoot);
        }

        void doStep (long long step, int offset, int stepSamples, const P::Values& v, const Pattern& pattern, int mode, bool chopsMode)
        {
            const float gate = v[P::MotGate];
            if (mode == 1)   // Arpeggiator
            {
                if (numHeld == 0 && ! v.b (P::MotFree)) return;
                std::array<int, 64> notes {};
                int count = 0;
                if (numHeld == 0) notes[(size_t) count++] = baseNote (v, chopsMode);
                for (int i = 0; i < numHeld; ++i) notes[(size_t) count++] = held[(size_t) i].note;
                const int arpMode = v.i (P::ArpMode);
                if (arpMode != 4) std::sort (notes.begin(), notes.begin() + count);
                const int octaves = std::clamp (v.i (P::ArpOct), 1, 4);
                std::array<int, 64> seqNotes {};
                int total = 0;
                for (int o = 0; o < octaves; ++o)
                    for (int i = 0; i < count && total < 64; ++i)
                        seqNotes[(size_t) total++] = notes[(size_t) i] + 12 * o;
                if (total == 0) return;
                const float vel = numHeld > 0 ? held[0].velocity : 0.8f;
                const int length = (int) (stepSamples * gate);

                if (arpMode == 5)   // Chord: all notes on every step
                {
                    for (int i = 0; i < count; ++i) playNote (offset, notes[(size_t) i], vel, length);
                    return;
                }
                int index = 0;
                switch (arpMode)
                {
                    case 0: index = arpIndex % total; ++arpIndex; break;
                    case 1: index = total - 1 - (arpIndex % total); ++arpIndex; break;
                    case 2:
                    {
                        const int period = std::max (1, total * 2 - 2);
                        const int k = arpIndex % period;
                        index = k < total ? k : period - k;
                        ++arpIndex;
                        break;
                    }
                    case 3: index = (int) (rng.next01() * (float) total) % total; break;
                    case 4: index = arpIndex % total; ++arpIndex; break;
                    default:   // Converge: outside in
                    {
                        const int k = arpIndex % total;
                        index = k % 2 == 0 ? k / 2 : total - 1 - k / 2;
                        ++arpIndex;
                        break;
                    }
                }
                playNote (offset, seqNotes[(size_t) index], vel, length);
                return;
            }

            if (mode == 2)   // Pattern
            {
                const int steps = std::clamp (v.i (P::PatLength), 1, kMaxSteps);
                const int idx = (int) (((step % steps) + steps) % steps);
                patternStep = idx;
                const auto& s = pattern.steps[(size_t) idx];
                lane = s.lane;
                if (! s.on || rng.next01() > s.chance) return;
                const int ratchets = std::clamp (s.ratchet, 1, 4);
                const int sub = stepSamples / ratchets;
                const float g = s.tie ? 1.05f : s.gate * gate;
                std::array<int, 16> bases {};
                int count = 0;
                if (numHeld == 0) bases[(size_t) count++] = baseNote (v, chopsMode);
                for (int i = 0; i < numHeld && count < 16; ++i) bases[(size_t) count++] = held[(size_t) i].note;
                for (int r = 0; r < ratchets; ++r)
                    for (int i = 0; i < count; ++i)
                    {
                        int note = bases[(size_t) i] + s.note;
                        if (v.b (P::SclOn) && ! chopsMode) note = snapNote (note, v.i (P::SclRoot), v.i (P::SclType));
                        const float vel = s.velocity * (numHeld > 0 ? 0.4f + 0.6f * held[0].velocity : 1.0f);
                        playNote (offset + r * sub, note, vel, (int) ((float) (ratchets > 1 ? sub : stepSamples) * g));
                    }
                return;
            }

            // Generative: a random walk through the scale around the base note
            const float density = v[P::GenDensity];
            const float evolve = v[P::GenEvolve];
            // Same seed + same step gives the same choice, so a given seed always plays the same phrase
            dsp::Random r;
            r.seed ((uint32_t) (v.i (P::GenSeed) * 7919 + (int) (step % 4096) * 104729 + 13));
            if (evolve > 0.001f) { r.nextInt(); if (rng.next01() < evolve) r.seed (rng.nextInt()); }
            if (r.next01() > density) { lane = lane * 0.9f; return; }

            const int root = v.i (P::SclRoot);
            const int type = v.b (P::SclOn) ? v.i (P::SclType) : (chopsMode ? 11 : v.i (P::SclType));
            const int range = std::max (1, v.i (P::GenRange) * (chopsMode ? 4 : 7));
            const int poly = std::clamp (v.i (P::GenPoly), 1, 4);
            const int base = baseNote (v, chopsMode);
            const int length = (int) ((float) stepSamples * v[P::GenLength] * std::max (0.2f, gate));
            for (int k = 0; k < poly; ++k)
            {
                const int move = (int) std::lround (r.nextBipolar() * 2.5f);
                walk = std::clamp (walk + move, 0, range);
                int note;
                if (chopsMode) note = base + walk + k * 3;
                else note = scaleStep (snapNote (base, root, type), walk + k * 2, root, type);
                const float vel = std::clamp (0.75f + v[P::GenVelRand] * 0.5f * r.nextBipolar(), 0.1f, 1.0f);
                playNote (offset + k * (int) (sr * 0.012), note, vel, length);
            }
            lane = (float) walk / (float) range;
        }

        double sr = 48000.0, freePpq = 0.0;
        std::array<bool, 128> physical {};
        std::array<ChordNotes, 128> chordOf {};
        std::array<Held, 32> held {};
        int numHeld = 0, latched = 0;
        bool latchArmed = false, running = false;
        std::array<Pending, 1024> pending {};
        int numPending = 0;
        std::array<int, 128> active {};
        int numActive = 0;
        NoteEvent* outBuf = nullptr;
        int numOut = 0, lastMode = 0, lastStep = -1, arpIndex = 0, arpDir = 1, patternStep = 0, walk = 0;
        float lane = 0.0f;
        int64_t sampleClock = 0;
        dsp::Random rng;
    };
}
