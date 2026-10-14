#pragma once
#include "Theme.h"
#include "../Parameters.h"
#include "../presets/Locks.h"

namespace huldra::ui
{
    namespace P = params;

    // A padlock in the title row of a panel (drawPanel): the part of the sound the panel shows stays when another sound
    // preset is loaded. The group is one of locks::ids() (see presets/Locks.h).
    class PanelLock : public LockButton
    {
    public:
        PanelLock (APVTS& state, int group) : LockButton (state, locks::set(), group, false) {}

        // Puts the lock at the right end of the panel's title row
        void place (juce::Rectangle<int> panel)
        {
            setBounds (panel.reduced (16, 0).removeFromTop (30).removeFromRight (18).withSizeKeepingCentre (18, 18));
        }
    };

    inline std::unique_ptr<Knob> makeKnob (APVTS& s, params::Id id, const juce::String& label)
    {
        return std::make_unique<Knob> (s, params::paramId (id), label);
    }
}
