#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Theme.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace huldra::ui
{
    // Small pill button (A / B, copy, options)
    class SmallButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SmallButton (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void()> onClick;
        std::function<void (const juce::MouseEvent&)> onRightClick;
        bool active = false;
        void setText (const juce::String& t) { text = t; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            if (active)
            {
                g.setColour (p.accentSoft);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (active ? p.accent : (hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            g.setColour (active ? p.accent : (hover ? p.text : p.text.withAlpha (0.8f)));
            drawSpacedText (g, text, r, 10.0f, juce::Justification::centred, 0.18f, active);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! getLocalBounds().contains (e.getPosition())) return;
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (e); }
            else if (onClick) onClick();
        }

    private:
        juce::String text;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Pitch and mod wheels: soft vertical strips. Pitch springs back to the centre.
    class Wheel : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Wheel (HuldraProcessor& p, bool isPitch) : proc (p), pitch (isPitch)
        {
            setTooltip (pitch ? "Pitch bend" : "Mod wheel (a modulation source)");
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, r.getWidth() * 0.5f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, r.getWidth() * 0.5f, 1.0f);
            const auto inner = r.reduced (3.0f, 6.0f);
            const float y = inner.getBottom() - inner.getHeight() * (pitch ? (value * 0.5f + 0.5f) : value);
            const float from = pitch ? inner.getCentreY() : inner.getBottom();
            g.setGradientFill (juce::ColourGradient (veilAt (dark, 0.0f).withAlpha (0.65f), 0.0f, inner.getBottom(),
                                                     veilAt (dark, 0.7f).withAlpha (0.65f), 0.0f, inner.getY(), false));
            g.fillRoundedRectangle (inner.withY (std::min (y, from)).withHeight (std::abs (from - y) + 0.01f), 3.0f);
            g.setColour (p.text);
            g.fillRoundedRectangle (inner.getX(), y - 1.5f, inner.getWidth(), 3.0f, 1.5f);
            if (pitch)
            {
                g.setColour (p.textDim.withAlpha (0.5f));
                g.fillRect (inner.getX() + 2.0f, inner.getCentreY() - 0.5f, inner.getWidth() - 4.0f, 1.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override { startValue = value; juce::ignoreUnused (e); }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float range = pitch ? 2.0f : 1.0f;
            value = juce::jlimit (pitch ? -1.0f : 0.0f, 1.0f, startValue - (float) e.getDistanceFromDragStartY() / (float) getHeight() * range);
            send();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (pitch) { value = 0.0f; send(); }
        }

    private:
        void send()
        {
            if (pitch) proc.uiPitchBend.store (value);
            else       proc.uiModWheel.store (value);
            repaint();
        }

        HuldraProcessor& proc;
        bool pitch;
        float value = 0.0f, startValue = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Morph pad: X / Y are modulation sources (and default macro targets). Drift moves it on its own;
    // the trail shows where the sound has been.
    class MorphPad : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MorphPad (HuldraProcessor& p) : proc (p)
        {
            setTooltip ("Morph pad: drag to move X / Y (route them in the Mod page). Right-click: drift options");
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            x = proc.apvts.getParameter (params::paramId (params::PadX));
            y = proc.apvts.getParameter (params::paramId (params::PadY));
            drift = proc.apvts.getParameter (params::paramId (params::PadDrift));
        }

        void tick()
        {
            const juce::Point<float> pos (proc.getLivePadX(), proc.getLivePadY());
            if (trail.empty() || trail.back().getDistanceFrom (pos) > 0.002f)
            {
                trail.push_back (pos);
                if (trail.size() > 48) trail.erase (trail.begin());
                repaint();
            }
            phase += 1.0f / 30.0f;
            if (drift->getValue() > 0.001f) repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            {
                juce::Graphics::ScopedSaveState s (g);
                juce::Path clip;
                clip.addRoundedRectangle (r, 10.0f);
                g.reduceClipRegion (clip);
                // grid of dots
                g.setColour (p.line);
                for (int i = 1; i < 6; ++i)
                    for (int j = 1; j < 4; ++j)
                        g.fillEllipse (r.getX() + r.getWidth() * (float) i / 6.0f - 1.0f, r.getY() + r.getHeight() * (float) j / 4.0f - 1.0f, 2.0f, 2.0f);
                // trail
                for (size_t i = 1; i < trail.size(); ++i)
                {
                    const float a = (float) i / (float) trail.size();
                    g.setColour (veilAt (dark, 1.0f - a).withAlpha (a * 0.7f));
                    g.drawLine ({ toScreen (trail[i - 1], r), toScreen (trail[i], r) }, 1.0f + a * 1.5f);
                }
                if (! trail.empty())
                {
                    const auto c = toScreen (trail.back(), r);
                    const float glow = 18.0f + 4.0f * std::sin (phase * 2.0f);
                    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.4f), c, p.accent.withAlpha (0.0f), c.translated (glow, 0.0f), true));
                    g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (c));
                    g.setColour (p.accent);
                    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
                    g.setColour (p.panel);
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (c));
                }
            }
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
            g.setColour (p.textDim);
            drawSpacedText (g, drift->getValue() > 0.001f ? "MORPH  ~" : "MORPH", r.reduced (10.0f, 6.0f).removeFromTop (12.0f), 8.5f,
                            juce::Justification::centredLeft, 0.25f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return showMenu();
            x->beginChangeGesture();
            y->beginChangeGesture();
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            const auto r = getLocalBounds().toFloat().reduced (6.0f);
            x->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()));
            y->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight()));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            x->endChangeGesture();
            y->endChangeGesture();
        }

    private:
        static juce::Point<float> toScreen (juce::Point<float> v, juce::Rectangle<float> r)
        {
            const auto in = r.reduced (6.0f);
            return { in.getX() + v.x * in.getWidth(), in.getBottom() - v.y * in.getHeight() };
        }

        void showMenu()
        {
            juce::PopupMenu m;
            const float d = drift->getValue();
            m.addItem (1, "No drift", true, d < 0.001f);
            m.addItem (2, "Gentle drift", true, std::abs (d - 0.25f) < 0.01f);
            m.addItem (3, "Wide drift", true, std::abs (d - 0.6f) < 0.01f);
            m.addItem (4, "Wander everywhere", true, std::abs (d - 1.0f) < 0.01f);
            m.addSeparator();
            m.addItem (5, "Centre");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                auto set = [] (juce::RangedAudioParameter* prm, float v) { prm->beginChangeGesture(); prm->setValueNotifyingHost (v); prm->endChangeGesture(); };
                if (r >= 1 && r <= 4) set (drift, std::array<float, 4> { 0.0f, 0.25f, 0.6f, 1.0f }[(size_t) r - 1]);
                if (r == 5) { set (x, 0.5f); set (y, 0.5f); }
            });
        }

        HuldraProcessor& proc;
        juce::RangedAudioParameter *x = nullptr, *y = nullptr, *drift = nullptr;
        std::vector<juce::Point<float>> trail;
        float phase = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Renders the current sound (C4 held for two seconds plus the tail) and drags it out as a WAV, or saves it.
    class ExportTile : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ExportTile (HuldraProcessor& p) : proc (p)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip ("Drag the sound (C4, 2 s) into your DAW as a WAV, or click to save it");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (hover ? p.textDim : p.line);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);

            // A little waveform of the sound
            auto wave = r.reduced (14.0f, 0.0f).withTrimmedTop (16.0f).withHeight (r.getHeight() * 0.42f);
            juce::Path path;
            const int n = 40;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / (float) n;
                const float env = std::sin (juce::MathConstants<float>::pi * std::pow (t, 0.7f));
                const float h = env * (0.35f + 0.65f * std::abs (std::sin ((float) i * 1.7f))) * wave.getHeight() * 0.5f;
                path.addRoundedRectangle (wave.getX() + t * wave.getWidth() - 1.0f, wave.getCentreY() - h, 2.0f, h * 2.0f + 0.5f, 1.0f);
            }
            g.setGradientFill (juce::ColourGradient (veilAt (dark, 0.0f).withAlpha (hover ? 0.95f : 0.7f), wave.getX(), 0.0f,
                                                     veilAt (dark, 1.0f).withAlpha (hover ? 0.95f : 0.7f), wave.getRight(), 0.0f, false));
            g.fillPath (path);
            g.setColour (hover ? p.text : p.textDim);
            drawSpacedText (g, "DRAG WAV", r.withTrimmedTop (r.getHeight() * 0.6f), 9.5f, juce::Justification::centred, 0.22f, hover);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging || e.getDistanceFromDragStart() < 6) return;
            dragging = true;
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Huldra");
            folder.createDirectory();
            const auto file = folder.getChildFile (fileName());
            writeWav (file);
            if (file.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                           [this] { dragging = false; });
            else
                dragging = false;
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
            chooser = std::make_unique<juce::FileChooser> ("Save sound as WAV",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (fileName()), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != juce::File()) writeWav (f.withFileExtension ("wav"));
                                  });
        }

    private:
        juce::String fileName() const { return juce::File::createLegalFileName ("Huldra " + proc.presetManager.getCurrentName()) + ".wav"; }

        void writeWav (const juce::File& file)
        {
            const auto values = proc.currentValues();
            const double sr = proc.getCurrentSampleRate() > 1000.0 ? proc.getCurrentSampleRate() : 48000.0;
            const double tail = std::min (8.0, (double) values[params::AmpRelease] + (values.b (params::FxVerbOn) ? values[params::FxVerbDecay] * 0.6 : 0.0) + 0.3);
            juce::AudioBuffer<float> buf (2, (int) ((2.0 + tail) * sr));
            HuldraProcessor::renderOffline (values, proc.instrument.snapshot(), proc.getFxOrder(), sr, 60, 0.85f, 2.0, buf);
            int end = buf.getNumSamples();
            while (end > 1 && std::abs (buf.getSample (0, end - 1)) < 1.0e-5f && std::abs (buf.getSample (1, end - 1)) < 1.0e-5f) --end;
            buf.setSize (2, std::max (1, end), true);
            const int fade = std::min (buf.getNumSamples(), (int) (0.005 * sr));
            buf.applyGainRamp (buf.getNumSamples() - fade, fade, 1.0f, 0.0f);
            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto stream = file.createOutputStream())
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                }
        }

        HuldraProcessor& proc;
        std::unique_ptr<juce::FileChooser> chooser;
        bool hover = false, dragging = false;
    };

    // ---------------------------------------------------------------------------------------------
    class Footer : public juce::Component
    {
    public:
        explicit Footer (HuldraProcessor& p)
            : proc (p), pitch (p, true), mod (p, false), keyboard (p.keyboardState), pad (p), exportTile (p),
              a ("A"), b ("B"), copy (utf8 ("A \xe2\x86\x92 B"))
        {
            addAndMakeVisible (pitch);
            addAndMakeVisible (mod);
            keyboard.setAvailableRange (36, 107);
            keyboard.setLowestVisibleKey (36);
            keyboard.setScrollButtonsVisible (false);
            keyboard.setKeyPressBaseOctave (5);
            addAndMakeVisible (keyboard);
            addAndMakeVisible (pad);
            addAndMakeVisible (exportTile);
            addAndMakeVisible (a);
            addAndMakeVisible (b);
            addAndMakeVisible (copy);
            a.setTooltip ("Sound A (compare two versions)");
            b.setTooltip ("Sound B");
            copy.setTooltip ("Copy the sound you hear to the other slot");
            a.onClick = [this] { switchTo (0); };
            b.onClick = [this] { switchTo (1); };
            copy.onClick = [this]
            {
                proc.storeSnapshot (1 - proc.getActiveSnapshot());
            };
            updateAB();
        }

        void tick() { pad.tick(); }

        void resized() override
        {
            auto r = getLocalBounds();
            pitch.setBounds (r.removeFromLeft (22));
            r.removeFromLeft (6);
            mod.setBounds (r.removeFromLeft (22));
            r.removeFromLeft (10);
            exportTile.setBounds (r.removeFromRight (96));
            r.removeFromRight (10);
            pad.setBounds (r.removeFromRight (150));
            r.removeFromRight (10);
            auto col = r.removeFromRight (82);
            const int h = (col.getHeight() - 8) / 3;
            a.setBounds (col.removeFromTop (h));
            col.removeFromTop (4);
            b.setBounds (col.removeFromTop (h));
            col.removeFromTop (4);
            copy.setBounds (col);
            r.removeFromRight (10);
            keyboard.setBounds (r);
            keyboard.setKeyWidth ((float) keyboard.getWidth() / 42.0f);
        }

        Keyboard& getKeyboard() { return keyboard; }

    private:
        void switchTo (int slot)
        {
            if (slot == proc.getActiveSnapshot()) return;
            proc.storeSnapshot (proc.getActiveSnapshot());
            if (proc.hasSnapshot (slot)) proc.recallSnapshot (slot);
            else                         proc.storeSnapshot (slot);
            proc.setActiveSnapshot (slot);
            updateAB();
        }

        void updateAB()
        {
            a.active = proc.getActiveSnapshot() == 0;
            b.active = proc.getActiveSnapshot() == 1;
            copy.setText (a.active ? utf8 ("A \xe2\x86\x92 B") : utf8 ("B \xe2\x86\x92 A"));
            a.repaint();
            b.repaint();
        }

        HuldraProcessor& proc;
        Wheel pitch, mod;
        Keyboard keyboard;
        MorphPad pad;
        ExportTile exportTile;
        SmallButton a, b, copy;
    };
}
