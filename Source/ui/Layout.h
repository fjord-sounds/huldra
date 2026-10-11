#pragma once
#include "Controls.h"
#include "../Parameters.h"

namespace huldra::ui
{
    namespace P = params;

    inline std::unique_ptr<Knob> makeKnob (APVTS& s, params::Id id, const juce::String& label)
    {
        return std::make_unique<Knob> (s, params::paramId (id), label);
    }

    // Small label above a selector (segmented / dropdown / toggle)
    class Labelled : public juce::Component
    {
    public:
        Labelled (std::unique_ptr<juce::Component> c, juce::String t) : content (std::move (c)), title (std::move (t))
        {
            addAndMakeVisible (*content);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::get().palette().textDim);
            drawSpacedText (g, title.toUpperCase(), getLocalBounds().removeFromTop (14).toFloat(), 9.5f,
                            juce::Justification::centredLeft, 0.16f);
        }

        void resized() override { content->setBounds (getLocalBounds().withTrimmedTop (16).withHeight (24)); }

        juce::Component& get() { return *content; }

    private:
        std::unique_ptr<juce::Component> content;
        juce::String title;
    };

    // A row of controls: each entry has a relative width (knobs 1, wide selectors 2-4).
    struct ControlRow
    {
        std::vector<std::pair<std::unique_ptr<juce::Component>, float>> items;

        void add (std::unique_ptr<juce::Component> c, float units = 1.0f) { items.emplace_back (std::move (c), units); }

        void addTo (juce::Component& parent)
        {
            for (auto& [c, u] : items) parent.addAndMakeVisible (*c);
        }

        void setVisible (bool v) { for (auto& [c, u] : items) c->setVisible (v); }

        // Selectors get a fixed height and sit centred in their cell
        void layout (juce::Rectangle<int> area, int gap = 4)
        {
            float total = 0.0f;
            for (auto& [c, u] : items) total += u;
            const float unit = (float) (area.getWidth() - gap * ((int) items.size() - 1)) / std::max (1.0f, total);
            float x = (float) area.getX();
            for (auto& [c, u] : items)
            {
                const int w = (int) (unit * u);
                juce::Rectangle<int> cell ((int) x, area.getY(), w, area.getHeight());
                if (dynamic_cast<Knob*> (c.get()) == nullptr)
                {
                    const int h = dynamic_cast<Labelled*> (c.get()) != nullptr ? 40 : 24;
                    cell = cell.withSizeKeepingCentre (w, h).translated (0, -6);
                }
                c->setBounds (cell);
                x += (float) w + (float) gap;
            }
        }
    };

    inline std::unique_ptr<juce::Component> labelled (std::unique_ptr<juce::Component> c, const juce::String& title)
    {
        return std::make_unique<Labelled> (std::move (c), title);
    }

}
