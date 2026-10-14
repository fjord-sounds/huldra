#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // A big keyboard where every key can be selected and edited. Edited keys show what was changed.
    class KeyBoard : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
    {
    public:
        explicit KeyBoard (HuldraProcessor& p) : proc (p)
        {
            proc.instrument.addChangeListener (this);
            selected.insert (60);
            setTooltip ("Click a key to edit it (and hear it). Shift-click selects several keys, drag across to select a range.");
        }

        ~KeyBoard() override { proc.instrument.removeChangeListener (this); }

        std::set<int> selected;
        std::function<void()> onSelectionChanged;
        static constexpr int lowKey = 36, highKey = 96;

        void tick()
        {
            for (auto& a : activity) a = std::max (0.0f, a - 1.0f / 30.0f / 0.35f);
            auto& d = proc.voiceDisplay;
            for (int i = 0; i < dsp::VoiceDisplay::kSlots; ++i)
            {
                const float lvl = d.level[(size_t) i].load();
                if (lvl < 0.0f) break;
                const int n = d.note[(size_t) i].load();
                if (juce::isPositiveAndBelow (n, 128)) activity[(size_t) n] = std::max (activity[(size_t) n], std::min (1.0f, lvl + 0.3f));
            }
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto& snap = proc.instrument.snapshot();
            // white keys first, then black keys on top
            for (int pass = 0; pass < 2; ++pass)
                for (int n = lowKey; n <= highKey; ++n)
                {
                    const bool black = juce::MidiMessage::isMidiNoteBlack (n);
                    if (black != (pass == 1)) continue;
                    auto k = keyRect (n);
                    const auto& e = snap.keys[(size_t) n];
                    const bool sel = selected.count (n) > 0;
                    auto base = black ? p.keyBlack : p.keyWhite;
                    if (e.mute) base = base.interpolatedWith (p.background, 0.6f);
                    g.setColour (base.interpolatedWith (p.accent, 0.55f * activity[(size_t) n]));
                    g.fillRoundedRectangle (k, black ? 3.0f : 5.0f);
                    g.setColour (black ? p.background : p.line);
                    g.drawRoundedRectangle (k, black ? 3.0f : 5.0f, 1.0f);
                    if (sel)
                    {
                        g.setColour (p.accent);
                        g.drawRoundedRectangle (k.reduced (1.5f), black ? 3.0f : 5.0f, 2.0f);
                    }
                    drawEditGlyphs (g, k, e, black, dark);
                    if (! black && n % 12 == 0)
                    {
                        g.setColour (p.textDim);
                        g.setFont (font (10.0f));
                        g.drawText (noteName (n), k.withTrimmedBottom (6.0f), juce::Justification::centredBottom);
                    }
                }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int n = keyAt (e.position);
            if (n < 0) return;
            if (e.mods.isShiftDown())
            {
                if (selected.count (n) > 0 && selected.size() > 1) selected.erase (n); else selected.insert (n);
            }
            else
            {
                selected.clear();
                selected.insert (n);
            }
            anchor = n;
            playing = n;
            proc.keyboardState.noteOn (1, n, 0.8f);
            if (onSelectionChanged) onSelectionChanged();
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int n = keyAt (e.position);
            if (n < 0 || n == lastDrag) return;
            lastDrag = n;
            selected.clear();
            for (int k = std::min (anchor, n); k <= std::max (anchor, n); ++k) selected.insert (k);
            if (onSelectionChanged) onSelectionChanged();
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (playing >= 0) proc.keyboardState.noteOff (1, playing, 0.0f);
            playing = lastDrag = -1;
        }

    private:
        int numWhite() const
        {
            int n = 0;
            for (int k = lowKey; k <= highKey; ++k) n += juce::MidiMessage::isMidiNoteBlack (k) ? 0 : 1;
            return n;
        }

        juce::Rectangle<float> keyRect (int note) const
        {
            const float ww = (float) getWidth() / (float) numWhite();
            int whiteIndex = 0;
            for (int k = lowKey; k < note; ++k) whiteIndex += juce::MidiMessage::isMidiNoteBlack (k) ? 0 : 1;
            if (! juce::MidiMessage::isMidiNoteBlack (note))
                return { (float) whiteIndex * ww + 1.0f, 0.0f, ww - 2.0f, (float) getHeight() };
            return { (float) whiteIndex * ww - ww * 0.32f, 0.0f, ww * 0.64f, (float) getHeight() * 0.6f };
        }

        int keyAt (juce::Point<float> pos) const
        {
            for (int n = lowKey; n <= highKey; ++n)
                if (juce::MidiMessage::isMidiNoteBlack (n) && keyRect (n).contains (pos)) return n;
            for (int n = lowKey; n <= highKey; ++n)
                if (! juce::MidiMessage::isMidiNoteBlack (n) && keyRect (n).contains (pos)) return n;
            return -1;
        }

        void drawEditGlyphs (juce::Graphics& g, juce::Rectangle<float> k, const KeyEdit& e, bool black, bool dark)
        {
            const auto& p = Theme::get().palette();
            const auto ink = black ? p.keyWhite : p.text;
            auto area = k.reduced (3.0f).withTrimmedBottom (black ? 4.0f : 22.0f);
            if (! black) area = area.withTrimmedTop (k.getHeight() * 0.6f);
            float y = area.getY() + 4.0f;
            auto nextRow = [&] { auto r = juce::Rectangle<float> (area.getX(), y, area.getWidth(), 12.0f); y += 14.0f; return r; };
            if (std::abs (e.tune) > 0.001f)
            {
                g.setColour (ink);
                g.setFont (font (9.5f, Weight::SemiBold));
                g.drawText ((e.tune > 0 ? "+" : "") + juce::String (e.tune, std::abs (e.tune - std::round (e.tune)) < 0.01f ? 0 : 1),
                            nextRow(), juce::Justification::centred);
            }
            if (e.engine >= 0)
            {
                g.setColour (engineColour (e.engine, dark));
                g.fillEllipse (nextRow().withSizeKeepingCentre (7.0f, 7.0f));
            }
            if (e.reverse)
            {
                g.setColour (ink.withAlpha (0.85f));
                const auto r = nextRow().withSizeKeepingCentre (10.0f, 8.0f);
                juce::Path a;
                a.startNewSubPath (r.getRight(), r.getCentreY());
                a.lineTo (r.getX(), r.getCentreY());
                a.startNewSubPath (r.getX() + 3.0f, r.getY());
                a.lineTo (r.getX(), r.getCentreY());
                a.lineTo (r.getX() + 3.0f, r.getBottom());
                g.strokePath (a, juce::PathStrokeType (1.2f));
            }
            if (e.choke > 0)
            {
                const auto r = nextRow().withSizeKeepingCentre (12.0f, 12.0f);
                g.setColour (p.accent);
                g.drawEllipse (r.reduced (0.5f), 1.0f);
                g.setFont (font (8.5f, Weight::SemiBold));
                g.drawText (juce::String (e.choke), r, juce::Justification::centred);
            }
            if (e.mute)
            {
                const auto r = nextRow().withSizeKeepingCentre (8.0f, 8.0f);
                g.setColour (ink.withAlpha (0.7f));
                g.drawLine ({ r.getTopLeft(), r.getBottomRight() }, 1.3f);
                g.drawLine ({ r.getTopRight(), r.getBottomLeft() }, 1.3f);
            }
            // gain as a small bar at the bottom
            if (std::abs (e.gainDb) > 0.05f)
            {
                const float w = std::clamp (std::abs (e.gainDb) / 18.0f, 0.1f, 1.0f) * (k.getWidth() - 8.0f);
                g.setColour ((e.gainDb > 0 ? p.accent : p.textDim).withAlpha (0.9f));
                g.fillRoundedRectangle (k.getCentreX() - w * 0.5f, k.getBottom() - (black ? 6.0f : 20.0f), w, 2.5f, 1.0f);
            }
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        HuldraProcessor& proc;
        std::array<float, 128> activity {};
        int anchor = 60, playing = -1, lastDrag = -1;
    };

    // ---------------------------------------------------------------------------------------------
    // Twelve pitch classes on a circle: the scale is lit, the chord of the last note is drawn as a star.
    class ChordWheel : public juce::Component
    {
    public:
        explicit ChordWheel (HuldraProcessor& p) : proc (p) {}

        void tick()
        {
            auto& d = proc.voiceDisplay;
            std::array<bool, 12> now {};
            bool any = false;
            for (int i = 0; i < dsp::VoiceDisplay::kSlots; ++i)
            {
                const float lvl = d.level[(size_t) i].load();
                if (lvl < 0.0f) break;
                if (lvl > 0.05f) { now[(size_t) (d.note[(size_t) i].load() % 12)] = true; any = true; }
            }
            for (int i = 0; i < 12; ++i) lit[(size_t) i] = now[(size_t) i] ? 1.0f : std::max (0.0f, lit[(size_t) i] - 0.05f);
            if (any) active = now;
            spin += 0.004f;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (8.0f);
            const auto c = r.getCentre();
            const float rad = std::min (r.getWidth(), r.getHeight()) * 0.42f;
            auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
            const int root = juce::roundToInt (v ("scl_root")), type = juce::roundToInt (v ("scl_type"));
            const bool snap = v ("scl_on") > 0.5f;
            auto pt = [&] (int pc, float rr) { return c.getPointOnCircumference (rr, (float) pc / 12.0f * juce::MathConstants<float>::twoPi); };

            g.setColour (p.line);
            g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.0f);

            // chord star between the sounding pitch classes
            juce::Path star;
            bool first = true;
            for (int i = 0; i < 12; ++i)
                if (active[(size_t) i] && lit[(size_t) i] > 0.01f)
                {
                    if (first) star.startNewSubPath (pt (i, rad)); else star.lineTo (pt (i, rad));
                    first = false;
                }
            if (! first)
            {
                star.closeSubPath();
                g.setColour (p.accent.withAlpha (0.18f));
                g.fillPath (star);
                g.setColour (p.accent.withAlpha (0.8f));
                g.strokePath (star, juce::PathStrokeType (1.4f));
            }

            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int i = 0; i < 12; ++i)
            {
                const bool inScale = motion::inScale (i, root, type);
                const auto at = pt (i, rad);
                const float size = 9.0f + 8.0f * lit[(size_t) i];
                const auto col = veilAt (dark, (float) ((i - root + 12) % 12) / 12.0f);
                if (lit[(size_t) i] > 0.01f)
                {
                    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.5f * lit[(size_t) i]), at, col.withAlpha (0.0f), at.translated (size * 1.6f, 0.0f), true));
                    g.fillEllipse (juce::Rectangle<float> (size * 3.2f, size * 3.2f).withCentre (at));
                }
                g.setColour (inScale && snap ? col : (inScale ? col.withAlpha (0.45f) : p.panel));
                g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (at));
                g.setColour (inScale ? col : p.line);
                g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (at), 1.0f);
                g.setColour (i == root ? p.text : p.textDim);
                g.setFont (font (10.0f, i == root));
                g.drawText (names[i], juce::Rectangle<float> (24.0f, 12.0f).withCentre (pt (i, rad + 17.0f)), juce::Justification::centred);
            }
            g.setColour (p.textDim);
            drawSpacedText (g, snap ? "SCALE SNAP ON" : "SCALE SNAP OFF", juce::Rectangle<float> (120.0f, 14.0f).withCentre (c), 8.5f,
                            juce::Justification::centred, 0.2f);
        }

    private:
        HuldraProcessor& proc;
        std::array<float, 12> lit {};
        std::array<bool, 12> active {};
        float spin = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    class KeysPage : public Page, private juce::ChangeListener
    {
    public:
        explicit KeysPage (HuldraProcessor& p)
            : Page (p), board (p), wheel (p),
              keysLock (p.apvts, locks::ids().keys), scaleLock (p.apvts, locks::ids().scale),
              tune ("Tune", -24, 24, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 2) + " st"; }),
              gain ("Gain", -36, 12, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }),
              pan ("Pan", -1, 1, 0, [] (double v) { const int x = juce::roundToInt (v * 100.0); return x == 0 ? juce::String ("C") : (x < 0 ? "L" + juce::String (-x) : "R" + juce::String (x)); }),
              cutoff ("Brightness", -6, 6, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " oct"; }),
              start ("Start", 0, 0.95, 0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }),
              formant ("Formant", -24, 24, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " st"; }),
              choke ("Choke", 0, 8, 0, [] (double v) { return v < 0.5 ? juce::String ("none") : "group " + juce::String ((int) v); }),
              slice ("Slice", -1, 127, -1, [] (double v) { return v < -0.5 ? juce::String ("auto") : "slice " + juce::String ((int) v + 1); }),
              engine ({ "Zone", "Classic", "Grain", "Spectral", "Vocal" }),
              flags ({ "Normal", "Reverse", "Mute" }),
              resetKey ("Reset", false), resetAll ("Reset all keys", false)
        {
            auto& s = p.apvts;
            addAndMakeVisible (board);
            addAndMakeVisible (wheel);
            addAndMakeVisible (keysLock);
            addAndMakeVisible (scaleLock);
            for (auto* k : { &tune, &gain, &pan, &cutoff, &start, &formant, &choke, &slice })
            {
                addAndMakeVisible (*k);
                k->onDragStart = [this] { proc.undoManager.beginNewTransaction ("Edit key"); };
                k->onChange = [this, k] (double, bool) { push (k); };
            }
            choke.setInterval (1);
            slice.setInterval (1);
            tune.setInterval (0.01);
            addAndMakeVisible (engine);
            addAndMakeVisible (flags);
            engine.onChange = [this] (int) { proc.undoManager.beginNewTransaction ("Key engine"); push (&engine); };
            flags.onChange = [this] (int) { proc.undoManager.beginNewTransaction ("Key flags"); push (&flags); };
            addAndMakeVisible (resetKey);
            addAndMakeVisible (resetAll);
            resetKey.onClick = [this]
            {
                proc.undoManager.beginNewTransaction ("Reset key");
                for (int n : board.selected) proc.instrument.setKey (n, {});
            };
            resetAll.onClick = [this] { proc.undoManager.beginNewTransaction ("Reset keys"); proc.instrument.resetKeys(); };
            board.onSelectionChanged = [this] { pull(); };

            scale.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::SclOn), "Snap"), "Scale"), 1.0f);
            scale.add (labelled (std::make_unique<Choice> (s, P::paramId (P::SclRoot)), "Key"), 1.0f);
            scale.add (labelled (std::make_unique<Choice> (s, P::paramId (P::SclType)), "Scale"), 2.0f);
            scale.addTo (*this);
            chordSel.add (labelled (std::make_unique<Choice> (s, P::paramId (P::ChordType)), "Chord"), 2.0f);
            chordSel.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::VoiceMode)), "Voices"), 2.4f);
            chordSel.addTo (*this);
            chords.add (makeKnob (s, P::ChordInv, "Inversion"));
            chords.add (makeKnob (s, P::ChordSpread, "Spread"));
            chords.add (makeKnob (s, P::ChordStrum, "Strum"));
            chords.add (makeKnob (s, P::RetuneAmt, "Retune"));
            chords.addTo (*this);
            proc.instrument.addChangeListener (this);
            pull();
        }

        ~KeysPage() override { proc.instrument.removeChangeListener (this); }

        void tick() override { board.tick(); wheel.tick(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            juce::String title = "Key " + noteName (*board.selected.begin());
            if (board.selected.size() > 1) title = juce::String ((int) board.selected.size()) + " keys";
            drawPanel (g, editArea.toFloat(), title);
            drawPanel (g, playArea.toFloat(), "Scale and chords");
            g.setColour (p.textDim);
            drawSpacedText (g, "ENGINE", engine.getBounds().toFloat().translated (0.0f, -16.0f).withHeight (14.0f), 9.5f, juce::Justification::centredLeft, 0.16f);
            drawSpacedText (g, "PLAY", flags.getBounds().toFloat().translated (0.0f, -16.0f).withHeight (14.0f), 9.5f, juce::Justification::centredLeft, 0.16f);
            g.setFont (font (11.5f));
            g.drawText ("MPE: pitch bend, pressure and slide (CC 74) work per note.", playArea.reduced (16, 10).removeFromBottom (16).toFloat(),
                        juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            board.setBounds (r.removeFromTop (190).reduced (2, 0));
            r.removeFromTop (12);
            editArea = r.removeFromLeft (r.getWidth() * 55 / 100);
            r.removeFromLeft (10);
            playArea = r;
            keysLock.place (editArea);
            scaleLock.place (playArea);

            auto e = editArea.reduced (12, 0).withTrimmedTop (34).withTrimmedBottom (10);
            auto knobs = e.removeFromTop (84);
            layoutRow (knobs, { &tune, &gain, &pan, &cutoff, &start, &formant, &choke, &slice });
            e.removeFromTop (28);
            auto line = e.removeFromTop (26);
            engine.setBounds (line.removeFromLeft (line.getWidth() * 55 / 100));
            line.removeFromLeft (16);
            flags.setBounds (line);
            auto buttons = e.removeFromBottom (32);
            resetAll.setBounds (buttons.removeFromRight (150));
            buttons.removeFromRight (8);
            resetKey.setBounds (buttons.removeFromRight (100));

            auto pa = playArea.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (28);
            wheel.setBounds (pa.removeFromRight (std::min (230, pa.getHeight() + 10)));
            pa.removeFromRight (10);
            scale.layout (pa.removeFromTop (54).translated (0, 8));
            chordSel.layout (pa.removeFromTop (54).translated (0, 8));
            chords.layout (pa.withTrimmedTop (10).removeFromTop (92));
        }

    private:
        void pull()
        {
            if (board.selected.empty()) return;
            const auto e = proc.instrument.getKey (*board.selected.begin());
            pulling = true;
            tune.setValue (e.tune); gain.setValue (e.gainDb); pan.setValue (e.pan); cutoff.setValue (e.cutoff);
            start.setValue (e.start); formant.setValue (e.formant); choke.setValue (e.choke); slice.setValue (e.slice);
            engine.setSelected (e.engine + 1);
            flags.setSelected (e.mute ? 2 : (e.reverse ? 1 : 0));
            pulling = false;
            repaint();
        }

        // Applies the changed control to every selected key (other settings of those keys stay)
        void push (juce::Component* changed)
        {
            if (pulling) return;
            for (int n : board.selected)
            {
                auto e = proc.instrument.getKey (n);
                if (changed == &tune) e.tune = (float) tune.getValue();
                if (changed == &gain) e.gainDb = (float) gain.getValue();
                if (changed == &pan) e.pan = (float) pan.getValue();
                if (changed == &cutoff) e.cutoff = (float) cutoff.getValue();
                if (changed == &start) e.start = (float) start.getValue();
                if (changed == &formant) e.formant = (float) formant.getValue();
                if (changed == &choke) e.choke = (int) choke.getValue();
                if (changed == &slice) e.slice = (int) slice.getValue();
                if (changed == &engine) e.engine = engine.getSelected() - 1;
                if (changed == &flags) { e.reverse = flags.getSelected() == 1; e.mute = flags.getSelected() == 2; }
                proc.instrument.setKey (n, e, true);
            }
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { pull(); }

        KeyBoard board;
        ChordWheel wheel;
        PanelLock keysLock, scaleLock;   // keep the key edits / scale and chords when switching presets
        ValueKnob tune, gain, pan, cutoff, start, formant, choke, slice;
        ChoiceChips engine, flags;
        PillButton resetKey, resetAll;
        ControlRow scale, chordSel, chords;
        juce::Rectangle<int> editArea, playArea;
        bool pulling = false;
    };
}
