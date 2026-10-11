#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace P = huldra::params;
using huldra::dsp::NoteEvent;

HuldraProcessor::HuldraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "HuldraState", P::createLayout())
{
    P::modTargets();   // build the static tables here, never on the audio thread
    huldra::InstrumentState::ensureChildren (apvts.state);
    if (! apvts.state.hasProperty ("fxOrder"))
        apvts.state.setProperty ("fxOrder", huldra::fx::orderToString (huldra::fx::defaultOrder()), nullptr);
    apvts.state.addListener (this);
    syncFxOrderFromState();
}

HuldraProcessor::~HuldraProcessor()
{
    bouncePool.removeAllJobs (true, 20000);
    apvts.state.removeListener (this);
}

void HuldraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    synth.prepare (sampleRate, maxBlockSize);
    motion.prepare (sampleRate);
    fxChain.prepare (sampleRate, maxBlockSize);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::MasterGain))->load()));
    presetLevel.reset (sampleRate, 0.02);
    presetLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::PresetLevel))->load()));
    dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
    dcX.fill (0.0f);
    dcY.fill (0.0f);
}

bool HuldraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void HuldraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // The input is only used for recording; it is read before the buffer becomes the output.
    const int inputs = getTotalNumInputChannels();
    if (inputs > 0 && buffer.getNumChannels() > 0)
        recorder.push (buffer.getReadPointer (0), inputs > 1 && buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr,
                       buffer.getNumSamples());
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (panicRequested.exchange (false))
    {
        midi.clear();
        synth.stopAll();
        motion.reset();
        fxChain.reset();
    }

    double ppq = 0.0, tempo = 120.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            tempo = pos->getBpm().orFallback (0.0);
            ppq = pos->getPpqPosition().orFallback (0.0);
        }
    hostPlaying.store (playing);
    if (! (tempo > 1.0 && tempo < 2000.0))
        tempo = apvts.getRawParameterValue (P::paramId (P::MotBpm))->load();
    bpm.store (tempo);

    const int total = buffer.getNumSamples();
    if (total <= maxBlockSize)
        return processChunk (buffer, midi, ppq, playing);

    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = std::min (maxBlockSize, total - start);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), std::min (2, buffer.getNumChannels()), start, n);
        juce::MidiBuffer partMidi;
        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + n)
                partMidi.addEvent (meta.getMessage(), meta.samplePosition - start);
        processChunk (part, partMidi, ppq + tempo / 60.0 * start / currentSampleRate, playing);
    }
    midi.clear();
}

void HuldraProcessor::applyPadDrift (P::Values& v, int numSamples)
{
    const float drift = v[P::PadDrift];
    if (drift > 0.001f)
    {
        // A slow Lissajous figure around the pad position
        driftPhase += (double) v[P::PadDriftRate] * numSamples / currentSampleRate;
        const double a = juce::MathConstants<double>::twoPi * driftPhase;
        v[P::PadX] = std::clamp (v[P::PadX] + drift * 0.45f * (float) std::sin (a), 0.0f, 1.0f);
        v[P::PadY] = std::clamp (v[P::PadY] + drift * 0.45f * (float) std::sin (a * 1.37 + 0.6), 0.0f, 1.0f);
    }
    livePadX.store (v[P::PadX], std::memory_order_relaxed);
    livePadY.store (v[P::PadY], std::memory_order_relaxed);
}

void HuldraProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double ppq, bool playing)
{
    const int n = buffer.getNumSamples();
    reader.read (values);
    applyPadDrift (values, n);
    const auto* snap = instrument.acquire();

    // Notes and expression from MIDI (the on-screen keyboard is merged in by the keyboard state)
    int numIn = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numIn < kMaxEvents)
            inEvents[(size_t) numIn++] = { meta.samplePosition, NoteEvent::On, m.getNoteNumber(), m.getFloatVelocity(), m.getChannel() };
        else if (m.isNoteOff() && numIn < kMaxEvents)
            inEvents[(size_t) numIn++] = { meta.samplePosition, NoteEvent::Off, m.getNoteNumber(), 0.0f, m.getChannel() };
        else if ((m.isAllNotesOff() || m.isAllSoundOff()) && numIn < kMaxEvents)
            inEvents[(size_t) numIn++] = { meta.samplePosition, NoteEvent::AllOff, 0, 0.0f, m.getChannel() };
        else
            synth.handleExpression (m);
    }
    midi.clear();
    if (const float bend = uiPitchBend.load (std::memory_order_relaxed); bend != lastUiBend)
    {
        lastUiBend = bend;
        synth.handleExpression (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) (8192.0f + bend * 8191.0f))));
    }
    if (const float mw = uiModWheel.load (std::memory_order_relaxed); mw != lastUiMod)
    {
        lastUiMod = mw;
        synth.handleExpression (juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, (int) (mw * 127.0f))));
    }

    huldra::motion::MotionEngine::Clock clock;
    clock.beatsPerSample = bpm.load() / 60.0 / currentSampleRate;
    clock.hostPlaying = playing;
    clock.ppq = ppq;
    const bool chops = values.i (P::PlayMode) == 1;
    const auto& pattern = snap != nullptr ? snap->pattern : huldra::motion::Pattern {};
    const int numOut = motion.process (inEvents.data(), numIn, outEvents.data(), n, values, clock, pattern, chops);

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    if (snap != nullptr && ! snap->loading)
        synth.process (l, r, n, values, *snap, outEvents.data(), numOut, bpm.load(), ppq, playing, motion.getLane(), &voiceDisplay);

    liveValues = values;
    synth.applyGlobalModulation (liveValues);

    presetLevel.setTargetValue (juce::Decibels::decibelsToGain (values[P::PresetLevel]));
    presetLevel.applyGain (buffer, n);

    huldra::fx::Order order;
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = fxOrder[i].load (std::memory_order_relaxed);
    fxChain.process ({ l, r, n }, liveValues, order, bpm.load(), &fxMeters);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (values[P::MasterGain]));
    masterGain.applyGain (buffer, n);

    // DC blocker (5 Hz) and a last safety stage above -0.3 dBFS
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float in = d[i];
            dcY[(size_t) ch] = in - dcX[(size_t) ch] + dcCoef * dcY[(size_t) ch];
            dcX[(size_t) ch] = in;
            const float x = dcY[(size_t) ch];
            d[i] = std::abs (x) > 0.97f ? std::copysign (0.97f + 0.02f * huldra::dsp::softClip ((std::abs (x) - 0.97f) / 0.02f), x) : x;
        }
    }

    scope.push (l, r, n);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, n);
        auto& slot = outputPeak[(size_t) ch];
        float prev = slot.load (std::memory_order_relaxed);
        while (peak > prev && ! slot.compare_exchange_weak (prev, peak, std::memory_order_relaxed)) {}
    }
}

void HuldraProcessor::renderOffline (const P::Values& v, const huldra::Snapshot& snap, const huldra::fx::Order& order,
                                     double sampleRate, int note, float velocity, double holdSeconds, juce::AudioBuffer<float>& out)
{
    constexpr int block = 256;
    auto synth = std::make_unique<huldra::dsp::Synth>();
    auto motion = std::make_unique<huldra::motion::MotionEngine>();
    auto fx = std::make_unique<huldra::fx::FxChain>();
    synth->prepare (sampleRate, block);
    motion->prepare (sampleRate);
    fx->prepare (sampleRate, block);
    out.clear();
    const int total = out.getNumSamples();
    const int offAt = (int) (holdSeconds * sampleRate);
    std::array<NoteEvent, 4> in {};
    std::vector<NoteEvent> evOut ((size_t) huldra::motion::MotionEngine::kMaxOut);
    double ppq = 0.0;
    const double bps = v[P::MotBpm] / 60.0 / sampleRate;
    const bool chops = v.i (P::PlayMode) == 1;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        int numIn = 0;
        if (pos == 0) in[(size_t) numIn++] = { 0, NoteEvent::On, note, velocity, 1 };
        if (offAt >= pos && offAt < pos + n) in[(size_t) numIn++] = { offAt - pos, NoteEvent::Off, note, 0.0f, 1 };
        huldra::motion::MotionEngine::Clock clock;
        clock.beatsPerSample = bps;
        clock.ppq = ppq;
        clock.hostPlaying = false;
        const int numOut = motion->process (in.data(), numIn, evOut.data(), n, v, clock, snap.pattern, chops);
        auto* l = out.getWritePointer (0) + pos;
        auto* r = out.getWritePointer (1) + pos;
        synth->process (l, r, n, v, snap, evOut.data(), numOut, v[P::MotBpm], ppq, false, motion->getLane(), nullptr);
        auto live = v;
        synth->applyGlobalModulation (live);
        const float level = juce::Decibels::decibelsToGain (v[P::PresetLevel]);
        for (int i = 0; i < n; ++i) { l[i] *= level; r[i] *= level; }
        fx->process ({ l, r, n }, live, order, v[P::MotBpm], nullptr);
        ppq += bps * n;
    }
}

