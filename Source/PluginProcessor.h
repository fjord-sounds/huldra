#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Synth.h"
#include "motion/MotionEngine.h"
#include "fx/FxChain.h"
#include "state/InstrumentState.h"
#include "presets/PresetManager.h"
#include "sample/Recorder.h"

namespace huldra
{
    // Ring buffer of the output for the displays.
    struct ScopeBuffer
    {
        static constexpr int size = 32768;
        std::array<float, size> left {}, right {};
        std::atomic<int> writePos { 0 };

        void push (const float* l, const float* r, int n)
        {
            int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                left[(size_t) w] = l[i];
                right[(size_t) w] = r[i];
                w = (w + 1) & (size - 1);
            }
            writePos.store (w, std::memory_order_release);
        }

        void copyLatest (float* l, float* r, int n) const
        {
            const int end = writePos.load (std::memory_order_acquire);
            for (int i = 0; i < n; ++i)
            {
                const auto idx = (size_t) ((end - n + i + size * 4) & (size - 1));
                if (r == nullptr) l[i] = 0.5f * (left[idx] + right[idx]);
                else { l[i] = left[idx]; r[i] = right[idx]; }
            }
        }
    };
}

class HuldraProcessor : public juce::AudioProcessor,
                        private juce::ValueTree::Listener
{
public:
    HuldraProcessor();
    ~HuldraProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    double getTailLengthSeconds() const override { return 10.0; }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    huldra::InstrumentState instrument { apvts, &undoManager };
    huldra::PresetManager presetManager { apvts, instrument, &undoManager };
    juce::MidiKeyboardState keyboardState;
    huldra::ScopeBuffer scope;
    huldra::fx::FxMeters fxMeters;
    huldra::dsp::VoiceDisplay voiceDisplay;
    huldra::Recorder recorder;
    std::atomic<float> uiPitchBend { 0.0f }, uiModWheel { 0.0f };   // the on-screen wheels

    huldra::fx::Order getFxOrder() const;
    void setFxOrder (const huldra::fx::Order&);
    void requestPanic() { panicRequested.store (true); }

    // Internal transport for the motion engine when the host is stopped
    bool isHostPlaying() const { return hostPlaying.load(); }
    int getCurrentStep() const { return motion.getCurrentStep(); }
    const huldra::motion::MotionEngine& getMotion() const { return motion; }
    double getCurrentSampleRate() const { return currentSampleRate; }
    double getBpm() const { return bpm.load(); }

    // The pad position after drift (what the sound uses right now)
    float getLivePadX() const { return livePadX.load(); }
    float getLivePadY() const { return livePadY.load(); }

    std::array<float, 2> takeOutputPeaks()
    {
        return { outputPeak[0].exchange (0.0f, std::memory_order_relaxed), outputPeak[1].exchange (0.0f, std::memory_order_relaxed) };
    }

    huldra::params::Values currentValues() const;

    // Recording and resampling (message thread)
    void startRecording (bool waitForSignal);
    void stopRecording();          // the take becomes the new sound
    // Renders 'seconds' of a held note through the whole sound (engines, motion, effects) into a new sample.
    void bounce (int note, double seconds, std::function<void (bool)> done);
    bool isBouncing() const { return bouncing.load(); }

    // A/B comparison
    void storeSnapshot (int slot);
    void recallSnapshot (int slot);
    bool hasSnapshot (int slot) const { return snapshots[(size_t) (slot & 1)] != nullptr; }
    int getActiveSnapshot() const { return activeSnapshot; }
    void setActiveSnapshot (int slot) { activeSnapshot = slot; }

    juce::String getDebugReport() const;

    // Offline rendering used by the bounce and the tests: renders into 'out' with fresh DSP objects.
    static void renderOffline (const huldra::params::Values&, const huldra::Snapshot&, const huldra::fx::Order&,
                               double sampleRate, int note, float velocity, double holdSeconds, juce::AudioBuffer<float>& out);

private:
    void processChunk (juce::AudioBuffer<float>&, juce::MidiBuffer&, double ppq, bool playing);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void syncFxOrderFromState();
    void applyPadDrift (huldra::params::Values&, int numSamples);

    huldra::params::Reader reader { apvts };
    huldra::params::Values values, liveValues;
    huldra::dsp::Synth synth;
    huldra::motion::MotionEngine motion;
    huldra::fx::FxChain fxChain;
    std::array<std::atomic<int>, huldra::fx::kNumFx> fxOrder {};

    static constexpr int kMaxEvents = 512;
    std::array<huldra::dsp::NoteEvent, kMaxEvents> inEvents {}, outEvents {};

    double currentSampleRate = 44100.0, freePpq = 0.0, driftPhase = 0.0;
    int maxBlockSize = 512;
    std::atomic<bool> panicRequested { false }, hostPlaying { false }, bouncing { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> livePadX { 0.5f }, livePadY { 0.5f };
    std::array<std::atomic<float>, 2> outputPeak {};
    juce::SmoothedValue<float> masterGain, presetLevel;
    std::array<float, 2> dcX {}, dcY {};
    float dcCoef = 0.9995f, lastUiBend = 0.0f, lastUiMod = 0.0f;
    int bounceCount = 0;
    juce::ThreadPool bouncePool { 1 };

    std::array<std::unique_ptr<juce::XmlElement>, 2> snapshots;
    int activeSnapshot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HuldraProcessor)
};
