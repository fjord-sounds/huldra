#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // ---------------------------------------------------------------------------------------------
    // Macro knob with an editable name (double-click the name).
    class MacroKnob : public juce::Component
    {
    public:
        MacroKnob (HuldraProcessor& p, int idx) : proc (p), index (idx), knob (p.apvts, params::paramId (params::macro (idx)), "")
        {
            addAndMakeVisible (knob);
            knob.setLabelHeight (0);
            editor.setJustification (juce::Justification::centred);
            editor.onReturnKey = [this] { commit(); };
            editor.onFocusLost = [this] { commit(); };
            editor.onEscapeKey = [this] { editor.setVisible (false); };
            addChildComponent (editor);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto name = proc.presetManager.getMacroNames()[index];
            g.setColour (p.text.withAlpha (0.85f));
            drawSpacedText (g, name.toUpperCase(), nameArea().toFloat(), 10.0f, juce::Justification::centred, 0.14f);
            // small value glow under the knob
            const float v = proc.apvts.getRawParameterValue (params::paramId (params::macro (index)))->load();
            g.setColour (veilAt (dark, (float) index / 7.0f).withAlpha (0.25f + 0.75f * v));
            g.fillRoundedRectangle (nameArea().toFloat().withSizeKeepingCentre (18.0f + 30.0f * v, 2.0f).translated (0.0f, -15.0f), 1.0f);
        }

        void resized() override
        {
            knob.setBounds (getLocalBounds().withTrimmedBottom (20));
            editor.setBounds (nameArea().expanded (4, 2));
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! nameArea().contains (e.getPosition())) return;
            styleTextEditor (editor);
            editor.setFont (font (11.0f));
            editor.setIndents (4, 0);
            editor.setText (proc.presetManager.getMacroNames()[index], false);
            editor.setVisible (true);
            editor.grabKeyboardFocus();
            editor.selectAll();
        }

    private:
        juce::Rectangle<int> nameArea() const { return getLocalBounds().removeFromBottom (18); }
        void commit()
        {
            if (! editor.isVisible()) return;
            proc.presetManager.setMacroName (index, editor.getText().trim());
            editor.setVisible (false);
            repaint();
        }

        HuldraProcessor& proc;
        int index;
        Knob knob;
        juce::TextEditor editor;
    };

    // ---------------------------------------------------------------------------------------------
    // One LFO: animated shape with the current phase, and its controls.
    class LfoPanel : public juce::Component
    {
    public:
        LfoPanel (HuldraProcessor& p, int idx)
            : proc (p), index (idx),
              shape (p.apvts, params::paramId (params::lfoParam (idx, 0)), lfoGlyph),
              rate (p.apvts, params::paramId (params::lfoParam (idx, 1)), "Rate"),
              sync (p.apvts, params::paramId (params::lfoParam (idx, 2)), "Sync"),
              beats (p.apvts, params::paramId (params::lfoParam (idx, 3))),
              retrig (p.apvts, params::paramId (params::lfoParam (idx, 4)), "Retrig"),
              fade (p.apvts, params::paramId (params::lfoParam (idx, 5)), "Fade")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &shape, &rate, &sync, &beats, &retrig, &fade })
                addAndMakeVisible (c);
            sync.onChange = [this] (bool) { resized(); };
        }

        void tick()
        {
            history[(size_t) write] = proc.voiceDisplay.sources[(size_t) params::ModSource::Lfo1 + (size_t) index].load();
            write = (write + 1) % (int) history.size();
            repaint (display().toNearestInt());
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            drawPanel (g, getLocalBounds().toFloat(), "LFO " + juce::String (index + 1));
            auto d = display();
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (d, 6.0f);
            g.setColour (p.line);
            g.fillRect (d.getX() + 4.0f, d.getCentreY(), d.getWidth() - 8.0f, 1.0f);
            // the recent output as a scrolling trace
            juce::Path trace;
            const int n = (int) history.size();
            for (int i = 0; i < n; ++i)
            {
                const float v = history[(size_t) ((write + i) % n)];
                const float x = d.getX() + 4.0f + (d.getWidth() - 8.0f) * (float) i / (float) (n - 1);
                const float y = d.getCentreY() - v * (d.getHeight() * 0.4f);
                if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
            }
            const auto col = veilAt (dark, (float) index / 3.0f);
            g.setColour (col);
            g.strokePath (trace, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float last = history[(size_t) ((write + n - 1) % n)];
            const juce::Point<float> head (d.getRight() - 4.0f, d.getCentreY() - last * d.getHeight() * 0.4f);
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.6f), head, col.withAlpha (0.0f), head.translated (10.0f, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (head));
            g.setColour (p.text);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (head));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 0).withTrimmedTop (30 + 50).withTrimmedBottom (6);
            shape.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);
            auto row = r;
            const int w = row.getWidth() / 4;
            auto c1 = row.removeFromLeft (w);
            const bool synced = proc.apvts.getRawParameterValue (params::paramId (params::lfoParam (index, 2)))->load() > 0.5f;
            rate.setVisible (! synced);
            beats.setVisible (synced);
            rate.setBounds (c1);
            beats.setBounds (c1.withSizeKeepingCentre (c1.getWidth() - 4, 24));
            fade.setBounds (row.removeFromLeft (w));
            auto toggles = row.reduced (4, 0);
            sync.setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (toggles.getWidth(), 20));
            retrig.setBounds (toggles.withSizeKeepingCentre (toggles.getWidth(), 20));
        }

    private:
        juce::Rectangle<float> display() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (30.0f).withHeight (46.0f); }

        HuldraProcessor& proc;
        int index;
        Segmented shape;
        Knob rate;
        Toggle sync;
        Choice beats;
        Toggle retrig;
        Knob fade;
        std::array<float, 90> history {};
        int write = 0;
    };

    // ---------------------------------------------------------------------------------------------
    // MSEG editor: drag points, double-click to add, right-click to delete, alt-drag a segment to bend it.
    class MsegEditor : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
    {
    public:
        explicit MsegEditor (HuldraProcessor& p) : proc (p)
        {
            proc.instrument.addChangeListener (this);
            setTooltip ("Drag points; double-click adds one, right-click removes it; drag a line up / down to bend the curve.");
        }

        ~MsegEditor() override { proc.instrument.removeChangeListener (this); }

        void setIndex (int i) { index = i; repaint(); }
        int getIndex() const { return index; }

        void tick() { repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = area();
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r.expanded (6.0f), 6.0f);
            for (int i = 1; i < 8; ++i)
            {
                g.setColour (p.line.withAlpha (i % 2 == 0 ? 0.8f : 0.4f));
                g.fillRect (r.getX() + r.getWidth() * (float) i / 8.0f, r.getY(), 1.0f, r.getHeight());
            }
            const auto shape = proc.instrument.getMseg (index);
            juce::Path path;
            const int n = 200;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / (float) n;
                const auto pt = toScreen (t, shape.valueAt (t));
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            juce::Path fill (path);
            fill.lineTo (r.getRight(), r.getBottom());
            fill.lineTo (r.getX(), r.getBottom());
            fill.closeSubPath();
            const auto col = veilAt (dark, 0.3f * (float) index);
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.35f), 0.0f, r.getY(), col.withAlpha (0.03f), 0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (int i = 0; i < shape.numPoints; ++i)
            {
                const auto pt = toScreen (shape.points[(size_t) i].time, shape.points[(size_t) i].level);
                const bool hov = i == hovered;
                g.setColour (p.panel);
                g.fillEllipse (juce::Rectangle<float> (hov ? 12.0f : 9.0f, hov ? 12.0f : 9.0f).withCentre (pt));
                g.setColour (hov ? p.text : col);
                g.drawEllipse (juce::Rectangle<float> (hov ? 12.0f : 9.0f, hov ? 12.0f : 9.0f).withCentre (pt), 1.6f);
            }
            // live value of this MSEG (newest voice)
            const float v = proc.voiceDisplay.sources[(size_t) params::ModSource::Mseg1 + (size_t) index].load();
            const float y = r.getBottom() - v * r.getHeight();
            g.setColour (p.text.withAlpha (0.35f));
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = pointAt (e.position);
            if (h != hovered) { hovered = h; repaint(); }
            setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto shape = proc.instrument.getMseg (index);
            dragPoint = pointAt (e.position);
            proc.undoManager.beginNewTransaction ("Edit MSEG");
            if (e.mods.isPopupMenu())
            {
                if (dragPoint > 0 && dragPoint < shape.numPoints - 1)
                {
                    for (int i = dragPoint; i + 1 < shape.numPoints; ++i) shape.points[(size_t) i] = shape.points[(size_t) i + 1];
                    --shape.numPoints;
                    proc.instrument.setMseg (index, shape, true);
                }
                dragPoint = -1;
                return;
            }
            if (dragPoint < 0)
            {
                // bend the segment under the mouse
                const float t = fromScreen (e.position).x;
                bendSegment = 1;
                while (bendSegment < shape.numPoints - 1 && shape.points[(size_t) bendSegment].time < t) ++bendSegment;
                startCurve = shape.points[(size_t) bendSegment].curve;
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto shape = proc.instrument.getMseg (index);
            if (dragPoint >= 0)
            {
                const auto v = fromScreen (e.position);
                auto& pt = shape.points[(size_t) dragPoint];
                const bool edge = dragPoint == 0 || dragPoint == shape.numPoints - 1;
                if (! edge)
                    pt.time = juce::jlimit (shape.points[(size_t) dragPoint - 1].time + 0.002f, shape.points[(size_t) dragPoint + 1].time - 0.002f, v.x);
                pt.level = juce::jlimit (0.0f, 1.0f, v.y);
            }
            else if (bendSegment > 0)
            {
                const auto& a = shape.points[(size_t) bendSegment - 1];
                const auto& b = shape.points[(size_t) bendSegment];
                const float dir = b.level >= a.level ? -1.0f : 1.0f;
                shape.points[(size_t) bendSegment].curve = juce::jlimit (-1.0f, 1.0f, startCurve + dir * (float) e.getDistanceFromDragStartY() / 80.0f);
            }
            proc.instrument.setMseg (index, shape, true);
        }

        void mouseUp (const juce::MouseEvent&) override { dragPoint = -1; bendSegment = -1; }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto shape = proc.instrument.getMseg (index);
            if (shape.numPoints >= dsp::kMaxEnvPoints || pointAt (e.position) >= 0) return;
            const auto v = fromScreen (e.position);
            int at = 1;
            while (at < shape.numPoints && shape.points[(size_t) at].time < v.x) ++at;
            for (int i = shape.numPoints; i > at; --i) shape.points[(size_t) i] = shape.points[(size_t) i - 1];
            shape.points[(size_t) at] = { v.x, juce::jlimit (0.0f, 1.0f, v.y), 0.0f };
            ++shape.numPoints;
            proc.undoManager.beginNewTransaction ("Add MSEG point");
            proc.instrument.setMseg (index, shape, true);
        }

    private:
        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (12.0f, 10.0f); }
        juce::Point<float> toScreen (float t, float v) const { const auto r = area(); return { r.getX() + t * r.getWidth(), r.getBottom() - v * r.getHeight() }; }
        juce::Point<float> fromScreen (juce::Point<float> pt) const
        {
            const auto r = area();
            return { juce::jlimit (0.0f, 1.0f, (pt.x - r.getX()) / r.getWidth()), (r.getBottom() - pt.y) / r.getHeight() };
        }

        int pointAt (juce::Point<float> pos) const
        {
            const auto shape = proc.instrument.getMseg (index);
            for (int i = 0; i < shape.numPoints; ++i)
                if (toScreen (shape.points[(size_t) i].time, shape.points[(size_t) i].level).getDistanceFrom (pos) < 8.0f) return i;
            return -1;
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        HuldraProcessor& proc;
        int index = 0, hovered = -1, dragPoint = -1, bendSegment = -1;
        float startCurve = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Target picker: the long list of destinations grouped into submenus.
    class TargetPicker : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TargetPicker (APVTS& s, params::Id id) : param (s.getParameter (params::paramId (id)))
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, nullptr);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        static juce::String groupOf (params::Id id)
        {
            const juce::String pid = params::paramId (id);
            if (pid.startsWith ("smp_")) return "Sample";
            if (pid.startsWith ("grn_")) return "Granular";
            if (pid.startsWith ("spc_")) return "Spectral";
            if (pid.startsWith ("voc_")) return "Vocal";
            if (pid.startsWith ("flt_") || pid.startsWith ("fenv_")) return "Filter";
            if (pid.startsWith ("amp_")) return "Amp";
            if (pid.startsWith ("lfo") || pid.startsWith ("mseg")) return "Modulation";
            if (pid.startsWith ("mot_") || pid.startsWith ("gen_") || pid.startsWith ("arp_") || pid.startsWith ("pat_")) return "Motion";
            if (pid.startsWith ("chop_") || pid.startsWith ("scl_") || pid.startsWith ("retune") || pid.startsWith ("chord")) return "Chops and scale";
            for (const char* fx : { "sat_", "lofi_", "comp_", "sq_", "eq_", "chr_", "dly_", "verb_", "width_", "lim_" })
                if (pid.startsWith (fx)) return "Effects";
            return "Voice";
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover ? p.textDim : p.line);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            const int index = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
            const auto& targets = params::modTargets();
            const juce::String text = index > 0 && index <= (int) targets.size() ? juce::String (params::spec (targets[(size_t) index - 1]).name) : juce::String ("No target");
            g.setColour (index > 0 ? p.text : p.textDim);
            g.setFont (font (12.0f));
            g.drawText (text, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            const auto& targets = params::modTargets();
            std::map<juce::String, juce::PopupMenu> groups;
            const juce::StringArray order { "Voice", "Sample", "Granular", "Spectral", "Vocal", "Filter", "Amp", "Modulation", "Motion", "Chops and scale", "Effects" };
            const int current = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
            for (int i = 0; i < (int) targets.size(); ++i)
                groups[groupOf (targets[(size_t) i])].addItem (i + 2, params::spec (targets[(size_t) i]).name, true, current == i + 1);
            juce::PopupMenu m;
            m.addItem (1, "No target", true, current == 0);
            m.addSeparator();
            for (const auto& name : order)
                if (groups.count (name) > 0) m.addSubMenu (name, groups[name]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r > 0) attachment->setValueAsCompleteGesture ((float) (r - 1));
            });
        }

    private:
        juce::RangedAudioParameter* param;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        bool hover = false;
    };

    // One row of the matrix: source, target, amount, and a dot that pulses with the source.
    class MatrixRow : public juce::Component
    {
    public:
        MatrixRow (HuldraProcessor& p, int s)
            : proc (p), slot (s), source (p.apvts, params::paramId (params::modSrc (s))), target (p.apvts, params::modDst (s))
        {
            addAndMakeVisible (source);
            addAndMakeVisible (target);
            amount.setSliderStyle (juce::Slider::LinearHorizontal);
            amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (amount);
            amountAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (params::paramId (params::modAmt (s))), amount, nullptr);
            amount.setDoubleClickReturnValue (true, 0.0);
            amount.setPopupDisplayEnabled (true, true, nullptr);
        }

        void tick()
        {
            const int src = juce::roundToInt (proc.apvts.getRawParameterValue (params::paramId (params::modSrc (slot)))->load());
            const float v = src > 0 ? proc.voiceDisplay.sources[(size_t) std::min (src, params::kNumModSources - 1)].load() : 0.0f;
            phase += 0.02f + std::abs (v) * 0.08f;
            value = v;
            repaint (getLocalBounds().removeFromLeft (22));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const float amt = proc.apvts.getRawParameterValue (params::paramId (params::modAmt (slot)))->load();
            const bool active = std::abs (amt) > 0.001f && source.isEnabled();
            auto dot = getLocalBounds().removeFromLeft (22).toFloat();
            g.setColour (p.textDim);
            g.setFont (font (10.0f));
            g.drawText (juce::String (slot + 1), dot.withTrimmedRight (8.0f), juce::Justification::centredLeft);
            if (active)
            {
                const auto c = veilAt (dark, 0.5f + 0.5f * std::clamp (value, -1.0f, 1.0f));
                const float s = 4.0f + 4.0f * std::abs (value);
                g.setColour (c.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (s * 2.0f, s * 2.0f).withCentre ({ dot.getRight() - 4.0f, dot.getCentreY() }));
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (s, s).withCentre ({ dot.getRight() - 4.0f, dot.getCentreY() }));
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedLeft (26).reduced (0, 3);
            source.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (6);
            target.setBounds (r.removeFromLeft (r.getWidth() - 120));
            r.removeFromLeft (8);
            amount.setBounds (r);
        }

    private:
        HuldraProcessor& proc;
        int slot;
        Choice source;
        TargetPicker target;
        KnobSlider amount;
        std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
        float phase = 0.0f, value = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    class ModPage : public Page
    {
    public:
        explicit ModPage (HuldraProcessor& p)
            : Page (p), mseg (p), msegTabs ({ "MSEG 1", "MSEG 2", "MSEG 3" }),
              drift (p.apvts, params::paramId (params::PadDrift), "Drift"),
              driftRate (p.apvts, params::paramId (params::PadDriftRate), "Drift Speed")
        {
            for (int i = 0; i < params::kNumMacros; ++i)
            {
                macros.push_back (std::make_unique<MacroKnob> (p, i));
                addAndMakeVisible (*macros.back());
            }
            addAndMakeVisible (drift);
            addAndMakeVisible (driftRate);
            for (int i = 0; i < params::kNumLfos; ++i)
            {
                lfos.push_back (std::make_unique<LfoPanel> (p, i));
                addAndMakeVisible (*lfos.back());
            }
            addAndMakeVisible (mseg);
            addAndMakeVisible (msegTabs);
            msegTabs.onChange = [this] (int i) { mseg.setIndex (i); updateMsegControls(); };
            for (int i = 0; i < params::kNumMsegs; ++i)
            {
                msegTime.push_back (std::make_unique<Knob> (p.apvts, params::paramId (params::msegTime (i)), "Length"));
                msegLoop.push_back (std::make_unique<Toggle> (p.apvts, params::paramId (params::msegLoop (i)), "Loop"));
                addChildComponent (*msegTime.back());
                addChildComponent (*msegLoop.back());
            }
            for (int s = 0; s < params::kNumModSlots; ++s)
            {
                rows.push_back (std::make_unique<MatrixRow> (p, s));
                matrixContent.addAndMakeVisible (*rows.back());
            }
            matrixView.setViewedComponent (&matrixContent, false);
            matrixView.setScrollBarsShown (true, false);
            matrixView.setScrollBarThickness (6);
            addAndMakeVisible (matrixView);
            updateMsegControls();
        }

        void tick() override
        {
            for (auto& l : lfos) l->tick();
            mseg.tick();
            for (auto& r : rows) r->tick();
            for (auto& m : macros) m->repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, macroArea.toFloat(), "Macros");
            drawPanel (g, msegArea.toFloat(), "");
            drawPanel (g, matrixArea.toFloat(), "Modulation matrix");
            g.setColour (Theme::get().palette().textDim);
            drawSpacedText (g, "SOURCE", { (float) matrixView.getX() + 26.0f, (float) matrixArea.getY() + 30.0f, 100.0f, 14.0f }, 9.0f, juce::Justification::centredLeft, 0.16f);
            drawSpacedText (g, "TARGET", { (float) matrixView.getX() + 142.0f, (float) matrixArea.getY() + 30.0f, 100.0f, 14.0f }, 9.0f, juce::Justification::centredLeft, 0.16f);
            drawSpacedText (g, "AMOUNT", { (float) matrixView.getRight() - 128.0f, (float) matrixArea.getY() + 30.0f, 100.0f, 14.0f }, 9.0f, juce::Justification::centredLeft, 0.16f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            macroArea = r.removeFromTop (118);
            r.removeFromTop (10);
            auto lfoRow = r.removeFromTop (178);
            r.removeFromTop (10);
            msegArea = r.removeFromLeft (r.getWidth() * 50 / 100);
            r.removeFromLeft (10);
            matrixArea = r;

            auto m = macroArea.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
            auto driftArea = m.removeFromRight (190);
            const int w = m.getWidth() / params::kNumMacros;
            for (auto& k : macros) k->setBounds (m.removeFromLeft (w).reduced (4, 0));
            drift.setBounds (driftArea.removeFromLeft (95));
            driftRate.setBounds (driftArea);

            const int lw = (lfoRow.getWidth() - 3 * 10) / 4;
            for (auto& l : lfos) { l->setBounds (lfoRow.removeFromLeft (lw)); lfoRow.removeFromLeft (10); }

            auto ms = msegArea.reduced (10, 0);
            auto top = ms.removeFromTop (34);
            msegTabs.setBounds (top.removeFromLeft (240).withSizeKeepingCentre (240, 24).translated (0, 2));
            ms.removeFromBottom (4);
            auto side = ms.removeFromRight (86);
            for (int i = 0; i < params::kNumMsegs; ++i)
            {
                auto sd = side;
                msegTime[(size_t) i]->setBounds (sd.removeFromTop (88));
                msegLoop[(size_t) i]->setBounds (sd.removeFromTop (26).reduced (8, 0));
            }
            mseg.setBounds (ms);

            matrixView.setBounds (matrixArea.reduced (10, 0).withTrimmedTop (46).withTrimmedBottom (8));
            const int rowH = 30;
            matrixContent.setSize (matrixView.getWidth() - 8, rowH * params::kNumModSlots);
            for (int i = 0; i < (int) rows.size(); ++i)
                rows[(size_t) i]->setBounds (0, i * rowH, matrixContent.getWidth(), rowH);
        }

    private:
        void updateMsegControls()
        {
            for (int i = 0; i < params::kNumMsegs; ++i)
            {
                msegTime[(size_t) i]->setVisible (i == mseg.getIndex());
                msegLoop[(size_t) i]->setVisible (i == mseg.getIndex());
            }
        }

        std::vector<std::unique_ptr<MacroKnob>> macros;
        std::vector<std::unique_ptr<LfoPanel>> lfos;
        MsegEditor mseg;
        MiniTabs msegTabs;
        std::vector<std::unique_ptr<Knob>> msegTime;
        std::vector<std::unique_ptr<Toggle>> msegLoop;
        Knob drift, driftRate;
        juce::Component matrixContent;
        juce::Viewport matrixView;
        std::vector<std::unique_ptr<MatrixRow>> rows;
        juce::Rectangle<int> macroArea, msegArea, matrixArea;
    };
}
