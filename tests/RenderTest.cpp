// Offline test and tool: renders Huldra without a DAW and checks the output (NaN, clipping, silence, every engine,
// chops, motion, analysis on synthetic signals, project round trips with embedded audio, odd block sizes, presets).
// Also makes screenshots, loudness data, the parameter table for the preset generator and the icon.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace P = huldra::params;

namespace
{
    double testSampleRate = 48000.0;
    int failures = 0;

    void set (juce::AudioProcessorValueTreeState& s, P::Id id, float value)
    {
        auto* p = s.getParameter (P::paramId (id));
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void check (bool ok, const juce::String& what)
    {
        if (! ok) { ++failures; printf ("  FAIL: %s\n", what.toRawUTF8()); }
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            return info;
        }
    };

    struct Stats
    {
        float peak = 0.0f;
        double rms = 0.0, tailRms = 0.0;
        bool finite = true;
        double cpu = 0.0;
        juce::AudioBuffer<float> audio;
    };

    struct Note { double start, length; int note; float velocity = 0.8f; };

    // Renders 'seconds' with the given notes; the last 0.5 s are measured as the tail.
    Stats render (HuldraProcessor& proc, const std::vector<Note>& notes, double seconds, int block = 512, FakePlayHead* head = nullptr)
    {
        const double sr = testSampleRate;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        if (head != nullptr) proc.setPlayHead (head);

        const int total = (int) (sr * seconds);
        Stats s;
        s.audio.setSize (2, total);
        juce::AudioBuffer<float> buf (2, block);
        double sumSq = 0.0, tailSq = 0.0, cpuSeconds = 0.0;
        long tailCount = 0;

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            juce::MidiBuffer midi;
            for (const auto& nt : notes)
            {
                const int on = (int) (nt.start * sr), off = (int) ((nt.start + nt.length) * sr);
                if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, nt.velocity), on - pos);
                if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
            }
            buf.setSize (2, n, false, false, true);
            buf.clear();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            proc.processBlock (buf, midi);
            cpuSeconds += (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            if (head != nullptr) head->ppq += head->bpm / 60.0 * n / sr;

            for (int ch = 0; ch < 2; ++ch)
            {
                s.audio.copyFrom (ch, pos, buf, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    s.finite &= std::isfinite (v);
                    s.peak = std::max (s.peak, std::abs (v));
                    sumSq += (double) v * v;
                    if (pos + i > total - (int) (sr * 0.5)) { tailSq += (double) v * v; ++tailCount; }
                }
            }
        }
        s.rms = std::sqrt (sumSq / (2.0 * total));
        s.tailRms = tailCount > 0 ? std::sqrt (tailSq / (double) tailCount) : 0.0;
        s.cpu = 100.0 * cpuSeconds / (total / sr);
        proc.setPlayHead (nullptr);
        return s;
    }

    void writeWav (const juce::AudioBuffer<float>& audio, const juce::File& file, double sr)
    {
        juce::WavAudioFormat wav;
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
    }

    // A fresh processor with the demo voices ready
    std::unique_ptr<HuldraProcessor> makeProcessor()
    {
        huldra::DemoVoices::get().waitUntilReady();
        auto p = std::make_unique<HuldraProcessor>();
        p->instrument.waitForLoads();
        return p;
    }

    // Synthetic sounds for the analysis tests
    huldra::SamplePtr sineSample (float hz, double seconds, const juce::String& name)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * 48000.0));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float t = (float) i / 48000.0f;
            const float env = std::min (1.0f, t / 0.02f) * std::min (1.0f, ((float) seconds - t) / 0.05f);
            const float v = 0.5f * env * (std::sin (juce::MathConstants<float>::twoPi * hz * t) + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 2.0f * hz * t));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return huldra::InstrumentState::fromBuffer (b, 48000.0, name);
    }

    huldra::SamplePtr clickTrain (int count, double gap)
    {
        juce::AudioBuffer<float> b (2, (int) ((count + 0.5) * gap * 48000.0));
        b.clear();
        juce::Random rnd (3);
        for (int k = 0; k < count; ++k)
        {
            const int at = (int) ((k + 0.25) * gap * 48000.0);
            for (int i = 0; i < 4000 && at + i < b.getNumSamples(); ++i)
            {
                const float v = (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 600.0f) * 0.8f;
                b.setSample (0, at + i, v);
                b.setSample (1, at + i, v);
            }
        }
        return huldra::InstrumentState::fromBuffer (b, 48000.0, "Clicks");
    }

    // ---------------------------------------------------------------------------------------------

    void engineTests (const juce::File& outDir, bool writeFiles)
    {
        printf ("Engines\n");
        static const char* names[] = { "classic", "granular", "spectral", "vocal" };
        for (int engine = 0; engine < 4; ++engine)
            for (int demo : { 0, 6 })
            {
                auto proc = makeProcessor();
                set (proc->apvts, P::DemoVoice, (float) demo);
                proc->instrument.rebuildNow();
                set (proc->apvts, P::Engine, (float) engine);
                set (proc->apvts, P::SmpLoopMode, 1.0f);
                set (proc->apvts, P::AmpRelease, 0.3f);
                if (engine == 1) set (proc->apvts, P::GrnScan, 0.5f);
                const auto s = render (*proc, { { 0.05, 1.6, 60 }, { 0.6, 1.0, 67 }, { 0.6, 1.0, 72 } }, 3.0);
                const auto label = juce::String (names[engine]) + " / demo " + juce::String (demo);
                check (s.finite, label + ": NaN/Inf");
                check (s.rms > 0.005, label + ": too quiet (" + juce::String (s.rms, 4) + ")");
                check (s.peak < 1.0f, label + ": clipping");
                check (s.tailRms < 0.01, label + ": does not stop (" + juce::String (s.tailRms, 4) + ")");
                printf ("  %-22s rms %.3f peak %.3f cpu %.1f %%\n", label.toRawUTF8(), s.rms, s.peak, s.cpu);
                if (writeFiles) writeWav (s.audio, outDir.getChildFile (label.replace (" / ", "_").replace (" ", "_") + ".wav"), testSampleRate);
            }
    }

    // Every engine must play the note that is pressed: the output is measured with Huldra's own pitch tracker.
    void pitchAccuracyTests()
    {
        printf ("Pitch accuracy\n");
        static const char* names[] = { "classic", "granular", "spectral", "vocal" };
        for (int engine = 0; engine < 4; ++engine)
            for (int note : { 48, 60, 67, 79 })
            {
                auto proc = makeProcessor();
                set (proc->apvts, P::Engine, (float) engine);
                set (proc->apvts, P::SmpLoopMode, 1.0f);
                set (proc->apvts, P::SmpStart, 0.2f);
                set (proc->apvts, P::GrnPitchRand, 0.0f);
                set (proc->apvts, P::GrnPos, 0.4f);
                set (proc->apvts, P::SpcPos, 0.3f);
                set (proc->apvts, P::LimOn, 0.0f);
                const auto s = render (*proc, { { 0.0, 1.5, note } }, 1.5);
                juce::AudioBuffer<float> part (2, (int) (testSampleRate * 1.0));
                for (int ch = 0; ch < 2; ++ch) part.copyFrom (ch, 0, s.audio, ch, (int) (testSampleRate * 0.3), part.getNumSamples());
                auto measured = huldra::InstrumentState::fromBuffer (part, testSampleRate, "measure");
                const float err = measured->rootNote - (float) note;
                check (std::abs (err) < 0.35f, juce::String (names[engine]) + " plays note " + juce::String (note) + " at "
                                               + juce::String (measured->rootNote, 2));
                printf ("  %-9s note %d -> %.2f\n", names[engine], note, measured->rootNote);
            }
    }

    // Sixteen notes at once on every engine: the CPU load must stay reasonable (one core)
    void cpuTests()
    {
        printf ("CPU (16 voices)\n");
        static const char* names[] = { "classic x4 unison", "granular", "spectral", "vocal" };
        for (int engine = 0; engine < 4; ++engine)
        {
            auto proc = makeProcessor();
            set (proc->apvts, P::Engine, (float) engine);
            set (proc->apvts, P::SmpLoopMode, 1.0f);
            set (proc->apvts, P::Polyphony, 16.0f);
            set (proc->apvts, P::GrnDensity, 60.0f);
            set (proc->apvts, P::VerbOn, 1.0f);
            set (proc->apvts, P::DlyOn, 1.0f);
            if (engine == 0) set (proc->apvts, P::UniVoices, 4.0f);
            std::vector<Note> notes;
            for (int i = 0; i < 16; ++i) notes.push_back ({ 0.01 * i, 2.5, 48 + i * 2 });
            const auto s = render (*proc, notes, 3.0);
            printf ("  %-18s %5.1f %%\n", names[engine], s.cpu);
            check (s.finite && s.peak < 1.0f, juce::String (names[engine]) + ": 16 voices broken");
            check (s.cpu < 60.0, juce::String (names[engine]) + ": too much CPU (" + juce::String (s.cpu, 1) + " %)");
        }
    }

    // Every parameter at random values (like a host automating everything): never NaN / Inf, never runaway levels.
    // When a trial fails, the parameters responsible are found by resetting them one by one.
    void fuzzTests (int trials)
    {
        printf ("Fuzz (%d random states)\n", trials);
        juce::Random rnd (12345);
        int bad = 0;
        for (int t = 0; t < trials && bad < 3; ++t)
        {
            std::vector<float> state;
            for (int i = 0; i < P::Count; ++i) state.push_back (rnd.nextFloat());
            auto run = [&] (const std::vector<float>& st)
            {
                auto proc = makeProcessor();
                for (int i = 0; i < P::Count; ++i)
                    if (i != P::MasterGain && i != P::Polyphony)
                        proc->apvts.getParameter (P::specs()[(size_t) i].paramId)->setValueNotifyingHost (st[(size_t) i]);
                proc->instrument.rebuildNow();
                const auto r = render (*proc, { { 0.0, 0.4, 60 }, { 0.05, 0.3, 67 }, { 0.2, 0.3, 48 } }, 0.8, 128);
                return r.finite && r.peak < 4.0f;
            };
            if (run (state)) continue;
            ++bad;
            juce::StringArray culprits;
            for (int i = 0; i < P::Count; ++i)
            {
                auto st = state;
                st[(size_t) i] = P::toNormalised (P::specs()[(size_t) i], P::specs()[(size_t) i].def);
                if (run (st)) culprits.add (juce::String (P::specs()[(size_t) i].paramId) + "=" + juce::String (P::fromNormalised (P::specs()[(size_t) i], state[(size_t) i]), 3));
            }
            juce::String ctx;
            for (auto id : { P::Engine, P::PlayMode, P::SmpEnd, P::SmpLoopStart, P::SmpLoopLen, P::SmpReverse, P::SmpXfade })
                ctx << P::specs()[(size_t) id].paramId << "=" << juce::String (P::fromNormalised (P::specs()[(size_t) id], state[(size_t) id]), 3) << " ";
            check (false, "fuzz trial " + juce::String (t) + " broke the output; culprits: " + culprits.joinIntoString (", ") + "  [" + ctx + "]");
        }
    }

    void engineFeatureTests()
    {
        printf ("Engine features\n");
        struct Case { const char* name; std::function<void (HuldraProcessor&)> setup; };
        const std::vector<Case> cases {
            { "reverse ping-pong", [] (HuldraProcessor& p) { set (p.apvts, P::SmpReverse, 1); set (p.apvts, P::SmpLoopMode, 2); } },
            { "unison 4", [] (HuldraProcessor& p) { set (p.apvts, P::UniVoices, 4); set (p.apvts, P::UniDetune, 40); } },
            { "granular freeze", [] (HuldraProcessor& p) { set (p.apvts, P::Engine, 1); set (p.apvts, P::GrnFreeze, 1); set (p.apvts, P::GrnDensity, 120); set (p.apvts, P::GrnOctaves, 0.5f); } },
            { "granular reverse", [] (HuldraProcessor& p) { set (p.apvts, P::Engine, 1); set (p.apvts, P::GrnReverse, 1); set (p.apvts, P::GrnWindow, 4); } },
            { "spectral freeze blur", [] (HuldraProcessor& p) { set (p.apvts, P::Engine, 2); set (p.apvts, P::SpcFreeze, 1); set (p.apvts, P::SpcPos, 0.4f); set (p.apvts, P::SpcBlur, 0.9f); set (p.apvts, P::SpcSmear, 0.5f); } },
            { "spectral shift stretch", [] (HuldraProcessor& p) { set (p.apvts, P::Engine, 2); set (p.apvts, P::SpcShift, 300); set (p.apvts, P::SpcStretch, 1.3f); set (p.apvts, P::SpcHarm, 0.6f); set (p.apvts, P::SpcGate, 0.4f); } },
            { "vocal formant", [] (HuldraProcessor& p) { set (p.apvts, P::Engine, 3); set (p.apvts, P::VocFormant, 7); set (p.apvts, P::VocSpeed, 0.5f); } },
            { "vowel filter", [] (HuldraProcessor& p) { set (p.apvts, P::FltOn, 1); set (p.apvts, P::FltType, 5); set (p.apvts, P::FltVowel, 2.5f); } },
            { "comb filter", [] (HuldraProcessor& p) { set (p.apvts, P::FltOn, 1); set (p.apvts, P::FltType, 6); set (p.apvts, P::FltReso, 0.8f); } },
            { "ladder + env", [] (HuldraProcessor& p) { set (p.apvts, P::FltOn, 1); set (p.apvts, P::FltType, 7); set (p.apvts, P::FltEnv, 3); set (p.apvts, P::FltCutoff, 300); } },
            { "lfo to cutoff", [] (HuldraProcessor& p)
                { set (p.apvts, P::FltOn, 1); set (p.apvts, P::Mod1Src, 10); set (p.apvts, P::Mod1Amt, 0.5f);
                  const auto& t = P::modTargets(); set (p.apvts, P::Mod1Dst, (float) (std::find (t.begin(), t.end(), P::FltCutoff) - t.begin() + 1)); } },
            { "retune", [] (HuldraProcessor& p) { set (p.apvts, P::RetuneAmt, 1); set (p.apvts, P::SclOn, 1); set (p.apvts, P::SclType, 1); } },
            { "chord + strum", [] (HuldraProcessor& p) { set (p.apvts, P::ChordType, 6); set (p.apvts, P::ChordStrum, 60); } },
            { "mono glide", [] (HuldraProcessor& p) { set (p.apvts, P::VoiceMode, 2); set (p.apvts, P::Glide, 0.3f); } },
            { "all effects", [] (HuldraProcessor& p)
                { for (auto id : { P::SatOn, P::LofiOn, P::CompOn, P::SqOn, P::EqOn, P::ChrOn, P::DlyOn, P::VerbOn, P::WidthOn }) set (p.apvts, id, 1);
                  set (p.apvts, P::VerbMode, 3); set (p.apvts, P::DlyMode, 2); set (p.apvts, P::ChrMode, 1); set (p.apvts, P::VerbDecay, 1.0f); } },
            { "cloud reverb, tape delay", [] (HuldraProcessor& p)
                { set (p.apvts, P::VerbOn, 1); set (p.apvts, P::VerbMode, 4); set (p.apvts, P::DlyOn, 1); set (p.apvts, P::DlyMode, 1); set (p.apvts, P::VerbDecay, 1.2f); } },
        };
        for (const auto& c : cases)
        {
            auto proc = makeProcessor();
            set (proc->apvts, P::SmpLoopMode, 1.0f);
            set (proc->apvts, P::AmpRelease, 0.2f);
            c.setup (*proc);
            const auto s = render (*proc, { { 0.05, 1.0, 60 }, { 0.5, 1.0, 64 } }, 5.0);
            check (s.finite, juce::String (c.name) + ": NaN/Inf");
            check (s.rms > 0.003, juce::String (c.name) + ": silent");
            check (s.peak < 1.0f, juce::String (c.name) + ": clipping");
            check (s.tailRms < 0.02, juce::String (c.name) + ": does not stop (" + juce::String (s.tailRms, 4) + ")");
            printf ("  %-26s rms %.3f peak %.3f cpu %.1f %%\n", c.name, s.rms, s.peak, s.cpu);
        }
    }

    void analysisTests()
    {
        printf ("Analysis\n");
        for (float hz : { 110.0f, 220.0f, 261.63f, 440.0f, 880.0f })
        {
            auto s = sineSample (hz, 1.0, "sine");
            const float expected = 69.0f + 12.0f * std::log2 (hz / 440.0f);
            check (std::abs (s->rootNote - expected) < 0.3f, "pitch of " + juce::String (hz) + " Hz: " + juce::String (s->rootNote, 2)
                                                             + " instead of " + juce::String (expected, 2));
            check (s->pitchConfidence > 0.8f, "confidence of " + juce::String (hz) + " Hz");
        }
        auto clicks = clickTrain (8, 0.4);
        const auto onsets = huldra::analysis::makeSlices (*clicks, 0, 0.5f, 16, {});
        check (onsets.size() == 8, "transients: " + juce::String ((int) onsets.size()) + " slices instead of 8");
        for (size_t i = 0; i < onsets.size() && i < 8; ++i)
        {
            const int expected = (int) ((i + 0.25) * 0.4 * 48000.0);
            check (std::abs (onsets[i].start - expected) < 900, "transient " + juce::String ((int) i) + " at " + juce::String (onsets[i].start));
        }
        const auto syl = huldra::analysis::makeSlices (*clicks, 1, 0.5f, 16, {});
        check (syl.size() == 8, "syllables: " + juce::String ((int) syl.size()) + " instead of 8");
        const auto grid = huldra::analysis::makeSlices (*clicks, 2, 0.5f, 12, {});
        check (grid.size() == 12, "grid: " + juce::String ((int) grid.size()));
        huldra::DemoVoices::get().waitUntilReady();
        auto phrase = huldra::DemoVoices::get().voice (7);
        const auto ps = huldra::analysis::makeSlices (*phrase, 1, 0.5f, 16, {});
        printf ("  phrase: %d syllables, root %.2f\n", (int) ps.size(), phrase->rootNote);
        check (ps.size() >= 6 && ps.size() <= 10, "phrase syllables: " + juce::String ((int) ps.size()));
        for (int d = 0; d < huldra::DemoVoices::kCount; ++d)
        {
            auto v = huldra::DemoVoices::get().voice (d);
            check (v != nullptr && v->numFrames > 48000 && v->peak > 0.1f, "demo voice " + juce::String (d));
            if (d != 3 && d != 5 && d != 7)
                check (std::abs (v->rootNote - (d == 2 || d == 6 ? 48.0f : 60.0f)) < 0.6f, "demo root " + juce::String (d) + ": " + juce::String (v->rootNote, 2));
        }
    }

    void chopsTest()
    {
        printf ("Chops\n");
        auto proc = makeProcessor();
        set (proc->apvts, P::DemoVoice, 7);
        set (proc->apvts, P::PlayMode, 1);
        set (proc->apvts, P::ChopMode, 1);
        proc->instrument.rebuildNow();
        const auto& snap = proc->instrument.snapshot();
        check (snap.slices.size() >= 6, "chop slices: " + juce::String ((int) snap.slices.size()));
        std::vector<Note> notes;
        for (int i = 0; i < 8; ++i) notes.push_back ({ 0.05 + i * 0.25, 0.2, 60 + i });
        const auto s = render (*proc, notes, 3.5);
        check (s.finite && s.rms > 0.005 && s.peak < 1.0f, "chops render");
        check (s.tailRms < 0.01, "chops do not stop");
        printf ("  %d slices, rms %.3f\n", (int) snap.slices.size(), s.rms);
    }

    void motionTests()
    {
        printf ("Motion\n");
        for (int mode = 1; mode <= 3; ++mode)
            for (bool autoplay : { false, true })
            {
                auto proc = makeProcessor();
                set (proc->apvts, P::MotMode, (float) mode);
                set (proc->apvts, P::MotFree, autoplay ? 1.0f : 0.0f);
                set (proc->apvts, P::AmpRelease, 0.1f);
                set (proc->apvts, P::GenDensity, 0.8f);
                FakePlayHead head;
                head.playing = true;
                const std::vector<Note> notes = autoplay ? std::vector<Note> {} : std::vector<Note> { { 0.0, 2.0, 60 }, { 0.0, 2.0, 64 } };
                const auto before = proc->voiceDisplay.noteCounter.load();
                const auto s = render (*proc, notes, autoplay ? 2.0 : 3.0, 512, &head);
                const auto played = proc->voiceDisplay.noteCounter.load() - before;
                const auto label = juce::String ("motion ") + juce::String (mode) + (autoplay ? " autoplay" : "");
                check (s.finite && s.peak < 1.0f, label + ": broken output");
                check (played >= 6, label + ": only " + juce::String ((int) played) + " notes");
                if (! autoplay) check (s.tailRms < 0.01, label + ": notes keep playing after release");
                printf ("  %-24s %d notes, rms %.3f\n", label.toRawUTF8(), (int) played, s.rms);
            }

        // Same seed, same phrase
        auto renderGen = [] (int seed)
        {
            auto proc = makeProcessor();
            set (proc->apvts, P::MotMode, 3);
            set (proc->apvts, P::MotFree, 1);
            set (proc->apvts, P::GenSeed, (float) seed);
            set (proc->apvts, P::GenEvolve, 0.0f);
            return render (*proc, {}, 2.0).audio;
        };
        const auto a = renderGen (5), b = renderGen (5), c = renderGen (9);
        float diffSame = 0.0f, diffOther = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            diffSame = std::max (diffSame, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            diffOther = std::max (diffOther, std::abs (a.getSample (0, i) - c.getSample (0, i)));
        }
        check (diffSame < 1.0e-4f, "generative: same seed differs (" + juce::String (diffSame) + ")");
        check (diffOther > 1.0e-3f, "generative: different seeds sound the same");
    }

    void blockSizeTest()
    {
        printf ("Block sizes\n");
        for (int block : { 1, 7, 64, 333, 2048 })
        {
            auto proc = makeProcessor();
            set (proc->apvts, P::SmpLoopMode, 1);
            const auto s = render (*proc, { { 0.01, 0.4, 60 } }, 1.0, block);
            check (s.finite && s.rms > 0.003 && s.peak < 1.0f, "block " + juce::String (block));
        }
        // Host blocks bigger than announced
        auto proc = makeProcessor();
        proc->setPlayConfigDetails (2, 2, testSampleRate, 256);
        proc->prepareToPlay (testSampleRate, 256);
        juce::AudioBuffer<float> big (2, 4096);
        big.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 10);
        proc->processBlock (big, midi);
        check (big.getMagnitude (0, 4096) > 0.001f, "big host block is silent");
    }

    void stateTest()
    {
        printf ("Project state\n");
        juce::MemoryBlock data;
        {
            auto proc = makeProcessor();
            proc->instrument.addSample (sineSample (220.0f, 1.0, "Low"), huldra::InstrumentState::AddMode::Replace);
            proc->instrument.addSample (sineSample (440.0f, 1.0, "High"), huldra::InstrumentState::AddMode::AddAutoMap);
            auto key = proc->instrument.getKey (64);
            key.tune = 3.0f;
            key.reverse = true;
            proc->instrument.setKey (64, key);
            set (proc->apvts, P::Engine, 1);
            const auto zones = proc->instrument.getZones();
            check (zones.size() == 2, "auto map zones: " + juce::String ((int) zones.size()));
            if (zones.size() == 2)
                check (zones[0].hi < zones[1].lo && std::abs (zones[0].root - 57.0f) < 0.3f, "auto map ranges");
            proc->getStateInformation (data);
        }
        auto proc = makeProcessor();
        proc->setStateInformation (data.getData(), (int) data.getSize());
        proc->instrument.waitForLoads();
        const auto samples = proc->instrument.getSamples();
        check (samples.size() == 2, "restored samples: " + juce::String ((int) samples.size()));
        if (samples.size() == 2)
        {
            check (samples[0]->name == "Low" && samples[1]->name == "High", "restored names");
            check (std::abs (samples[0]->rootNote - 57.0f) < 0.3f, "restored audio analyses to the same pitch");
            check (samples[0]->numFrames == 48000, "restored length " + juce::String (samples[0]->numFrames));
        }
        check (std::abs (proc->instrument.getKey (64).tune - 3.0f) < 1.0e-4f && proc->instrument.getKey (64).reverse, "key edits restored");
        check (proc->apvts.getRawParameterValue ("engine")->load() > 0.5f, "parameters restored");
        check (! proc->instrument.snapshot().usingDemo && proc->instrument.snapshot().zones.size() == 2, "snapshot uses the restored samples");
        const auto s = render (*proc, { { 0.0, 0.5, 57 }, { 0.0, 0.5, 69 } }, 1.0);
        check (s.rms > 0.01, "restored project plays");
    }

    void bounceTest()
    {
        printf ("Bounce\n");
        auto proc = makeProcessor();
        set (proc->apvts, P::Engine, 1);
        set (proc->apvts, P::SmpLoopMode, 1);
        juce::AudioBuffer<float> out (2, (int) (2.5 * testSampleRate));
        HuldraProcessor::renderOffline (proc->currentValues(), proc->instrument.snapshot(), proc->getFxOrder(), testSampleRate, 60, 0.8f, 2.0, out);
        check (out.getMagnitude (0, out.getNumSamples()) > 0.01f, "offline render is silent");
        auto s = huldra::InstrumentState::fromBuffer (out, testSampleRate, "Bounce");
        check (s->peak > 0.01f, "bounce analysis");
    }

    void presetTests (const juce::File& outDir, bool writeFiles)
    {
        printf ("Presets\n");
        huldra::PresetManager::ignoreUserPresets = true;
        auto probe = makeProcessor();
        const auto& list = probe->presetManager.getPresets();
        int count = 0;
        std::map<juce::String, int> categories;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (! list[(size_t) i].isFactory) continue;
            ++count;
            ++categories[list[(size_t) i].category];
            auto proc = makeProcessor();
            proc->presetManager.load (i);
            proc->instrument.rebuildNow();
            const bool chops = proc->apvts.getRawParameterValue ("play_mode")->load() > 0.5f;
            const auto s = render (*proc, chops ? std::vector<Note> { { 0.0, 0.3, 60 }, { 0.4, 0.3, 62 }, { 0.8, 0.3, 64 }, { 1.2, 0.3, 65 } }
                                                : std::vector<Note> { { 0.0, 2.0, 60 }, { 0.0, 2.0, 64 }, { 0.0, 2.0, 67 } }, 3.0);
            const auto& name = list[(size_t) i].name;
            check (s.finite, name + ": NaN/Inf");
            check (s.rms > 0.004, name + ": too quiet (" + juce::String (s.rms, 4) + ")");
            check (s.peak < 0.995f, name + ": clipping");
            if (writeFiles) writeWav (s.audio, outDir.getChildFile ("presets").getChildFile (juce::File::createLegalFileName (name) + ".wav"), testSampleRate);
        }
        printf ("  %d factory presets in %d categories\n", count, (int) categories.size());

        // Round trip: a saved preset brings back the same parameters and treatment
        auto proc = makeProcessor();
        set (proc->apvts, P::GrnSize, 333.0f);
        auto pattern = proc->instrument.getPattern();
        pattern.steps[3].note = 7;
        pattern.steps[3].on = true;
        proc->instrument.setPattern (pattern);
        const auto xml = proc->presetManager.createPresetXml ("Test", "User");
        auto other = makeProcessor();
        other->presetManager.applyXml (*xml, true);
        check (std::abs (other->apvts.getRawParameterValue ("grn_size")->load() - 333.0f) < 0.5f, "preset round trip: parameter");
        check (other->instrument.getPattern().steps[3].note == 7, "preset round trip: pattern");
    }
}

