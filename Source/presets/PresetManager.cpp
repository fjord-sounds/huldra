#include "PresetManager.h"
#include "Locks.h"
#include <fjord_base/fx/FxParams.h>

namespace huldra
{
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, InstrumentState& inst, juce::UndoManager* um)
        : PresetManagerBase (s, um, { ".huldra", "HuldraPreset", true, "Load motion patterns with presets", { "master_gain" } }),
          instrument (inst)
    {
        setLocks (locks::set());   // locked parts (Locks.h) keep their values when a preset is loaded
        initialise();
    }

    juce::String PresetManager::normaliseOrder (const juce::String& text)
    {
        namespace fx = fjord::base::fx;
        return fx::orderToString (fx::orderFromString (text, fx::defaultOrder()));
    }

    void PresetManager::applyExtras (const juce::XmlElement& xml, bool isInit)
    {
        // Keys, MSEGs, mask always (unless locked); the pattern only when wanted and not locked
        TreatmentKeep keep;
        keep.keys = isLockedProperty ("keysLock");
        keep.mask = isLockedProperty ("spectralLock");
        keep.pattern = isLockedProperty ("patternLock") || (! isInit && ! getLoadPatterns());
        for (int m = 0; m < dsp::kNumMsegs; ++m) keep.msegs[(size_t) m] = isLockedProperty (locks::msegProperty (m));
        instrument.applyTreatment (xml, undo, keep);
        instrument.rebuildNow();
    }

    void PresetManager::writeExtras (juce::XmlElement& xml) const
    {
        instrument.writeTreatment (xml);
    }
}
