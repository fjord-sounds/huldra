#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace P = huldra::params;

// Huldra inside the family design: a soft rose, the veil gradient (rose -> lilac -> ice blue).
const fjord::base::Brand& fjord::base::brand()
{
    static const Brand b = []
    {
        Brand x;
        x.name = "Huldra";
        x.logo = "huldra";
        x.tagline = "SOUND DESIGN SAMPLER";
        x.githubUrl = "https://github.com/fjord-sounds/huldra";
        x.settingsFolder = "Fjord/Huldra";
        x.userPresetFolder = "Fjord/Huldra/Presets";
        x.light = familyLight (juce::Colour (0xffb0545a));   // deep rose
        x.dark = familyDark (juce::Colour (0xfff29c9c));     // glowing rose
        x.gradientDark = { juce::Colour (0xfff29c9c), juce::Colour (0xffb49cf2), juce::Colour (0xff7fc4e8) };
        x.gradientLight = { juce::Colour (0xffb0545a), juce::Colour (0xff7a5cc0), juce::Colour (0xff2f7fb0) };
        x.glow = { juce::Colour (0xfff29c9c), juce::Colour (0xffb49cf2), juce::Colour (0xff7fc4e8) };
        x.glowDark = 0.13f;
        x.glowLight = 0.08f;
        x.flash = juce::Colour (0xfffbe4ea);
        x.categories = { "Pads", "Keys", "Plucks", "Leads & Bass", "Choirs", "Voices", "Vocal Chops", "Textures", "Motion",
                         "Risers & FX", "Experimental" };
        x.tags = { "Pad", "Keys", "Pluck", "Lead", "Bass", "Chops", "Choir", "Texture", "Airy", "Warm", "Dark", "Bright",
                   "Intimate", "Wide", "Lo-Fi", "Evolving", "Rhythmic", "Autoplay", "Arp", "Granular", "Spectral", "Vocal",
                   "Glitch", "Cinematic" };
        return x;
    }();
    return b;
}
using huldra::dsp::NoteEvent;

HuldraProcessor::HuldraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "HuldraState", P::createLayout())
{
    P::modTargets();   // build the static tables here, never on the audio thread
    huldra::InstrumentState::ensureChildren (apvts.state);
}

HuldraProcessor::~HuldraProcessor()
{
    bouncePool.removeAllJobs (true, 20000);
}

void HuldraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    synth.prepare (sampleRate, maxBlockSize);
    motion.prepare (sampleRate);
    fxChain.prepare (sampleRate, maxBlockSize);
    splitter.prepare (maxBlockSize);
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

    const auto transport = fjord::base::plugin::Transport::read (*this, apvts.getRawParameterValue (P::paramId (P::MotBpm))->load());
    hostPlaying.store (transport.playing);
    bpm.store (transport.bpm);

    splitter.process (buffer, midi, [&] (juce::AudioBuffer<float>& part, juce::MidiBuffer& partMidi, int offset)
    {
        processChunk (part, partMidi, transport.ppq + transport.bpm / 60.0 * offset / currentSampleRate, transport.playing);
    });
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

    // New notes restart the effect filter's envelope
    int numTriggers = 0;
    for (int i = 0; i < numOut && numTriggers < (int) triggerPositions.size(); ++i)
        if (outEvents[(size_t) i].type == NoteEvent::On)
            triggerPositions[(size_t) numTriggers++] = outEvents[(size_t) i].sample;
    fjord::base::fx::Context ctx;
    ctx.bpm = bpm.load();
    ctx.ppq = ppq;
    ctx.playing = playing;
    ctx.triggers = triggerPositions.data();
    ctx.numTriggers = numTriggers;
    fxChain.process ({ l, r, n }, P::fxValues (liveValues), fxOrder.get(), ctx, &fxMeters);

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
    outputPeaks.push (buffer, 0, n);
}

void HuldraProcessor::renderOffline (const P::Values& v, const huldra::Snapshot& snap, const fjord::base::fx::Order& order,
                                     double sampleRate, int note, float velocity, double holdSeconds, juce::AudioBuffer<float>& out)
{
    constexpr int block = 256;
    auto synth = std::make_unique<huldra::dsp::Synth>();
    auto motion = std::make_unique<huldra::motion::MotionEngine>();
    auto fx = std::make_unique<fjord::base::fx::Chain>();
    std::vector<int> triggers ((size_t) huldra::motion::MotionEngine::kMaxOut);
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
        int numTriggers = 0;
        for (int i = 0; i < numOut && numTriggers < (int) triggers.size(); ++i)
            if (evOut[(size_t) i].type == NoteEvent::On) triggers[(size_t) numTriggers++] = evOut[(size_t) i].sample;
        fjord::base::fx::Context ctx;
        ctx.bpm = v[P::MotBpm];
        ctx.ppq = ppq;
        ctx.triggers = triggers.data();
        ctx.numTriggers = numTriggers;
        fx->process ({ l, r, n }, P::fxValues (live), order, ctx, nullptr);
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
        const double tail = std::min (8.0, (double) v[P::AmpRelease] + (v.b (P::FxVerbOn) ? v[P::FxVerbDecay] * 0.6 : 0.0) + 0.2);
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
        v.v[(size_t) s.id] = apvts.getRawParameterValue (s.paramId)->load();
    return v;
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
    fjord::base::plugin::DebugReport report (*this, const_cast<huldra::PresetManager*> (&presetManager), maxBlockSize);
    juce::StringArray sampleNames;
    for (const auto& s : instrument.getSamples())
        sampleNames.add (s->name + " (" + juce::String (s->seconds(), 1) + " s, " + juce::String (s->sampleRate, 0) + " Hz)");
    report.add ("Samples", sampleNames.isEmpty() ? juce::String ("demo voice") : sampleNames.joinIntoString (", "));
    report.add ("FX order", apvts.state.getProperty ("fxOrder").toString());
    return report.finish();
}

juce::AudioProcessorEditor* HuldraProcessor::createEditor()
{
    return new HuldraEditor (*this);
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted (fjord-base/source/fjord_base/presets/Migration.h helps), and convert it in setStateInformation.
    constexpr int kStateVersion = 1;
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
            instrument.restoreAudio();
            presetManager.restoreFromState();
            fxOrder.sync();
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