// ---------------------------------------------------------------------------------------------
// Loudness of every factory preset (RMS over 2 s of a held C major triad, or four chops) as JSON.
static int loudness (const juce::File& out)
{
    huldra::PresetManager::ignoreUserPresets = true;
    auto probe = makeProcessor();
    const auto& list = probe->presetManager.getPresets();
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].isFactory) continue;
        auto proc = makeProcessor();
        proc->presetManager.load (i);
        proc->instrument.rebuildNow();
        set (proc->apvts, P::PresetLevel, 0.0f);
        set (proc->apvts, P::LimOn, 0);
        const bool chops = proc->apvts.getRawParameterValue ("play_mode")->load() > 0.5f;
        const auto s = render (*proc, chops ? std::vector<Note> { { 0.0, 0.3, 60 }, { 0.4, 0.3, 62 }, { 0.8, 0.3, 64 }, { 1.2, 0.3, 65 } }
                                            : std::vector<Note> { { 0.0, 2.0, 60 }, { 0.0, 2.0, 64 }, { 0.0, 2.0, 67 } }, 2.0);
        juce::DynamicObject::Ptr entry = new juce::DynamicObject();
        entry->setProperty ("rms", juce::Decibels::gainToDecibels (s.rms, -120.0));
        entry->setProperty ("peak", juce::Decibels::gainToDecibels ((double) s.peak, -120.0));
        root->setProperty (list[(size_t) i].name, juce::var (entry.get()));
    }
    out.replaceWithText (juce::JSON::toString (juce::var (root.get())));
    printf ("Wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

// Parameter table for the preset generator: id, kind, min, max, default, mod target index, choices
static int paramInfo (const juce::File& out)
{
    juce::StringArray lines;
    const auto& targets = P::modTargets();
    for (const auto& s : P::specs())
    {
        const auto it = std::find (targets.begin(), targets.end(), s.id);
        const int target = it == targets.end() ? -1 : (int) std::distance (targets.begin(), it) + 1;
        const char* kind = s.kind == P::Kind::Float ? "float" : s.kind == P::Kind::Int ? "int" : s.kind == P::Kind::Choice ? "choice" : "bool";
        lines.add (juce::String (s.paramId) + "\t" + kind + "\t" + juce::String (s.min) + "\t" + juce::String (s.max) + "\t"
                   + juce::String (s.def) + "\t" + juce::String (target) + "\t" + s.choices.joinIntoString ("|"));
    }
    out.replaceWithText (lines.joinIntoString ("\n") + "\n");
    return 0;
}

static int findPreset (HuldraProcessor& proc, const juce::String& name)
{
    const auto& list = proc.presetManager.getPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == name) return i;
    return -1;
}

