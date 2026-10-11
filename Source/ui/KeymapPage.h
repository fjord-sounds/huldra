#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // Keys across, velocity up. Zones can be moved and resized; the dot on the key strip is the root.
    class ZoneMap : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
    {
    public:
        explicit ZoneMap (HuldraProcessor& p) : proc (p)
        {
            proc.instrument.addChangeListener (this);
            setTooltip ("Drag a zone to move it, its edges to resize, the dot on the keys to set the root. Double-click a key to play it.");
        }

        ~ZoneMap() override { proc.instrument.removeChangeListener (this); }

        std::function<void (int)> onSelect;
        int selected = 0;

        void tick()
        {
            for (auto& a : activity) a = std::max (0.0f, a - 1.0f / 30.0f / 0.4f);
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
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

            const auto grid = gridArea();
            // octave lines and velocity guides
            for (int n = lowKey; n <= highKey; ++n)
                if (n % 12 == 0)
                {
                    g.setColour (p.line.withAlpha (0.8f));
                    g.fillRect (xOf (n), grid.getY(), 1.0f, grid.getHeight());
                    g.setColour (p.textDim);
                    g.setFont (font (9.5f));
                    g.drawText (noteName (n), juce::Rectangle<float> (xOf (n) + 3.0f, grid.getY() + 2.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
                }
            for (int v : { 32, 64, 96 })
            {
                g.setColour (p.line.withAlpha (0.4f));
                g.fillRect (grid.getX(), yOf (v), grid.getWidth(), 1.0f);
            }

            // live notes: glowing columns
            for (int n = lowKey; n <= highKey; ++n)
                if (activity[(size_t) n] > 0.01f)
                {
                    const float x = xOf (n), w = keyW();
                    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.0f), 0.0f, grid.getY(), p.accent.withAlpha (0.3f * activity[(size_t) n]),
                                                             0.0f, grid.getBottom(), false));
                    g.fillRect (x, grid.getY(), w, grid.getHeight());
                }

            const auto zones = proc.instrument.getZones();
            const auto samples = proc.instrument.getSamples();
            auto hueOf = [&] (const juce::String& uid)
            {
                for (size_t i = 0; i < samples.size(); ++i)
                    if (samples[i]->uid == uid) return samples.size() > 1 ? (float) i / (float) (samples.size() - 1) : 0.0f;
                return 0.0f;
            };
            for (int i = 0; i < (int) zones.size(); ++i)
            {
                const auto& z = zones[(size_t) i];
                auto zr = zoneRect (z);
                const auto col = veilAt (dark, hueOf (z.sample));
                const bool sel = i == selected;
                g.setColour (col.withAlpha (sel ? 0.3f : 0.16f));
                g.fillRoundedRectangle (zr, 5.0f);
                g.setColour (sel ? p.text : col);
                g.drawRoundedRectangle (zr.reduced (0.5f), 5.0f, sel ? 1.6f : 1.0f);
                if (auto s = proc.instrument.getSample (z.sample))
                {
                    if (zr.getWidth() > 50.0f && zr.getHeight() > 30.0f)
                    {
                        juce::Graphics::ScopedSaveState ss (g);
                        g.reduceClipRegion (zr.toNearestInt());
                        drawWaveform (g, *s, zr.reduced (4.0f, zr.getHeight() * 0.25f), 0.0f, 1.0f, 0.35f, false);
                        g.setColour (p.text);
                        g.setFont (font (11.5f, sel));
                        g.drawText (s->name, zr.reduced (7.0f, 4.0f), juce::Justification::topLeft, true);
                    }
                }
                // root dot on the key strip
                const float rx = xOf ((int) std::floor (z.root)) + keyW() * (0.5f + (z.root - std::floor (z.root)));
                const auto keys = keyStrip();
                g.setColour (col);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ rx, keys.getY() - 7.0f }));
                g.setColour (col.withAlpha (0.35f));
                g.drawLine (rx, zr.getBottom(), rx, keys.getY() - 11.0f, 1.0f);
            }

            // key strip
            const auto keys = keyStrip();
            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path clipKeys;
                clipKeys.addRoundedRectangle (keys, 4.0f);
                g.reduceClipRegion (clipKeys);
                g.setColour (p.keyWhite);
                g.fillRect (keys);
                for (int n = lowKey; n <= highKey; ++n)
                {
                    const bool black = juce::MidiMessage::isMidiNoteBlack (n);
                    const float a = activity[(size_t) n];
                    if (! black)
                    {
                        if (a > 0.01f)
                        {
                            g.setColour (p.accent.withAlpha (0.6f * a));
                            g.fillRect (juce::Rectangle<float> (xOf (n), keys.getY(), keyW(), keys.getHeight()));
                        }
                        if (! juce::MidiMessage::isMidiNoteBlack (n + 1))
                        {
                            g.setColour (p.line);
                            g.fillRect (xOf (n + 1) - 0.5f, keys.getY(), 1.0f, keys.getHeight());
                        }
                        continue;
                    }
                    // black keys: shorter, from the top
                    auto k = juce::Rectangle<float> (xOf (n) - keyW() * 0.15f, keys.getY(), keyW() * 1.3f, keys.getHeight() * 0.6f);
                    g.setColour (p.keyBlack.interpolatedWith (p.accent, 0.7f * a));
                    g.fillRoundedRectangle (k, 1.5f);
                }
                g.setColour (p.line);
                g.drawRoundedRectangle (keys, 4.0f, 1.0f);
            }

            if (zones.empty())
            {
                g.setColour (p.textDim);
                g.setFont (font (14.0f));
                g.drawText ("The demo voice plays across the whole keyboard.  Load or drop sounds to build your own keymap.", grid,
                            juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto zones = proc.instrument.getZones();
            drag = Drag::None;
            // Root dots first
            for (int i = (int) zones.size(); --i >= 0;)
            {
                const float rx = xOf ((int) std::floor (zones[(size_t) i].root)) + keyW() * 0.5f;
                if (std::abs (e.position.x - rx) < 6.0f && std::abs (e.position.y - (keyStrip().getY() - 7.0f)) < 7.0f)
                {
                    select (i);
                    drag = Drag::Root;
                    break;
                }
            }
            if (drag == Drag::None)
                for (int i = (int) zones.size(); --i >= 0;)
                {
                    const auto zr = zoneRect (zones[(size_t) i]);
                    if (! zr.expanded (4.0f).contains (e.position)) continue;
                    select (i);
                    if (std::abs (e.position.x - zr.getX()) < 6.0f) drag = Drag::Left;
                    else if (std::abs (e.position.x - zr.getRight()) < 6.0f) drag = Drag::Right;
                    else if (std::abs (e.position.y - zr.getY()) < 5.0f) drag = Drag::Top;
                    else if (std::abs (e.position.y - zr.getBottom()) < 5.0f) drag = Drag::Bottom;
                    else drag = Drag::Move;
                    break;
                }
            if (drag != Drag::None)
            {
                proc.undoManager.beginNewTransaction ("Edit zone");
                startZone = proc.instrument.getZones()[(size_t) selected];
                startKey = keyAt (e.position.x);
            }
            else if (keyStrip().contains (e.position))
            {
                playing = keyAt (e.position.x);
                proc.keyboardState.noteOn (1, playing, 0.85f);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag == Drag::None) return;
            auto z = startZone;
            const int key = keyAt (e.position.x);
            const int vel = velAt (e.position.y);
            switch (drag)
            {
                case Drag::Move:
                {
                    const int d = std::clamp (key - startKey, -z.lo, 127 - z.hi);
                    z.lo += d; z.hi += d; z.root += (float) d;
                    break;
                }
                case Drag::Left:   z.lo = std::min (key, z.hi); break;
                case Drag::Right:  z.hi = std::max (key, z.lo); break;
                case Drag::Top:    z.vhi = std::max (vel, z.vlo); break;
                case Drag::Bottom: z.vlo = std::min (vel, z.vhi); break;
                case Drag::Root:   z.root = (float) key + (startZone.root - std::floor (startZone.root)); break;
                case Drag::None:
                default: break;
            }
            proc.instrument.setZone (selected, z, true);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            drag = Drag::None;
            if (playing >= 0) proc.keyboardState.noteOff (1, playing, 0.0f);
            playing = -1;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto zones = proc.instrument.getZones();
            auto cursor = juce::MouseCursor::NormalCursor;
            for (int i = (int) zones.size(); --i >= 0;)
            {
                const auto zr = zoneRect (zones[(size_t) i]);
                if (! zr.expanded (4.0f).contains (e.position)) continue;
                if (std::abs (e.position.x - zr.getX()) < 6.0f || std::abs (e.position.x - zr.getRight()) < 6.0f) cursor = juce::MouseCursor::LeftRightResizeCursor;
                else if (std::abs (e.position.y - zr.getY()) < 5.0f || std::abs (e.position.y - zr.getBottom()) < 5.0f) cursor = juce::MouseCursor::UpDownResizeCursor;
                else cursor = juce::MouseCursor::DraggingHandCursor;
                break;
            }
            if (keyStrip().contains (e.position)) cursor = juce::MouseCursor::PointingHandCursor;
            setMouseCursor (cursor);
        }

    private:
        enum class Drag { None, Move, Left, Right, Top, Bottom, Root };
        static constexpr int lowKey = 24, highKey = 108;

        juce::Rectangle<float> gridArea() const { return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (14.0f).withTrimmedBottom (62.0f); }
        juce::Rectangle<float> keyStrip() const { return getLocalBounds().toFloat().reduced (14.0f, 0.0f).removeFromBottom (44.0f).withTrimmedBottom (12.0f); }
        float keyW() const { return gridArea().getWidth() / (float) (highKey - lowKey + 1); }
        float xOf (int note) const { return gridArea().getX() + (float) (note - lowKey) * keyW(); }
        float yOf (int vel) const { const auto g = gridArea(); return g.getBottom() - (float) vel / 127.0f * g.getHeight(); }
        int keyAt (float x) const { return juce::jlimit (0, 127, lowKey + (int) std::floor ((x - gridArea().getX()) / keyW())); }
        int velAt (float y) const { const auto g = gridArea(); return juce::jlimit (1, 127, (int) std::round ((g.getBottom() - y) / g.getHeight() * 127.0f)); }

        juce::Rectangle<float> zoneRect (const ZoneInfo& z) const
        {
            const float x0 = xOf (std::max (lowKey, z.lo)), x1 = xOf (std::min (highKey, z.hi) + 1);
            return { x0 + 1.0f, yOf (z.vhi), std::max (4.0f, x1 - x0 - 2.0f), yOf (z.vlo - 1) - yOf (z.vhi) };
        }

        void select (int i)
        {
            selected = i;
            if (onSelect) onSelect (i);
            repaint();
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            const int n = (int) proc.instrument.getZones().size();
            if (selected >= n) selected = std::max (0, n - 1);
            repaint();
        }

        HuldraProcessor& proc;
        std::array<float, 128> activity {};
        Drag drag = Drag::None;
        ZoneInfo startZone;
        int startKey = 0, playing = -1;
    };

    // ---------------------------------------------------------------------------------------------
    class KeymapPage : public Page, private juce::ChangeListener
    {
    public:
        explicit KeymapPage (HuldraProcessor& p)
            : Page (p), map (p),
              lo ("Low Key", 0, 127, 0, [] (double v) { return noteName ((int) v); }),
              hi ("High Key", 0, 127, 127, [] (double v) { return noteName ((int) v); }),
              root ("Root", 0, 127, 60, [] (double v) { return noteName (juce::roundToInt (v)) + " " + juce::String (juce::roundToInt ((v - std::round (v)) * 100.0)) + " ct"; }),
              tune ("Tune", -24, 24, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 2) + " st"; }),
              gain ("Gain", -36, 12, 0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }),
              pan ("Pan", -1, 1, 0, [] (double v) { const int x = juce::roundToInt (v * 100.0); return x == 0 ? juce::String ("C") : (x < 0 ? "L" + juce::String (-x) : "R" + juce::String (x)); }),
              vlo ("Vel Low", 1, 127, 1, [] (double v) { return juce::String ((int) v); }),
              vhi ("Vel High", 1, 127, 127, [] (double v) { return juce::String ((int) v); }),
              engine ({ "Global", "Classic", "Grain", "Spectral", "Vocal" }),
              autoMap ("Auto map", false), remove ("Remove", false), add ("Add sounds", true)
        {
            addAndMakeVisible (map);
            for (auto* k : { &lo, &hi, &root, &tune, &gain, &pan, &vlo, &vhi })
            {
                addAndMakeVisible (*k);
                k->onDragStart = [this] { proc.undoManager.beginNewTransaction ("Edit zone"); };
                k->onChange = [this] (double, bool) { pushZone(); };
            }
            lo.setInterval (1); hi.setInterval (1); vlo.setInterval (1); vhi.setInterval (1);
            addAndMakeVisible (engine);
            engine.onChange = [this] (int) { proc.undoManager.beginNewTransaction ("Zone engine"); pushZone(); };
            addAndMakeVisible (autoMap);
            addAndMakeVisible (remove);
            addAndMakeVisible (add);
            autoMap.onClick = [this] { proc.undoManager.beginNewTransaction ("Auto map"); proc.instrument.autoMap(); };
            remove.onClick = [this]
            {
                const auto zones = proc.instrument.getZones();
                if (! juce::isPositiveAndBelow (map.selected, (int) zones.size())) return;
                proc.undoManager.beginNewTransaction ("Remove zone");
                proc.instrument.removeZone (map.selected);
            };
            add.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Add sounds", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                               "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          if (! fc.getResults().isEmpty())
                                              proc.instrument.loadFilesAsync (fc.getResults(), InstrumentState::AddMode::AddAutoMap);
                                      });
            };
            map.onSelect = [this] (int) { pullZone(); };
            proc.instrument.addChangeListener (this);
            pullZone();
        }

        ~KeymapPage() override { proc.instrument.removeChangeListener (this); }

        void tick() override { map.tick(); }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, side.toFloat(), "Zone");
            const auto zones = proc.instrument.getZones();
            auto info = side.reduced (16, 0).withTrimmedTop (30).removeFromTop (22).toFloat();
            g.setColour (Theme::get().palette().textDim);
            g.setFont (font (12.0f));
            if (juce::isPositiveAndBelow (map.selected, (int) zones.size()))
            {
                const auto s = proc.instrument.getSample (zones[(size_t) map.selected].sample);
                g.drawText (s != nullptr ? s->name + "   " + juce::String (s->seconds(), 2) + " s" : juce::String ("(loading)"), info, juce::Justification::centredLeft, true);
            }
            else
                g.drawText ("No zone selected", info, juce::Justification::centredLeft);
            g.setColour (Theme::get().palette().textDim);
            drawSpacedText (g, "ENGINE", engine.getBounds().toFloat().translated (0.0f, -16.0f).withHeight (14.0f), 9.5f, juce::Justification::centredLeft, 0.16f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            side = r.removeFromRight (330);
            r.removeFromRight (10);
            map.setBounds (r);
            auto s = side.reduced (12, 0).withTrimmedTop (58);
            auto row1 = s.removeFromTop (84);
            layoutRow (row1, { &lo, &hi, &root, &tune });
            s.removeFromTop (8);
            auto row2 = s.removeFromTop (84);
            layoutRow (row2, { &gain, &pan, &vlo, &vhi });
            s.removeFromTop (26);
            engine.setBounds (s.removeFromTop (26));
            auto buttons = s.removeFromBottom (44).withTrimmedBottom (12);
            const int w = (buttons.getWidth() - 16) / 3;
            add.setBounds (buttons.removeFromLeft (w));
            buttons.removeFromLeft (8);
            autoMap.setBounds (buttons.removeFromLeft (w));
            buttons.removeFromLeft (8);
            remove.setBounds (buttons);
        }

    private:
        void pullZone()
        {
            const auto zones = proc.instrument.getZones();
            const bool valid = juce::isPositiveAndBelow (map.selected, (int) zones.size());
            for (auto* c : std::initializer_list<juce::Component*> { &lo, &hi, &root, &tune, &gain, &pan, &vlo, &vhi, &engine, &remove })
                c->setEnabled (valid);
            if (! valid) return;
            pulling = true;
            const auto& z = zones[(size_t) map.selected];
            lo.setValue (z.lo); hi.setValue (z.hi); root.setValue (z.root); tune.setValue (z.tune);
            gain.setValue (z.gainDb); pan.setValue (z.pan); vlo.setValue (z.vlo); vhi.setValue (z.vhi);
            engine.setSelected (z.engine + 1);
            pulling = false;
            repaint();
        }

        void pushZone()
        {
            if (pulling) return;
            const auto zones = proc.instrument.getZones();
            if (! juce::isPositiveAndBelow (map.selected, (int) zones.size())) return;
            auto z = zones[(size_t) map.selected];
            z.lo = (int) lo.getValue(); z.hi = (int) hi.getValue(); z.root = (float) root.getValue(); z.tune = (float) tune.getValue();
            z.gainDb = (float) gain.getValue(); z.pan = (float) pan.getValue(); z.vlo = (int) vlo.getValue(); z.vhi = (int) vhi.getValue();
            z.engine = engine.getSelected() - 1;
            proc.instrument.setZone (map.selected, z, true);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { pullZone(); }

        ZoneMap map;
        ValueKnob lo, hi, root, tune, gain, pan, vlo, vhi;
        ChoiceChips engine;
        PillButton autoMap, remove, add;
        juce::Rectangle<int> side;
        std::unique_ptr<juce::FileChooser> chooser;
        bool pulling = false;
    };
}
