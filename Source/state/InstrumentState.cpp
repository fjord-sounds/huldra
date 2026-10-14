#include "InstrumentState.h"
#include <set>

namespace huldra
{
    namespace
    {
        const juce::Identifier uidProp { "uid" }, nameProp { "name" }, pathProp { "path" }, audioProp { "audio" },
            activeProp { "active" }, embedProp { "embed" }, pointsProp { "points" }, stepsProp { "steps" }, bandsProp { "bands" },
            idProp { "id" }, noteProp { "note" };

        constexpr int kMaxSeconds = 600;

        juce::String maskToString (const std::array<float, kMaskBands>& m)
        {
            juce::StringArray s;
            for (float v : m) s.add (juce::String (v, 3));
            return s.joinIntoString (" ");
        }

        std::array<float, kMaskBands> maskFromString (const juce::String& text)
        {
            std::array<float, kMaskBands> m;
            m.fill (1.0f);
            const auto t = juce::StringArray::fromTokens (text, " ", "");
            for (int i = 0; i < std::min (kMaskBands, t.size()); ++i)
                m[(size_t) i] = std::clamp (t[i].getFloatValue(), 0.0f, 1.0f);
            return m;
        }

        KeyEdit keyFromTree (const juce::ValueTree& k)
        {
            KeyEdit e;
            e.tune = k.getProperty ("tune", 0.0f);
            e.gainDb = k.getProperty ("gain", 0.0f);
            e.pan = k.getProperty ("pan", 0.0f);
            e.cutoff = k.getProperty ("cutoff", 0.0f);
            e.start = k.getProperty ("start", 0.0f);
            e.formant = k.getProperty ("formant", 0.0f);
            e.engine = k.getProperty ("engine", -1);
            e.slice = k.getProperty ("slice", -1);
            e.choke = k.getProperty ("choke", 0);
            e.reverse = k.getProperty ("reverse", false);
            e.mute = k.getProperty ("mute", false);
            return e;
        }

        void keyToTree (juce::ValueTree k, const KeyEdit& e, juce::UndoManager* um)
        {
            k.setProperty ("tune", e.tune, um);
            k.setProperty ("gain", e.gainDb, um);
            k.setProperty ("pan", e.pan, um);
            k.setProperty ("cutoff", e.cutoff, um);
            k.setProperty ("start", e.start, um);
            k.setProperty ("formant", e.formant, um);
            k.setProperty ("engine", e.engine, um);
            k.setProperty ("slice", e.slice, um);
            k.setProperty ("choke", e.choke, um);
            k.setProperty ("reverse", e.reverse, um);
            k.setProperty ("mute", e.mute, um);
        }

        SamplePtr makeSample (const juce::AudioBuffer<float>& buffer, int numFrames, double sr, const juce::String& name)
        {
            auto s = std::make_shared<SampleData>();
            s->uid = juce::Uuid().toString();
            s->name = name;
            s->sampleRate = sr;
            s->numChannels = std::min (2, buffer.getNumChannels());
            s->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + numFrames);
            if (s->numChannels > 1)
            {
                // Keep stereo only when the channels actually differ
                bool same = true;
                const float* l = buffer.getReadPointer (0);
                const float* r = buffer.getReadPointer (1);
                for (int i = 0; i < numFrames && same; i += 7) same = std::abs (l[i] - r[i]) < 1.0e-5f;
                if (! same) s->right.assign (r, r + numFrames);
                else s->numChannels = 1;
            }
            analysis::analyse (*s);
            return s;
        }