void HuldraProcessor::startRecording (bool waitForSignal)
{
    recorder.arm (currentSampleRate > 0.0 ? currentSampleRate : 48000.0, waitForSignal);
}

void HuldraProcessor::stopRecording()
{
    recorder.stop();
    double rate = 48000.0;
    auto take = recorder.takeRecording (rate);
    if (take.getNumSamples() == 0) return;
    static int takeCount = 0;
    auto sample = huldra::InstrumentState::fromBuffer (take, rate, "Recording " + juce::String (++takeCount));
    instrument.addSample (sample, huldra::InstrumentState::AddMode::Replace);
}

void HuldraProcessor::bounce (int note, double seconds, std::function<void (bool)> done)
{
    if (bouncing.exchange (true)) return;
    const auto v = currentValues();
    const auto order = getFxOrder();
    const double rate = currentSampleRate > 0.0 ? currentSampleRate : 48000.0;
    auto snapCopy = std::make_shared<huldra::Snapshot> (instrument.snapshot());
    const int number = ++bounceCount;
    bouncePool.addJob ([this, v, order, rate, snapCopy, note, seconds, number, done]
    {
        const double tail = std::min (8.0, (double) v[P::AmpRelease] + (v.b (P::VerbOn) ? v[P::VerbDecay] * 0.6 : 0.0) + 0.2);
        juce::AudioBuffer<float> out (2, (int) ((seconds + tail) * rate));
        renderOffline (v, *snapCopy, order, rate, note, 0.85f, seconds, out);
        const float peak = out.getMagnitude (0, out.getNumSamples());
        if (peak > 0.0f) out.applyGain (0.89f / peak);
        juce::MessageManager::callAsync ([this, out, rate, number, done]
        {
            double r = rate;
            auto sample = huldra::InstrumentState::fromBuffer (out, r, "Bounce " + juce::String (number));
            const bool ok = sample != nullptr && sample->peak > 0.0f;
            if (ok) instrument.addSample (sample, huldra::InstrumentState::AddMode::Replace);
            bouncing.store (false);
            if (done) done (ok);
        });
    });
}

P::Values HuldraProcessor::currentValues() const
{
    P::Values v;
    for (const auto& s : P::specs())
        v[s.id] = apvts.getRawParameterValue (s.paramId)->load();
    return v;
}

huldra::fx::Order HuldraProcessor::getFxOrder() const
{
    huldra::fx::Order o;
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = fxOrder[i].load();
    return o;
}

void HuldraProcessor::setFxOrder (const huldra::fx::Order& o)
{
    apvts.state.setProperty ("fxOrder", huldra::fx::orderToString (o), &undoManager);
}

void HuldraProcessor::syncFxOrderFromState()
{
    const auto o = huldra::fx::orderFromString (apvts.state.getProperty ("fxOrder").toString());
    for (size_t i = 0; i < o.size(); ++i)
        fxOrder[i].store (o[i]);
}

void HuldraProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == apvts.state && id == juce::Identifier ("fxOrder"))
        syncFxOrderFromState();
}

void HuldraProcessor::storeSnapshot (int slot)
{
    snapshots[(size_t) (slot & 1)] = presetManager.createPresetXml (presetManager.getCurrentName(), presetManager.getCurrentCategory());
}

void HuldraProcessor::recallSnapshot (int slot)
{
    if (auto& xml = snapshots[(size_t) (slot & 1)])
    {
        const auto name = xml->getStringAttribute ("name");
        presetManager.applyXml (*xml, true);
        apvts.state.setProperty ("presetName", name, nullptr);
    }
}

int HuldraProcessor::getNumPrograms()
{
    int n = 0;
    for (const auto& p : presetManager.getPresets())
        n += p.isFactory ? 1 : 0;
    return std::max (1, n);
}

int HuldraProcessor::getCurrentProgram()
{
    return std::max (0, presetManager.getCurrentIndex());
}

void HuldraProcessor::setCurrentProgram (int index)
{
    if (index != presetManager.getCurrentIndex() && index < getNumPrograms())
        presetManager.load (index);
}

const juce::String HuldraProcessor::getProgramName (int index)
{
    const auto& list = presetManager.getPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String ("Init");
}

