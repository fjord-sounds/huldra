#pragma once
#include "Page.h"
#include "PresetBrowser.h"

namespace huldra::ui
{
    // Rotary knob for values that are not host parameters (per-key edits, zone settings).
    class ValueKnob : public juce::Component
    {
    public:
        ValueKnob (juce::String labelText, double min, double max, double def, std::function<juce::String (double)> format)
            : name (std::move (labelText)), formatter (std::move (format))
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            slider.setMouseDragSensitivity (220);
            slider.setRange (min, max);
            slider.setDoubleClickReturnValue (true, def);
            slider.setValue (def, juce::dontSendNotification);
            addAndMakeVisible (slider);
            slider.onValueChange = [this] { repaint(); if (onChange && ! updating) onChange (slider.getValue(), slider.isMouseButtonDown()); };
            slider.onDragStart = [this] { dragging = true; if (onDragStart) onDragStart(); repaint(); };
            slider.onDragEnd = [this] { dragging = false; if (onDragEnd) onDragEnd(); repaint(); };
            slider.addMouseListener (this, false);
        }

        // value, still dragging
        std::function<void (double, bool)> onChange;
        std::function<void()> onDragStart, onDragEnd;

        void setValue (double v)
        {
            if (dragging) return;
            updating = true;
            slider.setValue (v, juce::sendNotificationSync);
            updating = false;
        }

