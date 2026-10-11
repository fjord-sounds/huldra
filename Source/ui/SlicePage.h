#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // Waveform with the slices, threads from every slice down to its key, and a key strip.
    class SliceView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SliceView (HuldraProcessor& p) : proc (p), mode (p.apvts, params::ChopMode)
        {
            setTooltip ("Click a slice to play it. Double-click to add a marker, drag markers, right-click a marker to remove it.");
        }

        void tick()
        {
            time += 1.0f / 30.0f;
            for (auto& g : glow) g = std::max (0.0f, g - 1.0f / 30.0f / 0.35f);
            const auto& snap = proc.instrument.snapshot();
            const int base = juce::roundToInt (proc.apvts.getRawParameterValue ("chop_base")->load());
            if (proc.apvts.getRawParameterValue ("play_mode")->load() > 0.5f)
                forEachPlayhead (proc, snap.chopSample, [&] (float, float level, int note)
            {
                const int i = note - base;
                if (juce::isPositiveAndBelow (i, (int) glow.size())) glow[(size_t) i] = std::max (glow[(size_t) i], 0.4f + 0.6f * std::min (1.0f, level));
            });
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

            const auto& snap = proc.instrument.snapshot();
            const auto* s = snap.chopSample;
            if (s == nullptr) return;
            const auto w = wave();
            const auto& slices = snap.slices;
            const int base = juce::roundToInt (proc.apvts.getRawParameterValue ("chop_base")->load());
            const int n = (int) slices.size();

            // Slice backgrounds
            for (int i = 0; i < n; ++i)
            {
                const auto& sl = slices[(size_t) i];
                const float x0 = xOf ((float) sl.start / (float) s->numFrames), x1 = xOf ((float) sl.end / (float) s->numFrames);
                const auto col = veilAt (dark, n > 1 ? (float) i / (float) (n - 1) : 0.0f);
                const float gl = i < (int) glow.size() ? glow[(size_t) i] : 0.0f;
                g.setColour (col.withAlpha ((i % 2 == 0 ? 0.07f : 0.03f) + (i == hovered ? 0.08f : 0.0f) + 0.25f * gl));
                g.fillRoundedRectangle (x0 + 1.0f, w.getY(), x1 - x0 - 2.0f, w.getHeight(), 4.0f);
            }

            drawWaveform (g, *s, w, 0.0f, 1.0f, 0.95f);

            // Markers, numbers, detected pitch
            for (int i = 0; i < n; ++i)
            {
                const auto& sl = slices[(size_t) i];
                const float x0 = xOf ((float) sl.start / (float) s->numFrames), x1 = xOf ((float) sl.end / (float) s->numFrames);
                const auto col = veilAt (dark, n > 1 ? (float) i / (float) (n - 1) : 0.0f);
                g.setColour (col);
                g.fillRect (x0 - 0.5f, w.getY() - 8.0f, 1.0f, w.getHeight() + 8.0f);
                g.fillEllipse (x0 - 3.0f, w.getY() - 11.0f, 6.0f, 6.0f);
                if (x1 - x0 > 18.0f)
                {
                    g.setColour (p.text.withAlpha (0.8f));
                    g.setFont (font (11.0f, Weight::SemiBold));
                    g.drawText (juce::String (i + 1), juce::Rectangle<float> (x0 + 5.0f, w.getY() - 2.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
                    if (sl.pitch > 0.0f && x1 - x0 > 40.0f)
                    {
                        g.setColour (p.textDim);
                        g.setFont (font (10.0f));
                        g.drawText (noteName (juce::roundToInt (sl.pitch)), juce::Rectangle<float> (x0 + 5.0f, w.getBottom() - 16.0f, 40.0f, 14.0f),
                                    juce::Justification::centredLeft);
                    }
                }
            }

            // Threads down to the keys
            const auto keys = keyStrip();
            const int visible = std::min (n, maxKeys);
            const float keyW = keys.getWidth() / (float) std::max (1, visible);
            for (int i = 0; i < visible; ++i)
            {
                const auto& sl = slices[(size_t) i];
                const float x0 = xOf ((float) sl.start / (float) s->numFrames), x1 = xOf ((float) sl.end / (float) s->numFrames);
                const juce::Point<float> from ((x0 + x1) * 0.5f, w.getBottom() + 2.0f);
                const juce::Point<float> to (keys.getX() + keyW * ((float) i + 0.5f), keys.getY() - 2.0f);
                juce::Path thread;
                thread.startNewSubPath (from);
                const float mid = (from.y + to.y) * 0.5f;
                thread.cubicTo (from.x, mid, to.x, mid, to.x, to.y);
                const auto col = veilAt (dark, n > 1 ? (float) i / (float) (n - 1) : 0.0f);
                const float gl = glow[(size_t) i];
                g.setColour (col.withAlpha (0.25f + 0.6f * gl + (i == hovered ? 0.3f : 0.0f)));
                g.strokePath (thread, juce::PathStrokeType (1.0f + 2.0f * gl));
                if (gl > 0.01f)
                {
                    // a bead of light runs down the thread
                    const float t = std::fmod (time * 1.8f + (float) i * 0.13f, 1.0f);
                    const auto pt = thread.getPointAlongPath (thread.getLength() * t);
                    g.setColour (col.withAlpha (gl));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pt));
                }

                // the key
                auto key = juce::Rectangle<float> (keys.getX() + keyW * (float) i, keys.getY(), keyW, keys.getHeight()).reduced (1.5f, 0.0f);
                const int note = base + i;
                const bool black = juce::MidiMessage::isMidiNoteBlack (note);
                g.setColour ((black ? p.keyBlack : p.keyWhite).interpolatedWith (col, 0.15f + 0.6f * gl));
                g.fillRoundedRectangle (key, 3.0f);
                g.setColour (col.withAlpha (0.8f));
                g.fillRoundedRectangle (key.removeFromBottom (3.0f), 1.5f);
                if (keyW > 26.0f)
                {
                    g.setColour (black ? p.keyWhite.withAlpha (0.8f) : p.textDim);
                    g.setFont (font (9.5f));
                    g.drawText (noteName (note), key.withTrimmedBottom (4.0f), juce::Justification::centredBottom);
                }
            }
            if (n > maxKeys)
            {
                g.setColour (p.textDim);
                drawSpacedText (g, "+" + juce::String (n - maxKeys) + " MORE", keys.withX (keys.getRight() - 80.0f).withWidth (80.0f), 9.0f,
                                juce::Justification::centredRight, 0.15f);
            }
            if (n == 0)
            {
                g.setColour (p.textDim);
                g.setFont (font (13.0f));
                g.drawText ("No slices found - try a higher sensitivity or the grid", w, juce::Justification::centred);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = sliceAt (e.position.x);
            if (h != hovered) { hovered = h; repaint(); }
            setMouseCursor (markerAt (e.position.x) >= 1 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            draggingMarker = markerAt (e.position.x);
            if (draggingMarker >= 1)
            {
                if (e.mods.isPopupMenu())
                {
                    auto pts = currentPoints();
                    if (draggingMarker < (int) pts.size()) pts.erase (pts.begin() + draggingMarker);
                    commit (pts, true);
                    draggingMarker = -1;
                    return;
                }
                proc.undoManager.beginNewTransaction ("Move slice");
                return;
            }
            draggingMarker = -1;
            const int i = sliceAt (e.position.x);
            if (i < 0) return;
            const int base = juce::roundToInt (proc.apvts.getRawParameterValue ("chop_base")->load());
            playing = base + i;
            proc.keyboardState.noteOn (1, playing, 0.85f);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (draggingMarker < 1) return;
            auto pts = currentPoints();
            if (draggingMarker >= (int) pts.size()) return;
            const float lo = pts[(size_t) draggingMarker - 1] + 0.002f;
            const float hi = draggingMarker + 1 < (int) pts.size() ? pts[(size_t) draggingMarker + 1] - 0.002f : 0.999f;
            pts[(size_t) draggingMarker] = juce::jlimit (lo, hi, tOf (e.position.x));
            commit (pts, false);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (playing >= 0) proc.keyboardState.noteOff (1, playing, 0.0f);
            playing = -1;
            draggingMarker = -1;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! wave().contains (e.position)) return;
            auto pts = currentPoints();
            pts.push_back (tOf (e.position.x));
            std::sort (pts.begin(), pts.end());
            commit (pts, true);
        }

    private:
        static constexpr int maxKeys = 36;

        juce::Rectangle<float> wave() const { return getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (28.0f).withHeight ((float) getHeight() * 0.56f); }
        juce::Rectangle<float> keyStrip() const { return getLocalBounds().toFloat().reduced (16.0f, 0.0f).removeFromBottom (54.0f).withTrimmedBottom (12.0f); }
        float xOf (float t) const { const auto w = wave(); return w.getX() + t * w.getWidth(); }
        float tOf (float x) const { const auto w = wave(); return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / w.getWidth()); }

        int sliceAt (float x) const
        {
            const auto& snap = proc.instrument.snapshot();
            if (snap.chopSample == nullptr) return -1;
            const float t = tOf (x);
            for (int i = 0; i < (int) snap.slices.size(); ++i)
            {
                const auto& sl = snap.slices[(size_t) i];
                if (t * (float) snap.chopSample->numFrames >= (float) sl.start && t * (float) snap.chopSample->numFrames < (float) sl.end) return i;
            }
            return -1;
        }

        int markerAt (float x) const
        {
            const auto& snap = proc.instrument.snapshot();
            if (snap.chopSample == nullptr) return -1;
            for (int i = 1; i < (int) snap.slices.size(); ++i)
                if (std::abs (xOf ((float) snap.slices[(size_t) i].start / (float) snap.chopSample->numFrames) - x) < 5.0f) return i;
            return -1;
        }

        std::vector<float> currentPoints() const
        {
            const auto& snap = proc.instrument.snapshot();
            std::vector<float> pts;
            if (snap.chopSample == nullptr) return pts;
            for (const auto& sl : snap.slices) pts.push_back ((float) sl.start / (float) snap.chopSample->numFrames);
            return pts;
        }

        // Editing always ends in manual mode (the automatic markers become editable)
        void commit (const std::vector<float>& pts, bool undoable)
        {
            if (undoable) proc.undoManager.beginNewTransaction ("Edit slices");
            proc.instrument.setManualSlices (pts, true);
            if (mode.plain() < 2.5f) { mode.begin(); mode.setPlain (3.0f); mode.end(); }
            proc.instrument.rebuildNow();
            juce::ignoreUnused (undoable);
        }

        HuldraProcessor& proc;
        ParamDrag mode;
        std::array<float, 128> glow {};
        int hovered = -1, playing = -1, draggingMarker = -1;
        float time = 0.0f;
    };

    class SlicePage : public Page
    {
    public:
        explicit SlicePage (HuldraProcessor& p) : Page (p), view (p), chopsButton ("Play as chops", true)
        {
            auto& s = p.apvts;
            addAndMakeVisible (view);
            row.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::ChopMode), nullptr,
                                                            juce::StringArray { "Transients", "Syllables", "Grid", "Manual" }), "Find slices by"), 4.4f);
            row.add (makeKnob (s, P::ChopSens, "Sensitivity"));
            row.add (makeKnob (s, P::ChopCount, "Grid"));
            row.add (makeKnob (s, P::ChopBase, "First Key"));
            row.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::ChopPlay), nullptr, juce::StringArray { "One Shot", "Gate" }), "Playback"), 2.4f);
            row.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::ChopChoke), "Choke"), "Mono"), 1.1f);
            row.add (makeKnob (s, P::ChopFade, "Fade"));
            row.add (makeKnob (s, P::RetuneAmt, "Retune"));
            row.addTo (*this);
            addAndMakeVisible (chopsButton);
            chopsButton.onClick = [this]
            {
                auto* prm = proc.apvts.getParameter (params::paramId (params::PlayMode));
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (1.0f);
                prm->endChangeGesture();
            };
        }

        void tick() override
        {
            view.tick();
            const bool chops = proc.apvts.getRawParameterValue ("play_mode")->load() > 0.5f;
            chopsButton.setVisible (! chops);
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, controls.toFloat(), "Slices");
            if (chopsButton.isVisible())
            {
                g.setColour (Theme::get().palette().textDim);
                g.setFont (font (12.0f));
                g.drawText ("The keyboard plays the sound melodically right now.", chopsButton.getBounds().translated (-330, 0).withWidth (320).toFloat(),
                            juce::Justification::centredRight);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            controls = r.removeFromBottom (112);
            r.removeFromBottom (10);
            view.setBounds (r);
            row.layout (controls.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
            chopsButton.setBounds (controls.getRight() - 170, controls.getY() + 6, 156, 24);
        }

    private:
        SliceView view;
        ControlRow row;
        PillButton chopsButton;
        juce::Rectangle<int> controls;
    };
}
