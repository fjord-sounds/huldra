#pragma once
#include "Layout.h"
#include "../PluginProcessor.h"

namespace huldra::ui
{
    // Base of all pages: tick() is called at 30 Hz while the page is visible (and by hand for screenshots).
    class Page : public juce::Component
    {
    public:
        explicit Page (HuldraProcessor& p) : proc (p) {}
        virtual void tick() {}

    protected:
        HuldraProcessor& proc;
    };

    // Small helper: a panel with a title, used by most pages.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, float radius = 8.0f)
    {
        const auto& p = Theme::get().palette();
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, radius);
        g.setColour (p.line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (p.text);
            drawSpacedText (g, title.toUpperCase(), r.reduced (16.0f, 0.0f).removeFromTop (32.0f), 10.5f,
                            juce::Justification::centredLeft, 0.25f, true);
        }
    }
}