        double getValue() const { return slider.getValue(); }
        void setSkew (double centre) { slider.setSkewFactorFromMidPoint (centre); }
        void setInterval (double step) { slider.setRange (slider.getMinimum(), slider.getMaximum(), step); }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (16);
            const int size = std::min (r.getWidth(), r.getHeight());
            slider.setBounds (r.withSizeKeepingCentre (size, size));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool showValue = hovering || dragging;
            g.setColour (showValue ? p.text : p.textDim);
            drawSpacedText (g, showValue ? formatter (slider.getValue()) : name.toUpperCase(), getLocalBounds().removeFromBottom (16).toFloat(),
                            10.5f, juce::Justification::centred, showValue ? 0.02f : 0.12f);
        }

    private:
        juce::String name;
        std::function<juce::String (double)> formatter;
        KnobSlider slider;
        bool hovering = false, dragging = false, updating = false;
    };

    // Simple chips that select one of several options (not a parameter).
    class ChoiceChips : public juce::Component
    {
    public:
        explicit ChoiceChips (juce::StringArray labels) : items (std::move (labels)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void (int)> onChange;
        void setSelected (int i) { if (i != selected) { selected = i; repaint(); } }
        int getSelected() const { return selected; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const float w = (float) getWidth() / (float) items.size();
            for (int i = 0; i < items.size(); ++i)
            {
                auto cell = juce::Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (2.0f, 1.0f);
                const bool on = i == selected;
                if (on) { g.setColour (p.accentSoft); g.fillRoundedRectangle (cell, 3.0f); }
                g.setColour (on ? p.accent : (i == hovered ? p.text : p.textDim));
                drawSpacedText (g, items[i].toUpperCase(), cell, 10.0f, juce::Justification::centred, 0.08f, on);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { const int h = indexAt (e.x); if (h != hovered) { hovered = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            selected = indexAt (e.x);
            repaint();
            if (onChange) onChange (selected);
        }

    private:
        int indexAt (int x) const { return std::clamp (x * items.size() / std::max (1, getWidth()), 0, items.size() - 1); }
        juce::StringArray items;
        int selected = 0, hovered = -1;
    };

    // Text-only tab bar inside a page (e.g. MSEG 1 / 2 / 3)
    using MiniTabs = ChoiceChips;

    // Drags a host parameter from a custom display (start / end markers, loop handles ...)
    class ParamDrag
    {
    public:
        ParamDrag (APVTS& s, params::Id id) : param (s.getParameter (params::paramId (id))) {}
        void begin() { if (! active) { param->beginChangeGesture(); active = true; } }
        void setPlain (float v) { param->setValueNotifyingHost (param->convertTo0to1 (v)); }
        void end() { if (active) { param->endChangeGesture(); active = false; } }
        float plain() const { return param->convertFrom0to1 (param->getValue()); }
        void reset() { begin(); param->setValueNotifyingHost (param->getDefaultValue()); end(); }
        juce::RangedAudioParameter& get() { return *param; }

    private:
        juce::RangedAudioParameter* param;
        bool active = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Waveform drawing shared by the pages: mirrored bars coloured by the brightness of the sound.
    inline void drawWaveform (juce::Graphics& g, const SampleData& s, juce::Rectangle<float> area, float from = 0.0f, float to = 1.0f,
                              float alpha = 1.0f, bool bars = true)
    {
        const bool dark = Theme::get().isDark();
        const int n = SampleData::overviewSize;
        const float gain = 0.95f / std::max (0.05f, s.peak);
        const int columns = std::max (8, (int) (area.getWidth() / (bars ? 3.0f : 1.0f)));
        const float colW = area.getWidth() / (float) columns;
        const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        for (int c = 0; c < columns; ++c)
        {
            const float t0 = from + (to - from) * (float) c / (float) columns;
            const float t1 = from + (to - from) * (float) (c + 1) / (float) columns;
            const int b0 = std::clamp ((int) (t0 * n), 0, n - 1), b1 = std::clamp ((int) (t1 * n), b0 + 1, n);
            float lo = 0.0f, hi = 0.0f, bright = 0.0f;
            for (int b = b0; b < b1; ++b)
            {
                lo = std::min (lo, s.overviewMin[(size_t) b]);
                hi = std::max (hi, s.overviewMax[(size_t) b]);
                bright += s.overviewBright[(size_t) b];
            }
            bright /= (float) std::max (1, b1 - b0);
            const float top = mid - std::min (1.0f, hi * gain) * half, bottom = mid - std::max (-1.0f, lo * gain) * half;
            const auto col = veilAt (dark, 1.0f - bright).withMultipliedAlpha (alpha);
            g.setColour (col);
            const float x = area.getX() + (float) c * colW;
            if (bars)
                g.fillRoundedRectangle (x + colW * 0.15f, std::min (top, mid - 0.75f), colW * 0.7f, std::max (1.5f, bottom - top), colW * 0.35f);
            else
                g.fillRect (x, top, colW + 0.5f, std::max (1.0f, bottom - top));
        }
    }

    // The sample currently shown on the pages (active sample, or the demo voice)
    inline const SampleData* displaySample (HuldraProcessor& proc)
    {
        return proc.instrument.snapshot().chopSample;
    }

    // Playheads of all voices that play 's', with their level
    template <typename Fn> inline void forEachPlayhead (HuldraProcessor& proc, const SampleData* s, Fn&& fn)
    {
        auto& d = proc.voiceDisplay;
        for (int i = 0; i < dsp::VoiceDisplay::kSlots; ++i)
        {
            const float level = d.level[(size_t) i].load (std::memory_order_relaxed);
            if (level < 0.0f) break;
            if (d.sample[(size_t) i].load (std::memory_order_relaxed) != s) continue;
            fn (d.playhead[(size_t) i].load (std::memory_order_relaxed), level, d.note[(size_t) i].load (std::memory_order_relaxed));
        }
    }

    inline juce::String noteName (int n) { return params::noteName (n); }

    // ADSR graph that follows four parameters
    class AdsrGraph : public juce::Component
    {
    public:
        AdsrGraph (APVTS& s, params::Id a, params::Id d, params::Id su, params::Id r, std::function<float()> levelSource = nullptr)
            : state (s), ids { a, d, su, r }, level (std::move (levelSource)) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
            auto v = [this] (int i) { return state.getRawParameterValue (params::paramId (ids[(size_t) i]))->load(); };
            const float a = v (0), d = v (1), s = v (2), rel = v (3);
            const float total = a + d + 0.5f + rel;
            auto tx = [&] (float t) { return r.getX() + std::sqrt (t / total) * r.getWidth(); };
            juce::Path path;
            path.startNewSubPath (r.getX(), r.getBottom());
            path.lineTo (tx (a), r.getY());
            const int steps = 16;
            for (int i = 1; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                path.lineTo (tx (a + d * t), r.getY() + (1.0f - (s + (1.0f - s) * std::exp (-4.6f * t))) * r.getHeight());
            }
            path.lineTo (tx (a + d + 0.5f), r.getY() + (1.0f - s) * r.getHeight());
            for (int i = 1; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                path.lineTo (tx (a + d + 0.5f + rel * t), r.getY() + (1.0f - s * std::exp (-6.9f * t)) * r.getHeight());
            }
            juce::Path fill (path);
            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (veilAt (dark, 0.0f).withAlpha (0.28f), r.getX(), r.getY(),
                                                     veilAt (dark, 1.0f).withAlpha (0.06f), r.getRight(), r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (p.accent);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (level != nullptr)
            {
                const float lv = level();
                if (lv > 0.001f)
                {
                    g.setColour (p.text.withAlpha (0.5f));
                    const float y = r.getBottom() - lv * r.getHeight();
                    g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
                }
            }
        }

    private:
        APVTS& state;
        std::array<params::Id, 4> ids;
        std::function<float()> level;
    };
}
