#pragma once
#include <fjord_base/ui/Theme.h>
#include <fjord_base/ui/Controls.h>
#include <fjord_base/ui/Keyboard.h>
#include <fjord_base/ui/PluginView.h>

// Huldra's UI builds on the shared family UI of fjord-base (palette, Manrope, controls, header, presets).
// Only Huldra's own colour helpers live here: the veil (rose -> lilac -> ice blue) is the brand gradient.
namespace huldra::ui
{
    using namespace fjord::base::ui;

    inline const std::array<juce::Colour, 3>& veilColours (bool dark) { return gradientColours (dark); }
    inline juce::Colour veilAt (bool dark, float x) { return gradientAt (dark, x); }
    inline void drawVeilGlow (juce::Graphics& g, juce::Rectangle<float> area, bool dark, float strength = 1.0f) { drawGlow (g, area, dark, strength); }

    // Colour of each engine (classic, granular, spectral, vocal), readable in both themes.
    inline juce::Colour engineColour (int engine, bool dark)
    {
        static const juce::uint32 darkC[]  = { 0xfff29c9c, 0xffb49cf2, 0xff7fc4e8, 0xfff2c49c };
        static const juce::uint32 lightC[] = { 0xffb0545a, 0xff7a5cc0, 0xff2f7fb0, 0xffb07a3a };
        return juce::Colour ((dark ? darkC : lightC)[std::clamp (engine, 0, 3)]);
    }
}
