#pragma once
#include <fjord_base/presets/PresetManagerBase.h>
#include "../state/InstrumentState.h"

namespace huldra
{
    // Presets are XML files (.huldra) with plain values and the "treatment" (per-key edits, MSEGs, pattern, mask):
    //   <HuldraPreset name="Paper Moon" category="Pads" author="Fjord" tags="Pad,Airy" macros="..." fxOrder="...">
    //     <Param id="engine" value="1"/> … <Keys/> <Msegs/> <Pattern/> <Mask/>
    // A preset never contains audio: it is applied to whatever the user has loaded or recorded (or to a demo voice).
    class PresetManager : public fjord::base::presets::PresetManagerBase
    {
    public:
        PresetManager (juce::AudioProcessorValueTreeState&, InstrumentState&, juce::UndoManager* = nullptr);

        static juce::String normaliseOrder (const juce::String&);

    protected:
        void applyExtras (const juce::XmlElement&, bool isInit) override;
        void writeExtras (juce::XmlElement&) const override;
        juce::String normaliseFxOrder (const juce::String& order) const override { return normaliseOrder (order); }

    private:
        InstrumentState& instrument;
    };
}
