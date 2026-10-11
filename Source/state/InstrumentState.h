#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../sample/Analysis.h"
#include "../sample/DemoVoices.h"
#include "../dsp/Envelope.h"
#include "../motion/Pattern.h"
#include "../Parameters.h"

namespace huldra
{
    constexpr int kMaskBands = 32;
    constexpr int kNumKeys = 128;

    // Per-key changes (the KEYS page). Every key can be retuned, re-levelled, filtered, reversed, sent to
    // another engine or slice, muted or put into a choke group.
    struct KeyEdit
    {
        float tune = 0.0f;       // semitones
        float gainDb = 0.0f;
        float pan = 0.0f;
        float cutoff = 0.0f;     // octaves added to the filter cutoff
        float start = 0.0f;      // 0..1 extra start offset
        float formant = 0.0f;    // semitones (vocal engine)
        int engine = -1;         // -1 = zone / global engine
        int slice = -1;          // Chops: -1 = automatic (key - first key)
        int choke = 0;           // 0 = none, 1..8
        bool reverse = false, mute = false;

        bool isDefault() const
        {
            return std::abs (tune) < 1.0e-4f && std::abs (gainDb) < 1.0e-4f && std::abs (pan) < 1.0e-4f
                && std::abs (cutoff) < 1.0e-4f && std::abs (start) < 1.0e-4f && std::abs (formant) < 1.0e-4f
                && engine < 0 && slice < 0 && choke == 0 && ! reverse && ! mute;
        }
    };

    // A key/velocity zone of the keymap (Melodic mode).
    struct ZoneInfo
    {
        juce::String sample;
        int lo = 0, hi = 127, vlo = 1, vhi = 127;
        float root = 60.0f;      // MIDI note the sample plays at its own pitch (fractional = fine tuning)
        float tune = 0.0f, gainDb = 0.0f, pan = 0.0f;
        int engine = -1;
    };

    // Everything the audio thread needs, immutable once published.
    struct Snapshot
    {
        struct Zone
        {
            const SampleData* sample = nullptr;
            int lo = 0, hi = 127, vlo = 1, vhi = 127;
            float root = 60.0f, tune = 0.0f, gain = 1.0f, pan = 0.0f;
            int engine = -1;
        };

        std::vector<Zone> zones;
        const SampleData* chopSample = nullptr;      // the active sample (Chops mode, SOURCE page)
        std::vector<analysis::Slice> slices;
        std::array<KeyEdit, kNumKeys> keys {};
        std::array<float, kMaskBands> mask {};
        std::array<dsp::EnvShape, dsp::kNumMsegs> msegs {};
        motion::Pattern pattern;
        bool usingDemo = false, loading = false;
        uint32_t version = 0;
        std::vector<SamplePtr> keepAlive;
    };