// One preset (or "init") as a WAV: a held chord, or chops
static int single (const juce::String& presetName, const juce::File& out, int note)
{
    huldra::PresetManager::ignoreUserPresets = true;
    auto proc = makeProcessor();
    if (presetName != "init")
    {
        const int index = findPreset (*proc, presetName);
        if (index < 0) { printf ("Preset not found: %s\n", presetName.toRawUTF8()); return 1; }
        proc->presetManager.load (index);
        proc->instrument.rebuildNow();
    }
    FakePlayHead head;
    head.playing = true;
    const auto s = render (*proc, { { 0.0, 3.0, note }, { 0.0, 3.0, note + 4 }, { 0.0, 3.0, note + 7 } }, 5.0, 512, &head);
    writeWav (s.audio, out, testSampleRate);
    printf ("peak %.3f rms %.3f\n", s.peak, s.rms);
    return 0;
}

// One UI screenshot: page = source|slice|keymap|engine|spectral|keys|mod|motion|fx|browser|about|save, theme = light|dark
static int takeScreenshot (const juce::String& pageName, const juce::String& presetName, const juce::String& theme,
                           const juce::File& outFile, int fxIndex)
{
    auto pages = HuldraView::pageNames;
    pages.addArray ({ "browser", "about", "save" });
    const int pageIndex = pages.indexOf (pageName);
    if (pageIndex < 0) { printf ("Unknown page: %s\n", pageName.toRawUTF8()); return 1; }

    huldra::PresetManager::ignoreUserPresets = true;
    auto proc = makeProcessor();
    // "Preset name:param=value,param=value" sets parameters after loading (for showing a page in a certain state)
    const auto name = presetName.upToFirstOccurrenceOf (":", false, false);
    if (name != "init")
    {
        const int index = findPreset (*proc, name);
        if (index < 0) { printf ("Preset not found: %s\n", name.toRawUTF8()); return 1; }
        proc->presetManager.load (index);
    }
    for (const auto& kv : juce::StringArray::fromTokens (presetName.fromFirstOccurrenceOf (":", false, false), ",", ""))
        if (kv.startsWith ("test_samples="))
        {
            // a small multi-sample: sines a fifth apart, spread over the keys
            const int count = kv.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            for (int i = 0; i < count; ++i)
                proc->instrument.addSample (sineSample (110.0f * std::pow (1.5f, (float) i), 1.5, "Sine " + juce::String (i + 1)),
                                            i == 0 ? huldra::InstrumentState::AddMode::Replace : huldra::InstrumentState::AddMode::AddAutoMap);
        }
        else if (kv.startsWith ("test_keys="))
        {
            huldra::KeyEdit a; a.tune = 7.0f; a.engine = 1; proc->instrument.setKey (62, a);
            huldra::KeyEdit b; b.reverse = true; b.gainDb = -6.0f; proc->instrument.setKey (65, b);
            huldra::KeyEdit c; c.mute = true; proc->instrument.setKey (66, c);
            huldra::KeyEdit d; d.choke = 1; d.tune = -12.0f; proc->instrument.setKey (48, d);
            huldra::KeyEdit e; e.choke = 1; e.engine = 3; e.gainDb = 4.0f; proc->instrument.setKey (49, e);
        }
        else if (auto* prm = proc->apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false)))
            prm->setValueNotifyingHost (prm->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    proc->instrument.rebuildNow();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    huldra::ui::Theme::get().setDark (theme == "dark");
    std::unique_ptr<juce::AudioProcessorEditor> editorWindow (proc->createEditor());
    editorWindow->setSize (HuldraView::baseWidth, HuldraView::baseHeight);
    auto* editor = dynamic_cast<HuldraEditor*> (editorWindow.get());
    if (editor == nullptr) return 1;
    auto& view = editor->getView();

    const int numPages = HuldraView::pageNames.size();
    if (pageIndex == numPages)          { view.selectPage (0); view.showBrowser (true); }
    else if (pageIndex == numPages + 1) { proc->setRateAndBufferSizeDetails (48000.0, 512); view.selectPage (0); view.showAbout(); }
    else if (pageIndex == numPages + 2) { view.selectPage (0); view.showSaveDialog(); }
    else                                view.selectPage (pageIndex);
    if (pageIndex == 8 && fxIndex >= 0)
        view.getFxPage().selectEffect (fxIndex);

    // Play a chord while the visuals animate, so they show the sound
    juce::AudioBuffer<float> buffer (2, 512);
    for (int k = 0; k < 220; ++k)
    {
        juce::MidiBuffer midi;
        if (k == 110) for (int n : { 60, 64, 67 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
        buffer.clear();
        proc->processBlock (buffer, midi);
        view.tickAnimations();
    }

    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
    editorWindow.reset();
    huldra::ui::Theme::get().setDark (false);
    return outFile.existsAsFile() ? 0 : 1;
}


// App icon (1024 x 1024) in the Fjord family style: a night forest by a lake, a rose veil of light over the trees,
// the "h." mark and its reflection in the water.
static int makeIcon (const juce::File& out)
{
    constexpr int size = 1024;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const auto card = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    juce::Path shape;
    shape.addRoundedRectangle (card, 210.0f);
    for (int i = 12; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.012f));
        g.fillPath (shape, juce::AffineTransform::translation (0.0f, 10.0f).scaled (1.0f + (float) i * 0.004f, 1.0f + (float) i * 0.004f, 512.0f, 540.0f));
    }
    g.saveState();
    g.reduceClipRegion (shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141a2e), 0.0f, 100.0f, juce::Colour (0xff0a0f18), 0.0f, 924.0f, false));
    g.fillRect (card);

    // Veil: three soft ribbons of light (rose, lilac, ice)
    const juce::Colour veil[] = { juce::Colour (0xfff29c9c), juce::Colour (0xffb49cf2), juce::Colour (0xff7fc4e8) };
    for (int k = 0; k < 3; ++k)
    {
        juce::Path ribbon;
        const float y0 = 300.0f + 55.0f * (float) k;
        for (int i = 0; i <= 80; ++i)
        {
            const float x = (float) i / 80.0f;
            const float y = y0 + 60.0f * std::sin (x * (3.2f + (float) k * 0.6f) + (float) k * 1.4f) + 18.0f * std::sin (x * 11.0f + (float) k);
            if (i == 0) ribbon.startNewSubPath (100.0f, y); else ribbon.lineTo (100.0f + x * 824.0f, y);
        }
        for (auto [w, a] : { std::pair { 120.0f, 0.10f }, std::pair { 50.0f, 0.18f }, std::pair { 10.0f, 0.55f } })
        {
            g.setColour (veil[k].withAlpha (a));
            g.strokePath (ribbon, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    // stars
    juce::Random rnd (7);
    for (int i = 0; i < 70; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * rnd.nextFloat()));
        const float s = 2.0f + 3.0f * rnd.nextFloat();
        g.fillEllipse (100.0f + rnd.nextFloat() * 824.0f, 110.0f + rnd.nextFloat() * 300.0f, s, s);
    }
    // Forest: rows of pointed trees
    auto forest = [&] (float base, float height, juce::Colour c, int seed)
    {
        juce::Random r (seed);
        juce::Path p;
        p.startNewSubPath (100.0f, 680.0f);
        float x = 100.0f;
        while (x < 924.0f)
        {
            const float w = 34.0f + r.nextFloat() * 40.0f;
            const float h = height * (0.6f + 0.6f * r.nextFloat());
            p.lineTo (x, base);
            p.lineTo (x + w * 0.5f, base - h);
            p.lineTo (x + w, base);
            x += w * 0.8f;
        }
        p.lineTo (924.0f, 680.0f);
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    };
    forest (640.0f, 170.0f, juce::Colour (0xff1c2238), 3);
    forest (668.0f, 120.0f, juce::Colour (0xff0d111d), 11);
    g.setColour (juce::Colour (0xff080b13));
    g.fillRect (100.0f, 664.0f, 824.0f, 264.0f);

    // The mark
    const auto markFont = huldra::ui::font (330.0f, huldra::ui::Weight::Regular);
    g.setFont (markFont);
    g.setColour (juce::Colour (0xffe2edf0));
    g.drawText ("h", juce::Rectangle<float> (330.0f, 320.0f, 260.0f, 340.0f), juce::Justification::centredBottom, false);
    g.setColour (juce::Colour (0xfff29c9c));
    g.fillEllipse (juce::Rectangle<float> (46.0f, 46.0f).withCentre ({ 572.0f, 572.0f }));
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> (100, 668, 824, 220));
        g.addTransform (juce::AffineTransform::verticalFlip (1300.0f));
        g.setColour (juce::Colour (0xffe2edf0).withAlpha (0.13f));
        g.drawText ("h", juce::Rectangle<float> (330.0f, 320.0f, 260.0f, 340.0f), juce::Justification::centredBottom, false);
        g.setColour (juce::Colour (0xfff29c9c).withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (46.0f, 46.0f).withCentre ({ 572.0f, 572.0f }));
    }
    // Reflection lines in the veil colours
    const float widths[] = { 500.0f, 340.0f, 200.0f, 90.0f };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (veil[i % 3].withAlpha (0.8f));
        g.fillRoundedRectangle (juce::Rectangle<float> (widths[i], 7.0f).withCentre ({ 512.0f, 724.0f + 41.0f * (float) i }), 3.5f);
    }
    g.restoreState();
    g.setColour (juce::Colour (0x40ffffff));
    g.strokePath (shape, juce::PathStrokeType (2.0f));
    out.deleteFile();
    if (auto stream = out.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (img, *stream);
    printf ("Wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    huldra::ui::Theme::get().setPersistent (false);

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (const int i = args.indexOf ("--sr"); i >= 0 && i + 1 < args.size())
    {
        testSampleRate = args[i + 1].getDoubleValue();
        args.removeRange (i, 2);
    }

    auto file = [] (const juce::String& path) { return juce::File::getCurrentWorkingDirectory().getChildFile (path); };
    if (args.size() > 1 && args[0] == "--icon")       return makeIcon (file (args[1]));
    if (args.size() > 1 && args[0] == "--param-info") return paramInfo (file (args[1]));
    if (args.size() > 1 && args[0] == "--loudness")   return loudness (file (args[1]));
    if (args.size() > 2 && args[0] == "--single")     return single (args[1], file (args[2]), args.size() > 3 ? args[3].getIntValue() : 60);
    if (args.size() > 4 && args[0] == "--screenshot")
        return takeScreenshot (args[1], args[2], args[3], file (args[4]), args.size() > 5 ? args[5].getIntValue() : -1);

    const auto outDir = file (args.isEmpty() ? "build/renders" : args[0]);
    const bool writeFiles = juce::SystemStats::getEnvironmentVariable ("HULDRA_WAV", {}).getIntValue() != 0;
    printf ("Huldra render test at %.0f Hz\n", testSampleRate);

    analysisTests();
    engineTests (outDir, writeFiles);
    pitchAccuracyTests();
    cpuTests();
    fuzzTests (juce::SystemStats::getEnvironmentVariable ("HULDRA_FUZZ", "40").getIntValue());
    engineFeatureTests();
    chopsTest();
    motionTests();
    blockSizeTest();
    stateTest();
    bounceTest();
    presetTests (outDir, writeFiles);

    printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