        SamplePtr readFromReader (juce::AudioFormatReader& reader, const juce::String& name, juce::String& error)
        {
            const auto length = (int) std::min<juce::int64> (reader.lengthInSamples, (juce::int64) (reader.sampleRate * kMaxSeconds));
            if (length < 16 || reader.sampleRate < 8000.0)
            {
                error = "The file is empty or its sample rate is not supported.";
                return nullptr;
            }
            const int channels = (int) std::min<unsigned int> (2, reader.numChannels);
            juce::AudioBuffer<float> buffer (channels, length);
            reader.read (&buffer, 0, length, 0, true, channels > 1);
            return makeSample (buffer, length, reader.sampleRate, name);
        }
    }

    InstrumentState::InstrumentState (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um) : apvts (s), undo (um)
    {
        ensureChildren (apvts.state);
        apvts.state.addListener (this);
        rebuildNow();
        startTimerHz (10);
    }

    InstrumentState::~InstrumentState()
    {
        alive->store (false);
        stopTimer();
        cancelPendingUpdate();
        apvts.state.removeListener (this);
        pool.removeAllJobs (true, 10000);
        delete current.exchange (nullptr);
        retired.clear();
    }

    void InstrumentState::ensureChildren (juce::ValueTree state)
    {
        auto inst = state.getOrCreateChildWithName (instrumentId, nullptr);
        inst.getOrCreateChildWithName (samplesId, nullptr);
        inst.getOrCreateChildWithName (zonesId, nullptr);
        inst.getOrCreateChildWithName (keysId, nullptr);
        inst.getOrCreateChildWithName (slicesId, nullptr);
        auto msegs = inst.getOrCreateChildWithName (msegsId, nullptr);
        for (int i = 0; i < dsp::kNumMsegs; ++i)
            if (! msegs.getChildWithProperty (idProp, i).isValid())
            {
                juce::ValueTree m (msegId);
                m.setProperty (idProp, i, nullptr);
                m.setProperty (pointsProp, dsp::defaultMseg (i).toString(), nullptr);
                msegs.appendChild (m, nullptr);
            }
        auto pattern = inst.getOrCreateChildWithName (patternId, nullptr);
        if (! pattern.hasProperty (stepsProp))
            pattern.setProperty (stepsProp, motion::Pattern::defaultPattern().toString(), nullptr);
        auto mask = inst.getOrCreateChildWithName (maskId, nullptr);
        if (! mask.hasProperty (bandsProp))
        {
            std::array<float, kMaskBands> m;
            m.fill (1.0f);
            mask.setProperty (bandsProp, maskToString (m), nullptr);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Loading

    SamplePtr InstrumentState::decodeFile (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Huldra can't read " + file.getFileName() + ".";
            return nullptr;
        }
        auto s = readFromReader (*reader, file.getFileNameWithoutExtension(), error);
        if (s != nullptr) s->file = file;
        return s;
    }

    SamplePtr InstrumentState::fromBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& name)
    {
        return makeSample (buffer, buffer.getNumSamples(), sampleRate, name);
    }

    void InstrumentState::startLoadJob (std::function<SamplePtr()> job, std::function<void (SamplePtr)> done)
    {
        pendingLoads.fetch_add (1);
        auto token = alive;
        pool.addJob ([this, token, job = std::move (job), done = std::move (done)]
        {
            auto s = job();
            juce::MessageManager::callAsync ([this, token, s, done]
            {
                if (! token->load()) return;
                done (s);
                pendingLoads.fetch_sub (1);
                rebuildNow();
            });
        });
    }

    void InstrumentState::loadFilesAsync (const juce::Array<juce::File>& files, AddMode mode)
    {
        // One file replaces the sound (or is added); several files are spread over the keyboard by pitch.
        const bool several = files.size() > 1;
        if (several && mode == AddMode::Replace)
        {
            undo->beginNewTransaction ("Load samples");
            tree().getChildWithName (samplesId).removeAllChildren (undo);
            tree().getChildWithName (zonesId).removeAllChildren (undo);
            tree().setProperty ("active", juce::var(), undo);
        }
        const auto effective = several ? AddMode::AddAutoMap : mode;
        for (const auto& f : files)
            startLoadJob ([f, this]
                          {
                              juce::String err;
                              auto s = decodeFile (f, err);
                              if (s == nullptr) setError (err);
                              return s;
                          },
                          [this, effective] (SamplePtr s)
                          {
                              if (s != nullptr) addSample (s, effective);
                              sendChangeMessage();
                          });
    }

    void InstrumentState::addSampleNode (const SampleData& s, juce::UndoManager* um)
    {
        juce::ValueTree node (sampleId);
        node.setProperty (uidProp, s.uid, nullptr);
        node.setProperty (nameProp, s.name, nullptr);
        if (s.file != juce::File()) node.setProperty (pathProp, s.file.getFullPathName(), nullptr);
        node.setProperty (embedProp, true, nullptr);
        tree().getChildWithName (samplesId).appendChild (node, um);
    }

    void InstrumentState::addSample (SamplePtr s, AddMode mode)
    {
        if (s == nullptr) return;
        {
            const juce::ScopedLock sl (bankLock);
            bank[s->uid] = s;
        }
        undo->beginNewTransaction ("Add sample");
        auto samples = tree().getChildWithName (samplesId);
        auto zones = tree().getChildWithName (zonesId);

        if (mode == AddMode::Replace)
        {
            // The old samples stay in the bank so that undo can bring them back
            samples.removeAllChildren (undo);
            zones.removeAllChildren (undo);
        }

        addSampleNode (*s, undo);
        if (mode == AddMode::AddAutoMap)
            autoMap();
        else
        {
            juce::ValueTree z (zoneId);
            z.setProperty ("sample", s->uid, nullptr);
            z.setProperty ("lo", 0, nullptr);
            z.setProperty ("hi", 127, nullptr);
            z.setProperty ("vlo", 1, nullptr);
            z.setProperty ("vhi", 127, nullptr);
            z.setProperty ("root", s->pitchConfidence > 0.3f ? s->rootNote : 60.0f, nullptr);
            zones.appendChild (z, undo);
        }
        if (mode == AddMode::Replace || getActiveUid().isEmpty())
            tree().setProperty (activeProp, s->uid, undo);
        rebuildNow();
    }

    void InstrumentState::removeSample (const juce::String& uid)
    {
        undo->beginNewTransaction ("Remove sample");
        auto samples = tree().getChildWithName (samplesId);
        auto zones = tree().getChildWithName (zonesId);
        for (int i = zones.getNumChildren(); --i >= 0;)
            if (zones.getChild (i).getProperty ("sample").toString() == uid)
                zones.removeChild (i, undo);
        for (int i = samples.getNumChildren(); --i >= 0;)
            if (samples.getChild (i).getProperty (uidProp).toString() == uid)
                samples.removeChild (i, undo);
        if (getActiveUid() == uid)
            tree().setProperty (activeProp, samples.getNumChildren() > 0 ? samples.getChild (0).getProperty (uidProp) : juce::var(), undo);
        // The sample stays in the bank (undo can bring it back); it is only dropped when the project is saved/reloaded.
        rebuildNow();
    }

    void InstrumentState::setActiveSample (const juce::String& uid)
    {
        tree().setProperty (activeProp, uid, nullptr);
    }

    juce::String InstrumentState::getActiveUid() const
    {
        return tree().getProperty (activeProp).toString();
    }

    SamplePtr InstrumentState::getSample (const juce::String& uid) const
    {
        const juce::ScopedLock sl (bankLock);
        const auto it = bank.find (uid);
        return it != bank.end() ? it->second : nullptr;
    }

    std::vector<SamplePtr> InstrumentState::getSamples() const
    {
        std::vector<SamplePtr> out;
        const auto samples = tree().getChildWithName (samplesId);
        for (int i = 0; i < samples.getNumChildren(); ++i)
            if (auto s = getSample (samples.getChild (i).getProperty (uidProp).toString()))
                out.push_back (s);
        return out;
    }

    // ---------------------------------------------------------------------------------------------
    // Keymap

    std::vector<ZoneInfo> InstrumentState::getZones() const
    {
        std::vector<ZoneInfo> out;
        const auto zones = tree().getChildWithName (zonesId);
        for (int i = 0; i < zones.getNumChildren(); ++i)
        {
            const auto z = zones.getChild (i);
            ZoneInfo info;
            info.sample = z.getProperty ("sample").toString();
            info.lo = z.getProperty ("lo", 0);
            info.hi = z.getProperty ("hi", 127);
            info.vlo = z.getProperty ("vlo", 1);
            info.vhi = z.getProperty ("vhi", 127);
            info.root = z.getProperty ("root", 60.0f);
            info.tune = z.getProperty ("tune", 0.0f);
            info.gainDb = z.getProperty ("gain", 0.0f);
            info.pan = z.getProperty ("pan", 0.0f);
            info.engine = z.getProperty ("engine", -1);
            out.push_back (info);
        }
        return out;
    }

    void InstrumentState::setZone (int index, const ZoneInfo& info, bool undoable)
    {
        auto z = tree().getChildWithName (zonesId).getChild (index);
        if (! z.isValid()) return;
        auto* um = undoable ? undo : nullptr;
        z.setProperty ("sample", info.sample, um);
        z.setProperty ("lo", std::clamp (info.lo, 0, 127), um);
        z.setProperty ("hi", std::clamp (info.hi, info.lo, 127), um);
        z.setProperty ("vlo", std::clamp (info.vlo, 1, 127), um);
        z.setProperty ("vhi", std::clamp (info.vhi, info.vlo, 127), um);
        z.setProperty ("root", info.root, um);
        z.setProperty ("tune", info.tune, um);
        z.setProperty ("gain", info.gainDb, um);
        z.setProperty ("pan", info.pan, um);
        z.setProperty ("engine", info.engine, um);
    }

    void InstrumentState::removeZone (int index)
    {
        tree().getChildWithName (zonesId).removeChild (index, undo);
    }

    void InstrumentState::autoMap()
    {
        auto zones = tree().getChildWithName (zonesId);
        zones.removeAllChildren (undo);
        auto samples = getSamples();
        std::vector<SamplePtr> pitched, unpitched;
        for (auto& s : samples)
            (s->pitchConfidence > 0.3f ? pitched : unpitched).push_back (s);
        std::stable_sort (pitched.begin(), pitched.end(), [] (const SamplePtr& a, const SamplePtr& b) { return a->rootNote < b->rootNote; });

        auto addZone = [&] (const SampleData& s, int lo, int hi, float root)
        {
            juce::ValueTree z (zoneId);
            z.setProperty ("sample", s.uid, nullptr);
            z.setProperty ("lo", lo, nullptr);
            z.setProperty ("hi", hi, nullptr);
            z.setProperty ("vlo", 1, nullptr);
            z.setProperty ("vhi", 127, nullptr);
            z.setProperty ("root", root, nullptr);
            zones.appendChild (z, undo);
        };

        // Unpitched sounds (drums, noises, chops) sit on single keys from C1 upwards, played at their own pitch
        int key = 36;
        for (auto& s : unpitched)
        {
            addZone (*s, key, key, (float) key);
            key = std::min (127, key + 1);
        }
        // Pitched sounds share the rest of the keyboard, split half-way between their roots
        const int lowest = unpitched.empty() ? 0 : key;
        for (size_t i = 0; i < pitched.size(); ++i)
        {
            const float root = pitched[i]->rootNote;
            int lo = i == 0 ? lowest : (int) std::floor ((pitched[i - 1]->rootNote + root) * 0.5f) + 1;
            int hi = i + 1 == pitched.size() ? 127 : (int) std::floor ((root + pitched[i + 1]->rootNote) * 0.5f);
            lo = std::clamp (lo, lowest, 127);
            hi = std::clamp (hi, lo, 127);
            addZone (*pitched[i], lo, hi, root);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Keys, slices, shapes

    KeyEdit InstrumentState::getKey (int note) const
    {
        const auto k = tree().getChildWithName (keysId).getChildWithProperty (noteProp, note);
        return k.isValid() ? keyFromTree (k) : KeyEdit {};
    }

    void InstrumentState::setKey (int note, const KeyEdit& e, bool undoable)
    {
        auto keys = tree().getChildWithName (keysId);
        auto k = keys.getChildWithProperty (noteProp, note);
        auto* um = undoable ? undo : nullptr;
        if (e.isDefault())
        {
            if (k.isValid()) keys.removeChild (k, um);
            return;
        }
        if (! k.isValid())
        {
            k = juce::ValueTree (keyId);
            k.setProperty (noteProp, note, nullptr);
            keys.appendChild (k, um);
        }
        keyToTree (k, e, um);
    }

    void InstrumentState::resetKeys()
    {
        tree().getChildWithName (keysId).removeAllChildren (undo);
    }

    std::vector<float> InstrumentState::getManualSlices() const
    {
        std::vector<float> out;
        for (const auto& t : juce::StringArray::fromTokens (tree().getChildWithName (slicesId).getProperty (pointsProp).toString(), " ", ""))
            out.push_back (t.getFloatValue());
        std::sort (out.begin(), out.end());
        return out;
    }

    void InstrumentState::setManualSlices (const std::vector<float>& points, bool undoable)
    {
        juce::StringArray s;
        for (float p : points) s.add (juce::String (std::clamp (p, 0.0f, 1.0f), 5));
        child (slicesId).setProperty (pointsProp, s.joinIntoString (" "), undoable ? undo : nullptr);
    }

    dsp::EnvShape InstrumentState::getMseg (int index) const
    {
        const auto m = tree().getChildWithName (msegsId).getChildWithProperty (idProp, index);
        return dsp::EnvShape::fromString (m.getProperty (pointsProp).toString(), dsp::defaultMseg (index));
    }

    void InstrumentState::setMseg (int index, const dsp::EnvShape& shape, bool undoable)
    {
        auto m = tree().getChildWithName (msegsId).getChildWithProperty (idProp, index);
        if (m.isValid()) m.setProperty (pointsProp, shape.toString(), undoable ? undo : nullptr);
    }

    motion::Pattern InstrumentState::getPattern() const
    {
        return motion::Pattern::fromString (tree().getChildWithName (patternId).getProperty (stepsProp).toString());
    }

    void InstrumentState::setPattern (const motion::Pattern& p, bool undoable)
    {
        child (patternId).setProperty (stepsProp, p.toString(), undoable ? undo : nullptr);
    }

    std::array<float, kMaskBands> InstrumentState::getMask() const
    {
        return maskFromString (tree().getChildWithName (maskId).getProperty (bandsProp).toString());
    }

    void InstrumentState::setMask (const std::array<float, kMaskBands>& m, bool undoable)
    {
        child (maskId).setProperty (bandsProp, maskToString (m), undoable ? undo : nullptr);
    }

    // ---------------------------------------------------------------------------------------------
    // Presets

    void InstrumentState::writeTreatment (juce::XmlElement& xml) const
    {
        for (const auto& id : { keysId, msegsId, patternId, maskId })
            if (auto c = tree().getChildWithName (id); c.isValid())
                if (auto e = c.createXml())
                    xml.addChildElement (e.release());
    }

    void InstrumentState::resetTreatment (juce::ValueTree inst, juce::UndoManager* um, const TreatmentKeep& keep)
    {
        if (! keep.keys)
            inst.getOrCreateChildWithName (keysId, nullptr).removeAllChildren (um);
        auto msegs = inst.getOrCreateChildWithName (msegsId, nullptr);
        for (int i = 0; i < dsp::kNumMsegs; ++i)
            if (auto m = msegs.getChildWithProperty (idProp, i); m.isValid() && ! keep.msegs[(size_t) i])
                m.setProperty (pointsProp, dsp::defaultMseg (i).toString(), um);
        if (! keep.pattern)
            inst.getOrCreateChildWithName (patternId, nullptr).setProperty (stepsProp, motion::Pattern::defaultPattern().toString(), um);
        if (! keep.mask)
        {
            std::array<float, kMaskBands> m;
            m.fill (1.0f);
            inst.getOrCreateChildWithName (maskId, nullptr).setProperty (bandsProp, maskToString (m), um);
        }
    }

    void InstrumentState::applyTreatment (const juce::XmlElement& xml, juce::UndoManager* um, const TreatmentKeep& keep)
    {
        auto inst = tree();
        resetTreatment (inst, um, keep);

        if (auto* keys = xml.getChildByName (keysId); keys != nullptr && ! keep.keys)
        {
            auto node = inst.getChildWithName (keysId);
            for (auto* k : keys->getChildWithTagNameIterator (keyId.toString()))
            {
                const int note = k->getIntAttribute ("note", -1);
                if (! juce::isPositiveAndBelow (note, kNumKeys)) continue;
                auto v = juce::ValueTree::fromXml (*k);
                const auto e = keyFromTree (v);
                if (e.isDefault()) continue;
                juce::ValueTree t (keyId);
                t.setProperty (noteProp, note, nullptr);
                keyToTree (t, e, nullptr);
                node.appendChild (t, um);
            }
        }
        if (auto* msegs = xml.getChildByName (msegsId))
            for (auto* m : msegs->getChildWithTagNameIterator (msegId.toString()))
            {
                const int id = m->getIntAttribute ("id", -1);
                if (juce::isPositiveAndBelow (id, dsp::kNumMsegs) && keep.msegs[(size_t) id]) continue;
                if (auto node = inst.getChildWithName (msegsId).getChildWithProperty (idProp, id); node.isValid())
                    node.setProperty (pointsProp, dsp::EnvShape::fromString (m->getStringAttribute ("points"), dsp::defaultMseg (id)).toString(), um);
            }
        if (auto* p = xml.getChildByName (patternId); p != nullptr && ! keep.pattern)
            inst.getChildWithName (patternId).setProperty (stepsProp, motion::Pattern::fromString (p->getStringAttribute ("steps")).toString(), um);
        if (auto* m = xml.getChildByName (maskId); m != nullptr && ! keep.mask)
            inst.getChildWithName (maskId).setProperty (bandsProp, maskToString (maskFromString (m->getStringAttribute ("bands"))), um);
    }

    // ---------------------------------------------------------------------------------------------
    // Project state

    void InstrumentState::embedAudio (juce::ValueTree& stateCopy) const
    {
        auto samples = stateCopy.getChildWithName (instrumentId).getChildWithName (samplesId);
        for (int i = 0; i < samples.getNumChildren(); ++i)
        {
            auto node = samples.getChild (i);
            const auto s = getSample (node.getProperty (uidProp).toString());
            if (s == nullptr || s->isDemo || ! (bool) node.getProperty (embedProp, true))
                continue;
            juce::MemoryBlock block;
            {
                juce::FlacAudioFormat flac;
                auto* stream = new juce::MemoryOutputStream (block, false);
                std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (stream, s->sampleRate, (unsigned int) s->numChannels, 24, {}, 0));
                if (writer == nullptr) { delete stream; continue; }
                const float* chans[] = { s->left.data(), s->right.empty() ? s->left.data() : s->right.data() };
                writer->writeFromFloatArrays (chans, s->numChannels, s->numFrames);
            }
            node.setProperty (audioProp, block.toBase64Encoding(), nullptr);
        }
    }

    void InstrumentState::restoreAudio()
    {
        auto samples = tree().getChildWithName (samplesId);
        std::set<juce::String> wanted;
        for (int i = 0; i < samples.getNumChildren(); ++i)
        {
            auto node = samples.getChild (i);
            const auto uid = node.getProperty (uidProp).toString();
            wanted.insert (uid);
            const auto audio = node.getProperty (audioProp).toString();
            node.removeProperty (audioProp, nullptr);   // keep the live state light
            if (getSample (uid) != nullptr) continue;

            const auto name = node.getProperty (nameProp).toString();
            const juce::File file (node.getProperty (pathProp).toString());
            pendingLoads.fetch_add (1);
            auto token = alive;
            pool.addJob ([this, token, uid, name, file, audio]
            {
                juce::String err;
                SamplePtr s;
                if (audio.isNotEmpty())
                {
                    juce::MemoryBlock block;
                    if (block.fromBase64Encoding (audio))
                    {
                        juce::FlacAudioFormat flac;
                        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, false), true));
                        if (reader != nullptr) s = readFromReader (*reader, name, err);
                    }
                }
                if (s == nullptr && file.existsAsFile())
                    s = decodeFile (file, err);
                if (s != nullptr)
                {
                    s->uid = uid;
                    s->name = name;
                    if (file.existsAsFile()) s->file = file;
                    if (token->load())
                    {
                        const juce::ScopedLock sl (bankLock);
                        bank[uid] = s;
                    }
                }
                else if (token->load())
                    setError ("A sample of this project could not be restored (" + name + ").");
                pendingLoads.fetch_sub (1);
                if (token->load()) triggerAsyncUpdate();
            });
        }
        {
            const juce::ScopedLock sl (bankLock);
            for (auto it = bank.begin(); it != bank.end();)
                if (wanted.count (it->first) == 0) { trash.push_back (it->second); it = bank.erase (it); }
                else ++it;
        }
        rebuildNow();
    }

    void InstrumentState::waitForLoads()
    {
        while (pendingLoads.load() > 0)
            juce::Thread::sleep (5);
        DemoVoices::get().waitUntilReady();
        rebuildNow();
    }

    // ---------------------------------------------------------------------------------------------
    // Snapshot

    void InstrumentState::rebuildNow()
    {
        auto snap = std::make_unique<Snapshot>();
        snap->version = ++version;
        snap->loading = pendingLoads.load() > 0;

        auto zonesInfo = getZones();
        {
            const juce::ScopedLock sl (bankLock);
            for (auto& [uid, s] : bank) snap->keepAlive.push_back (s);
            for (const auto& z : zonesInfo)
            {
                const auto it = bank.find (z.sample);
                if (it == bank.end()) continue;
                Snapshot::Zone zone;
                zone.sample = it->second.get();
                zone.lo = z.lo; zone.hi = z.hi; zone.vlo = z.vlo; zone.vhi = z.vhi;
                zone.root = z.root; zone.tune = z.tune;
                zone.gain = juce::Decibels::decibelsToGain (z.gainDb);
                zone.pan = z.pan;
                zone.engine = z.engine;
                snap->zones.push_back (zone);
            }
            if (auto it = bank.find (getActiveUid()); it != bank.end())
                snap->chopSample = it->second.get();
        }
        if (snap->chopSample == nullptr && ! snap->zones.empty())
            snap->chopSample = snap->zones.front().sample;

        const int demoIndex = juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::DemoVoice))->load());
        if (snap->zones.empty() && ! snap->loading && zonesInfo.empty())
        {
            if (auto demo = DemoVoices::get().voice (demoIndex))
            {
                snap->usingDemo = true;
                snap->keepAlive.push_back (demo);
                Snapshot::Zone zone;
                zone.sample = demo.get();
                zone.root = demo->rootNote;
                snap->zones.push_back (zone);
                snap->chopSample = demo.get();
            }
        }

        // Slices of the active sample (cached)
        const int mode = juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::ChopMode))->load());
        const float sens = apvts.getRawParameterValue (params::paramId (params::ChopSens))->load();
        const int count = juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::ChopCount))->load());
        watchedParams = { (float) mode, sens, (float) count, (float) demoIndex };
        if (snap->chopSample != nullptr)
        {
            const auto manual = getManualSlices();
            juce::String key = snap->chopSample->uid + "|" + juce::String (mode) + "|" + juce::String (sens, 3) + "|" + juce::String (count);
            if (mode == 3) for (float p : manual) key << " " << juce::String (p, 5);
            if (key != sliceKey)
            {
                sliceKey = key;
                sliceCache = analysis::makeSlices (*snap->chopSample, mode, sens, count, manual);
            }
            snap->slices = sliceCache;
        }

        for (const auto k : tree().getChildWithName (keysId))
        {
            const int note = k.getProperty (noteProp, -1);
            if (juce::isPositiveAndBelow (note, kNumKeys))
                snap->keys[(size_t) note] = keyFromTree (k);
        }
        snap->mask = getMask();
        for (int i = 0; i < dsp::kNumMsegs; ++i)
            snap->msegs[(size_t) i] = getMseg (i);
        snap->pattern = getPattern();
        publish (std::move (snap));
    }

    void InstrumentState::publish (std::unique_ptr<Snapshot> snap)
    {
        if (auto* old = current.exchange (snap.release(), std::memory_order_acq_rel))
            retired.emplace_back (std::unique_ptr<Snapshot> (old), juce::Time::getMillisecondCounterHiRes());
        collectGarbage (false);
        sendChangeMessage();
    }

    void InstrumentState::collectGarbage (bool force)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        // A block never takes longer than this, so the audio thread no longer uses older snapshots
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [&] (const auto& r) { return force || now - r.second > 2000.0; }),
                       retired.end());
        const juce::ScopedLock sl (bankLock);
        trash.erase (std::remove_if (trash.begin(), trash.end(),
                                     [] (const SamplePtr& s) { return s.use_count() == 1 && s->users.load() == 0; }),
                     trash.end());
    }

    void InstrumentState::timerCallback()
    {
        const auto& snap = snapshot();
        const int demoIndex = juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::DemoVoice))->load());
        const std::array<float, 4> now {
            (float) juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::ChopMode))->load()),
            apvts.getRawParameterValue (params::paramId (params::ChopSens))->load(),
            (float) juce::roundToInt (apvts.getRawParameterValue (params::paramId (params::ChopCount))->load()),
            (float) demoIndex };
        const bool demoNowReady = snap.zones.empty() && ! snap.loading && DemoVoices::get().isReady() && getZones().empty();
        if (now != watchedParams || demoNowReady)
            rebuildNow();
        else
            collectGarbage (false);
    }
}