    class InstrumentState : public juce::ChangeBroadcaster,
                            private juce::ValueTree::Listener,
                            private juce::Timer,
                            private juce::AsyncUpdater
    {
    public:
        static inline const juce::Identifier instrumentId { "Instrument" }, samplesId { "Samples" }, sampleId { "Sample" },
            zonesId { "Zones" }, zoneId { "Zone" }, keysId { "Keys" }, keyId { "Key" }, msegsId { "Msegs" }, msegId { "Mseg" },
            patternId { "Pattern" }, maskId { "Mask" }, slicesId { "Slices" };

        enum class AddMode { Replace, Add, AddAutoMap };

        InstrumentState (juce::AudioProcessorValueTreeState&, juce::UndoManager*);
        ~InstrumentState() override;

        // ---- Audio thread ----
        const Snapshot* acquire() const { return current.load (std::memory_order_acquire); }

        // ---- Message thread ----
        const Snapshot& snapshot() const { return *current.load (std::memory_order_acquire); }
        static void ensureChildren (juce::ValueTree state);
        juce::ValueTree tree() const { return apvts.state.getChildWithName (instrumentId); }

        // Samples
        void loadFilesAsync (const juce::Array<juce::File>&, AddMode);
        void addSample (SamplePtr, AddMode);       // the sample must be analysed
        void removeSample (const juce::String& uid);
        void setActiveSample (const juce::String& uid);
        juce::String getActiveUid() const;
        SamplePtr getSample (const juce::String& uid) const;
        std::vector<SamplePtr> getSamples() const;    // in keymap order
        int numPendingLoads() const { return pendingLoads.load(); }
        juce::String getLastError() const { const juce::ScopedLock sl (bankLock); return lastError; }

        static SamplePtr decodeFile (const juce::File&, juce::String& error);
        static SamplePtr fromBuffer (const juce::AudioBuffer<float>&, double sampleRate, const juce::String& name);

        // Keymap
        std::vector<ZoneInfo> getZones() const;
        void setZone (int index, const ZoneInfo&, bool undoable = true);
        void removeZone (int index);
        void autoMap();                                // spreads all samples over the keyboard by pitch

        // Per-key edits
        KeyEdit getKey (int note) const;
        void setKey (int note, const KeyEdit&, bool undoable = true);
        void resetKeys();

        // Manual slice points (0..1 of the active sample)
        std::vector<float> getManualSlices() const;
        void setManualSlices (const std::vector<float>&, bool undoable = true);

        // Shapes and pattern
        dsp::EnvShape getMseg (int index) const;
        void setMseg (int index, const dsp::EnvShape&, bool undoable = true);
        motion::Pattern getPattern() const;
        void setPattern (const motion::Pattern&, bool undoable = true);
        std::array<float, kMaskBands> getMask() const;
        void setMask (const std::array<float, kMaskBands>&, bool undoable = true);

        // Presets carry the "treatment" (keys, MSEGs, pattern, mask), never the user's samples.
        void writeTreatment (juce::XmlElement& presetXml) const;
        void applyTreatment (const juce::XmlElement& presetXml, juce::UndoManager*);
        static void resetTreatment (juce::ValueTree instrument, juce::UndoManager*);

        // Project state: the audio of every sample is embedded (FLAC) or referenced by path.
        void embedAudio (juce::ValueTree& stateCopy) const;
        void restoreAudio();       // after the state was replaced: decodes embedded audio / files in the background
        void waitForLoads();       // tests: block until all background loads are done, then rebuild

        void rebuildNow();         // tests and offline renders
        juce::UndoManager* getUndoManager() const { return undo; }

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { scheduleRebuild(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { scheduleRebuild(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { scheduleRebuild(); }
        void valueTreeRedirected (juce::ValueTree& t) override { ensureChildren (t); scheduleRebuild(); }
        void handleAsyncUpdate() override { rebuildNow(); }
        void timerCallback() override;
        void scheduleRebuild() { triggerAsyncUpdate(); }
        void publish (std::unique_ptr<Snapshot>);
        void collectGarbage (bool force);
        juce::ValueTree child (const juce::Identifier& id) const { return tree().getOrCreateChildWithName (id, nullptr); }
        void addSampleNode (const SampleData&, juce::UndoManager*);
        void startLoadJob (std::function<SamplePtr()> job, std::function<void (SamplePtr)> done);
        void setError (const juce::String& e) { const juce::ScopedLock sl (bankLock); lastError = e; }

        juce::AudioProcessorValueTreeState& apvts;
        juce::UndoManager* undo;

        mutable juce::CriticalSection bankLock;
        std::map<juce::String, SamplePtr> bank;     // uid -> sample
        std::vector<SamplePtr> trash;               // removed, waiting until no voice and no snapshot uses them

        std::atomic<Snapshot*> current { nullptr };
        std::vector<std::pair<std::unique_ptr<Snapshot>, double>> retired;
        uint32_t version = 0;

        // Slice cache (slicing only reruns when its inputs change)
        juce::String sliceKey;
        std::vector<analysis::Slice> sliceCache;
        std::array<float, 4> watchedParams { -1, -1, -1, -1 };

        juce::ThreadPool pool { 2 };
        std::atomic<int> pendingLoads { 0 };
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE (InstrumentState)
    };
}