juce::String HuldraProcessor::getDebugReport() const
{
   #ifdef JucePlugin_VersionString
    juce::String version = JucePlugin_VersionString;
   #else
    juce::String version = "dev";
   #endif
   #if JUCE_ARM
    const juce::String arch = "arm64";
   #elif JUCE_64BIT
    const juce::String arch = "x86_64";
   #else
    const juce::String arch = "32-bit";
   #endif

    juce::StringArray lines;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto add = [&] (const juce::String& key, const juce::String& value)
    {
        lines.add (key.paddedRight (' ', 16) + (home.length() > 1 ? value.replace (home, "~") : value));
    };

    lines.add ("Huldra debug info");
    lines.add ("-----------------");
   #ifdef HULDRA_GIT_HASH
    const juce::String build = juce::String (HULDRA_GIT_HASH) + ", ";
   #else
    const juce::String build;
   #endif
    add ("Version", version + " (" + build + arch + ", built " + __DATE__ + ")");
    add ("Format", juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    add ("Host", juce::PluginHostType().getHostDescription());
    add ("Plugin", juce::File::getSpecialLocation (juce::File::currentApplicationFile).getFullPathName());
    add ("User presets", huldra::PresetManager::getUserPresetFolder().getFullPathName());
    add ("Settings", huldra::ui::Theme::get().getSettingsFile().getFullPathName());
    add ("Sample rate", juce::String (getSampleRate(), 0) + " Hz, block " + juce::String (getBlockSize())
                        + " (prepared " + juce::String (maxBlockSize) + ")");
    add ("OS", juce::SystemStats::getOperatingSystemName() + (juce::SystemStats::isOperatingSystem64Bit() ? " 64-bit" : ""));
    add ("CPU", juce::SystemStats::getCpuModel().trim() + ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores");
    add ("JUCE", juce::SystemStats::getJUCEVersion());
    add ("Theme / scale", juce::String (huldra::ui::Theme::get().isDark() ? "dark" : "light") + ", "
                          + juce::String (juce::roundToInt (huldra::ui::Theme::get().getScale() * 100.0f)) + " %");

    int factory = 0, user = 0;
    for (const auto& p : presetManager.getPresets())
        (p.isFactory ? factory : user)++;
    add ("Presets", juce::String (factory) + " factory, " + juce::String (user) + " user");
    add ("Preset", presetManager.getCurrentName() + " (" + presetManager.getCurrentCategory() + ")");
    juce::StringArray sampleNames;
    for (const auto& s : instrument.getSamples())
        sampleNames.add (s->name + " (" + juce::String (s->seconds(), 1) + " s, " + juce::String (s->sampleRate, 0) + " Hz)");
    add ("Samples", sampleNames.isEmpty() ? juce::String ("demo voice") : sampleNames.joinIntoString (", "));
    add ("FX order", apvts.state.getProperty ("fxOrder").toString());

    juce::StringArray changed;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-5f)
                changed.add (rp->getParameterID() + "=" + juce::String (rp->convertFrom0to1 (rp->getValue()), 4));
    lines.add ("");
    lines.add ("Changed parameters (" + juce::String (changed.size()) + "):");
    lines.add (changed.joinIntoString (" "));
    return lines.joinIntoString ("\n");
}

juce::AudioProcessorEditor* HuldraProcessor::createEditor()
{
    return new HuldraEditor (*this);
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted, and add the conversion to migrateState().
    constexpr int kStateVersion = 1;

    void migrateState (juce::ValueTree& state)
    {
        state.setProperty ("stateVersion", kStateVersion, nullptr);
    }
}

void HuldraProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);
    apvts.state.setProperty ("darkTheme", huldra::ui::Theme::get().isDark(), nullptr);
    auto copy = apvts.copyState();
    instrument.embedAudio (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, dest);
}

void HuldraProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            huldra::InstrumentState::ensureChildren (tree);
            apvts.replaceState (tree);
            migrateState (apvts.state);
            instrument.restoreAudio();
            presetManager.restoreFromState();
            syncFxOrderFromState();
            undoManager.clearUndoHistory();

            if (apvts.state.hasProperty ("darkTheme"))
            {
                const bool dark = apvts.state.getProperty ("darkTheme");
                juce::MessageManager::callAsync ([dark] { huldra::ui::Theme::get().setDark (dark); });
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HuldraProcessor();
}
