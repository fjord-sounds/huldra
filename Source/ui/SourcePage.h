#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // ---------------------------------------------------------------------------------------------
    // The loaded sounds as chips, plus load / record / bounce. While no own sound is loaded, the demo voices.
    class SampleStrip : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
    {
    public:
        explicit SampleStrip (HuldraProcessor& p) : proc (p)
        {
            proc.instrument.addChangeListener (this);
            demo = proc.apvts.getParameter (params::paramId (params::DemoVoice));
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        ~SampleStrip() override { proc.instrument.removeChangeListener (this); }

        void tick()
        {
            const auto state = proc.recorder.getState();
            if (state != lastRec || state == Recorder::State::Recording || state == Recorder::State::Waiting) { lastRec = state; repaint(); }
            if (state == Recorder::State::Finished) proc.stopRecording();
            phase += 1.0f / 30.0f;
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            layout();
            if (proc.instrument.snapshot().usingDemo)
            {
                g.setColour (p.textDim);
                drawSpacedText (g, "DEMO", getLocalBounds().removeFromLeft (70).toFloat(), 10.0f, juce::Justification::centredLeft, 0.3f, true);
            }
            for (const auto& c : chips)
            {
                auto r = c.bounds.toFloat().reduced (0.5f);
                const bool on = c.active, hov = c.index == hovered;
                if (on) { g.setColour (p.accentSoft); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
                g.setColour (on ? p.accent : (hov ? p.textDim : p.line));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                auto inner = r.reduced (12.0f, 0.0f);
                if (c.colourDot)
                {
                    g.setColour (veilAt (dark, c.hue));
                    g.fillEllipse (inner.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f));
                    inner.removeFromLeft (7.0f);
                }
                g.setColour (on ? p.text : p.text.withAlpha (0.8f));
                g.setFont (font (12.5f, on));
                g.drawText (c.label, inner, juce::Justification::centredLeft, true);
                if (c.sub.isNotEmpty())
                {
                    g.setColour (p.textDim);
                    g.setFont (font (10.5f));
                    g.drawText (c.sub, inner, juce::Justification::centredRight, false);
                }
            }

            // Buttons on the right
            for (int b = 0; b < 3; ++b)
            {
                auto r = buttonBounds (b).toFloat().reduced (0.5f);
                const bool hov = hoveredButton == b;
                const bool rec = b == 1 && (lastRec == Recorder::State::Recording || lastRec == Recorder::State::Waiting);
                const bool busy = b == 2 && proc.isBouncing();
                if (rec)
                {
                    const float pulse = 0.5f + 0.5f * std::sin (phase * 6.0f);
                    g.setColour (juce::Colour (0xffe0505a).withAlpha (0.18f + 0.15f * pulse));
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                }
                g.setColour (rec ? juce::Colour (0xffe0505a) : (hov ? p.textDim : p.line));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                auto inner = r.reduced (12.0f, 0.0f);
                const auto icon = inner.removeFromLeft (12.0f).withSizeKeepingCentre (11.0f, 11.0f);
                g.setColour (b == 1 ? juce::Colour (0xffe0505a) : (hov ? p.text : p.textDim));
                if (b == 0)
                {
                    juce::Path ar;
                    ar.startNewSubPath (icon.getCentreX(), icon.getY());
                    ar.lineTo (icon.getCentreX(), icon.getBottom() - 2.0f);
                    ar.startNewSubPath (icon.getX() + 1.0f, icon.getY() + 4.0f);
                    ar.lineTo (icon.getCentreX(), icon.getY());
                    ar.lineTo (icon.getRight() - 1.0f, icon.getY() + 4.0f);
                    ar.startNewSubPath (icon.getX(), icon.getBottom());
                    ar.lineTo (icon.getRight(), icon.getBottom());
                    g.strokePath (ar, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else if (b == 1)
                {
                    if (rec) g.fillRoundedRectangle (icon.reduced (1.5f), 1.5f);
                    else     g.fillEllipse (icon.reduced (1.0f));
                }
                else
                {
                    // bounce: a circle arrow
                    juce::Path ar;
                    ar.addCentredArc (icon.getCentreX(), icon.getCentreY(), 5.0f, 5.0f, 0.0f, 0.6f, 5.6f, true);
                    g.strokePath (ar, juce::PathStrokeType (1.4f));
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (icon.getCentre()));
                    if (busy)
                    {
                        g.setColour (p.accent);
                        juce::Path spin;
                        spin.addCentredArc (icon.getCentreX(), icon.getCentreY(), 7.0f, 7.0f, 0.0f, phase * 6.0f, phase * 6.0f + 1.5f, true);
                        g.strokePath (spin, juce::PathStrokeType (1.6f));
                    }
                }
                inner.removeFromLeft (8.0f);
                juce::String label = b == 0 ? "LOAD" : (b == 1 ? (lastRec == Recorder::State::Waiting ? "WAITING" : (rec ? recordTime() : "RECORD")) : "BOUNCE");
                g.setColour (hov || rec ? p.text : p.text.withAlpha (0.85f));
                drawSpacedText (g, label, inner, 10.0f, juce::Justification::centredLeft, 0.16f, true);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int h = -1, hb = -1;
            for (const auto& c : chips) if (c.bounds.contains (e.getPosition())) h = c.index;
            for (int b = 0; b < 3; ++b) if (buttonBounds (b).contains (e.getPosition())) hb = b;
            if (h != hovered || hb != hoveredButton)
            {
                hovered = h;
                hoveredButton = hb;
                static const char* tips[] = { "Load sounds (several files are spread over the keys)",
                                              "Record from the track input or the microphone. Right-click: start on signal",
                                              "Bounce: render the sound as you hear it into a new sample (resample in place)" };
                setTooltip (hb >= 0 ? tips[hb] : (h >= 1000 ? "Demo voice (used until you load or record your own sound)"
                                                             : (h >= 0 ? "Click: show this sample.  Right-click: options" : "")));
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = hoveredButton = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int b = 0; b < 3; ++b)
                if (buttonBounds (b).contains (e.getPosition()))
                    return buttonClicked (b, e.mods.isPopupMenu());
            for (const auto& c : chips)
            {
                if (! c.bounds.contains (e.getPosition())) continue;
                if (c.index >= 1000)
                {
                    demo->beginChangeGesture();
                    demo->setValueNotifyingHost (demo->convertTo0to1 ((float) (c.index - 1000)));
                    demo->endChangeGesture();
                }
                else if (e.mods.isPopupMenu()) sampleMenu (c.uid);
                else proc.instrument.setActiveSample (c.uid);
                repaint();
                return;
            }
        }

    private:
        struct Chip { juce::Rectangle<int> bounds; juce::String label, sub, uid; int index; bool active; bool colourDot; float hue; };

        juce::Rectangle<int> buttonBounds (int b) const
        {
            const int widths[] = { 92, 112, 104 };
            int x = getWidth();
            for (int i = 2; i >= b; --i) x -= widths[i] + (i < 2 ? 8 : 0);
            return { x, 0, widths[b], getHeight() };
        }

        void layout()
        {
            chips.clear();
            const auto& snap = proc.instrument.snapshot();
            const int right = buttonBounds (0).getX() - 16;
            int x = 0;
            if (snap.usingDemo)
            {
                const int current = juce::roundToInt (demo->convertFrom0to1 (demo->getValue()));
                const int w = std::min (96, (right - 80) / DemoVoices::kCount);
                x = 80;
                for (int i = 0; i < DemoVoices::kCount; ++i)
                {
                    chips.push_back ({ { x, 0, w - 6, getHeight() }, DemoVoices::names[i], {}, {}, 1000 + i, i == current, false, 0.0f });
                    x += w;
                }
                return;
            }
            const auto samples = proc.instrument.getSamples();
            const auto active = proc.instrument.getActiveUid();
            const int n = (int) samples.size();
            const int w = n == 0 ? 0 : std::clamp ((right) / n, 110, 210);
            for (int i = 0; i < n; ++i)
            {
                if (x + w > right + 4) break;
                const auto& s = samples[(size_t) i];
                const auto sub = s->pitchConfidence > 0.3f ? noteName (juce::roundToInt (s->rootNote)) : juce::String (s->seconds(), 1) + " s";
                chips.push_back ({ { x, 0, w - 6, getHeight() }, s->name, sub, s->uid, i, s->uid == active || (active.isEmpty() && i == 0),
                                   true, n > 1 ? (float) i / (float) (n - 1) : 0.0f });
                x += w;
            }
        }

        juce::String recordTime() const
        {
            const double t = proc.recorder.recordedSeconds();
            return "STOP  " + juce::String ((int) t / 60) + ":" + juce::String (std::fmod (t, 60.0), 1).paddedLeft ('0', 4);
        }

        void buttonClicked (int b, bool popup)
        {
            if (b == 0)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load sounds", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                               "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          const auto files = fc.getResults();
                                          if (! files.isEmpty())
                                              proc.instrument.loadFilesAsync (files, files.size() > 1 ? InstrumentState::AddMode::AddAutoMap
                                                                                                      : InstrumentState::AddMode::Replace);
                                      });
            }
            else if (b == 1)
            {
                const auto st = proc.recorder.getState();
                if (st == Recorder::State::Recording) proc.stopRecording();
                else if (st == Recorder::State::Waiting) proc.recorder.cancel();
                else proc.startRecording (popup || waitForSignal);
                if (popup)
                {
                    juce::PopupMenu m;
                    m.addItem (1, "Start recording when a signal arrives", true, waitForSignal);
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r == 1) waitForSignal = ! waitForSignal; });
                }
            }
            else
            {
                juce::PopupMenu m;
                m.addSectionHeader ("Render the sound into a new sample");
                m.addItem (1, "C3, 2 seconds");
                m.addItem (2, "C3, 4 seconds");
                m.addItem (3, "C3, 8 seconds (motion and long tails)");
                m.addItem (4, "C2, 4 seconds");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
                {
                    const int notes[] = { 0, 60, 60, 60, 48 };
                    const double secs[] = { 0, 2, 4, 8, 4 };
                    if (r > 0) proc.bounce (notes[r], secs[r], [safe = juce::Component::SafePointer<SampleStrip> (this)] (bool) { if (safe) safe->repaint(); });
                    repaint();
                });
            }
            repaint();
        }

        void sampleMenu (const juce::String& uid)
        {
            auto s = proc.instrument.getSample (uid);
            if (s == nullptr) return;
            juce::PopupMenu m;
            m.addSectionHeader (s->name + "  " + juce::String (s->seconds(), 2) + " s, " + juce::String (s->sampleRate / 1000.0, 1) + " kHz");
            m.addItem (1, "Show this sample");
            m.addItem (2, "Spread all samples over the keys");
            if (s->file.existsAsFile()) m.addItem (3, "Show file");
            m.addSeparator();
            m.addItem (4, "Remove");
            m.addItem (5, "Remove all (back to the demo voices)");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, uid, file = s->file] (int r)
            {
                if (r == 1) proc.instrument.setActiveSample (uid);
                if (r == 2) { proc.undoManager.beginNewTransaction ("Auto map"); proc.instrument.autoMap(); }
                if (r == 3) file.revealToUser();
                if (r == 4) proc.instrument.removeSample (uid);
                if (r == 5) for (auto& smp : proc.instrument.getSamples()) proc.instrument.removeSample (smp->uid);
            });
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        HuldraProcessor& proc;
        juce::RangedAudioParameter* demo;
        std::vector<Chip> chips;
        int hovered = -1, hoveredButton = -1;
        Recorder::State lastRec = Recorder::State::Idle;
        bool waitForSignal = false;
        float phase = 0.0f;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    // ---------------------------------------------------------------------------------------------
    // The big waveform: veil ribbons behind, start / end / loop handles, crossfades, playheads, the melody line.
    class WaveformView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit WaveformView (HuldraProcessor& p)
            : proc (p), start (p.apvts, params::SmpStart), end (p.apvts, params::SmpEnd),
              loopStart (p.apvts, params::SmpLoopStart), loopLen (p.apvts, params::SmpLoopLen)
        {
            setTooltip ("Drag the markers: start, end and the loop. Double-click a marker to reset it.");
        }

        void tick()
        {
            time += 1.0f / 30.0f;
            std::array<float, 1024> buf {};
            proc.scope.copyLatest (buf.data(), nullptr, (int) buf.size());
            float sum = 0.0f;
            for (float v : buf) sum += v * v;
            const float rms = std::sqrt (sum / (float) buf.size());
            level += (std::min (1.0f, rms * 4.0f) - level) * 0.15f;
            if (proc.recorder.getState() == Recorder::State::Recording || proc.recorder.getState() == Recorder::State::Waiting)
                proc.recorder.copyRing (input.data(), (int) input.size());
            // playhead trails
            for (auto& t : trails) t.age += 1.0f / 30.0f;
            trails.erase (std::remove_if (trails.begin(), trails.end(), [] (const Trail& t) { return t.age > 0.5f; }), trails.end());
            if (const auto* s = displaySample (proc))
                forEachPlayhead (proc, s, [this] (float pos, float lvl, int) { trails.push_back ({ pos, lvl, 0.0f }); });
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 10.0f);
            g.reduceClipRegion (clip);

            drawVeil (g, r, dark);

            const auto rec = proc.recorder.getState();
            if (rec == Recorder::State::Recording || rec == Recorder::State::Waiting)
            {
                drawLiveInput (g, wave (), dark, rec == Recorder::State::Waiting);
                g.setColour (p.line);
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
                return;
            }

            const auto* s = displaySample (proc);
            if (s == nullptr)
            {
                g.setColour (p.textDim);
                g.setFont (font (14.0f));
                g.drawText (proc.instrument.numPendingLoads() > 0 ? "Loading ..." : "Drop a sound here", r, juce::Justification::centred);
                return;
            }

            const auto w = wave();
            const float st = start.plain(), en = std::max (st + 0.001f, end.plain());
            const float ls = loopStart.plain(), ll = loopLen.plain();
            const bool loops = proc.apvts.getRawParameterValue ("smp_loop")->load() > 0.5f;
            const bool chops = proc.apvts.getRawParameterValue ("play_mode")->load() > 0.5f;

            // Played region bright, the rest dimmed
            drawWaveform (g, *s, w, 0.0f, 1.0f, 0.22f);
            {
                juce::Graphics::ScopedSaveState s2 (g);
                g.reduceClipRegion (juce::Rectangle<float> (xOf (st), w.getY() - 4.0f, xOf (en) - xOf (st), w.getHeight() + 8.0f).toNearestInt());
                drawWaveform (g, *s, w, 0.0f, 1.0f, 1.0f);
            }

            // Pitch line (the detected melody)
            drawPitchCurve (g, *s, w, dark);

            // Loop region with crossfade curves
            if (loops && ! chops)
            {
                const float a = st + (en - st) * ls;
                const float b = std::min (en, a + (en - st) * ll);
                const float xa = xOf (a), xb = xOf (b);
                g.setColour (p.accent.withAlpha (dark ? 0.08f : 0.06f));
                g.fillRect (xa, w.getY(), xb - xa, w.getHeight());
                g.setColour (p.accent.withAlpha (0.55f));
                g.fillRect (xa, w.getY(), 1.0f, w.getHeight());
                g.fillRect (xb - 1.0f, w.getY(), 1.0f, w.getHeight());
                const float xf = (b - a) * 0.5f * proc.apvts.getRawParameterValue ("smp_xfade")->load();
                if (xf > 0.0005f)
                {
                    const float x0 = xOf (b - xf), x1 = xb;
                    juce::Path out, in;
                    out.startNewSubPath (x0, w.getY() + 6.0f);
                    in.startNewSubPath (x0, w.getBottom() - 6.0f);
                    for (int i = 1; i <= 20; ++i)
                    {
                        const float t = (float) i / 20.0f;
                        const float x = x0 + (x1 - x0) * t;
                        out.lineTo (x, w.getY() + 6.0f + (1.0f - std::sqrt (1.0f - t)) * (w.getHeight() - 12.0f));
                        in.lineTo (x, w.getBottom() - 6.0f - std::sqrt (t) * (w.getHeight() - 12.0f));
                    }
                    g.setColour (p.accent.withAlpha (0.7f));
                    g.strokePath (out, juce::PathStrokeType (1.2f));
                    g.setColour (veilAt (dark, 0.6f).withAlpha (0.7f));
                    g.strokePath (in, juce::PathStrokeType (1.2f));
                }
                drawHandle (g, xa, w, "LOOP", true);
                drawHandle (g, xb, w, {}, true);
            }

            drawHandle (g, xOf (st), w, "START", false);
            drawHandle (g, xOf (en), w, "END", false);

            // Playheads with a short trail
            for (const auto& t : trails)
            {
                const float x = xOf (t.pos);
                const float a = (1.0f - t.age / 0.5f) * (0.25f + 0.75f * std::min (1.0f, t.level));
                g.setColour (p.text.withAlpha (a * 0.12f));
                g.fillRect (x - 2.0f, w.getY(), 4.0f, w.getHeight());
            }
            forEachPlayhead (proc, s, [&] (float pos, float lvl, int)
            {
                const float x = xOf (pos);
                g.setGradientFill (juce::ColourGradient (p.text.withAlpha (0.0f), x, w.getY(), p.text.withAlpha (0.9f * std::min (1.0f, lvl + 0.2f)),
                                                         x, w.getCentreY(), false));
                g.fillRect (x - 0.75f, w.getY(), 1.5f, w.getHeight());
                g.setColour (p.text);
                g.fillEllipse (x - 3.0f, w.getCentreY() - 3.0f, 6.0f, 6.0f);
            });

            // Info line
            g.setColour (p.textDim);
            auto info = r.reduced (16.0f, 8.0f).removeFromBottom (16.0f);
            juce::String root = s->pitchConfidence > 0.3f
                ? noteName (juce::roundToInt (s->rootNote)) + "  " + juce::String (juce::roundToInt ((s->rootNote - std::round (s->rootNote)) * 100.0f)) + " ct"
                : juce::String ("unpitched");
            drawSpacedText (g, s->name.toUpperCase() + "   " + utf8 ("\xc2\xb7") + "   " + juce::String (s->seconds(), 2) + " S   "
                               + utf8 ("\xc2\xb7") + "   " + juce::String (s->sampleRate / 1000.0, 1) + " KHZ   " + utf8 ("\xc2\xb7")
                               + "   " + root.toUpperCase() + "   " + utf8 ("\xc2\xb7") + "   " + juce::String (juce::roundToInt (s->pitchConfidence * 100.0f)) + " % VOICED",
                            info, 9.5f, juce::Justification::centredLeft, 0.15f);

            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragTarget = handleAt (e.position.x);
            if (dragTarget == nullptr) return;
            dragTarget->begin();
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragTarget == nullptr) return;
            const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - inset) / ((float) getWidth() - 2.0f * inset));
            const float st = start.plain(), en = end.plain();
            if (dragTarget == &start) start.setPlain (std::min (t, en - 0.002f));
            else if (dragTarget == &end) end.setPlain (std::max (t, st + 0.002f));
            else if (dragTarget == &loopStart) loopStart.setPlain ((t - st) / std::max (0.001f, en - st));
            else if (dragTarget == &loopLen)
            {
                const float a = st + (en - st) * loopStart.plain();
                loopLen.setPlain ((t - a) / std::max (0.001f, en - st));
            }
        }

        void mouseUp (const juce::MouseEvent&) override { if (dragTarget != nullptr) dragTarget->end(); dragTarget = nullptr; }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (auto* h = handleAt (e.position.x)) h->reset();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (handleAt (e.position.x) != nullptr ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        }

    private:
        struct Trail { float pos, level, age; };

        juce::Rectangle<float> wave() const { return getLocalBounds().toFloat().reduced (inset, 30.0f).withTrimmedBottom (6.0f); }
        float xOf (float t) const { return inset + t * ((float) getWidth() - 2.0f * inset); }
        static constexpr float inset = 16.0f;

        ParamDrag* handleAt (float x)
        {
            const float st = start.plain(), en = end.plain();
            const bool loops = proc.apvts.getRawParameterValue ("smp_loop")->load() > 0.5f;
            const float a = st + (en - st) * loopStart.plain();
            const float b = std::min (en, a + (en - st) * loopLen.plain());
            std::vector<std::pair<ParamDrag*, float>> handles { { &start, xOf (st) }, { &end, xOf (en) } };
            if (loops) { handles.push_back ({ &loopStart, xOf (a) }); handles.push_back ({ &loopLen, xOf (b) }); }
            ParamDrag* best = nullptr;
            float bestD = 7.0f;
            for (auto& [h, hx] : handles)
                if (std::abs (hx - x) < bestD) { bestD = std::abs (hx - x); best = h; }
            return best;
        }

        void drawHandle (juce::Graphics& g, float x, juce::Rectangle<float> w, const juce::String& label, bool loop)
        {
            const auto& p = Theme::get().palette();
            g.setColour (loop ? p.accent : p.text.withAlpha (0.85f));
            g.fillRect (x - 0.5f, w.getY() - 14.0f, 1.0f, w.getHeight() + 14.0f);
            juce::Path tab;
            tab.addTriangle (x - 5.0f, w.getY() - 18.0f, x + 5.0f, w.getY() - 18.0f, x, w.getY() - 11.0f);
            g.fillPath (tab);
            if (label.isNotEmpty())
            {
                g.setColour (loop ? p.accent : p.textDim);
                drawSpacedText (g, label, { x + 6.0f, w.getY() - 26.0f, 60.0f, 14.0f }, 8.5f, juce::Justification::centredLeft, 0.2f);
            }
        }

        void drawVeil (juce::Graphics& g, juce::Rectangle<float> r, bool dark)
        {
            if (Theme::get().reduceMotion()) return;
            const auto cols = veilColours (dark);
            for (int k = 0; k < 5; ++k)
            {
                juce::Path ribbon;
                const float y0 = r.getY() + r.getHeight() * (0.25f + 0.12f * (float) k);
                const float amp = r.getHeight() * (0.05f + 0.1f * level) * (1.0f + 0.3f * (float) (k % 2));
                const int n = 60;
                for (int i = 0; i <= n; ++i)
                {
                    const float x = (float) i / (float) n;
                    const float y = y0 + amp * std::sin (x * (3.0f + (float) k * 0.7f) + time * (0.35f + 0.11f * (float) k) + (float) k * 1.3f)
                                       + amp * 0.4f * std::sin (x * 9.0f - time * 0.8f + (float) k);
                    if (i == 0) ribbon.startNewSubPath (r.getX(), y);
                    else ribbon.lineTo (r.getX() + x * r.getWidth(), y);
                }
                const auto c = cols[(size_t) (k % 3)];
                g.setColour (c.withAlpha ((dark ? 0.10f : 0.08f) + 0.12f * level));
                g.strokePath (ribbon, juce::PathStrokeType (18.0f + 22.0f * level, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (c.withAlpha ((dark ? 0.18f : 0.14f) + 0.2f * level));
                g.strokePath (ribbon, juce::PathStrokeType (1.0f));
            }
        }

        void drawPitchCurve (juce::Graphics& g, const SampleData& s, juce::Rectangle<float> w, bool dark)
        {
            if (s.pitchConfidence < 0.3f || s.pitchCurve.empty()) return;
            const float centre = s.rootNote;
            juce::Path path;
            bool drawing = false;
            const int n = (int) s.pitchCurve.size();
            const int step = std::max (1, n / 600);
            for (int i = 0; i < n; i += step)
            {
                const float note = s.pitchCurve[(size_t) i];
                if (note <= 0.0f) { drawing = false; continue; }
                const float x = w.getX() + (float) i / (float) n * w.getWidth();
                const float y = w.getCentreY() - (note - centre) / 12.0f * w.getHeight() * 0.42f;
                if (! drawing) path.startNewSubPath (x, y); else path.lineTo (x, y);
                drawing = true;
            }
            g.setColour (veilAt (dark, 0.85f).withAlpha (0.75f));
            g.strokePath (path, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawLiveInput (juce::Graphics& g, juce::Rectangle<float> w, bool dark, bool waiting)
        {
            const auto& p = Theme::get().palette();
            const int n = (int) input.size();
            const int cols = (int) (w.getWidth() / 3.0f);
            for (int c = 0; c < cols; ++c)
            {
                float peak = 0.0f;
                for (int i = c * n / cols; i < (c + 1) * n / cols; ++i) peak = std::max (peak, std::abs (input[(size_t) i]));
                const float h = std::min (1.0f, peak * 1.4f) * w.getHeight() * 0.5f;
                g.setColour (veilAt (dark, (float) c / (float) cols).withAlpha (0.9f));
                g.fillRoundedRectangle (w.getX() + (float) c * 3.0f, w.getCentreY() - h - 0.5f, 2.0f, h * 2.0f + 1.0f, 1.0f);
            }
            g.setColour (waiting ? p.textDim : juce::Colour (0xffe0505a));
            drawSpacedText (g, waiting ? "WAITING FOR A SIGNAL ..." : "RECORDING", w.withHeight (24.0f).translated (0.0f, -24.0f), 10.5f,
                            juce::Justification::centred, 0.3f, true);
        }

        HuldraProcessor& proc;
        ParamDrag start, end, loopStart, loopLen;
        ParamDrag* dragTarget = nullptr;
        std::vector<Trail> trails;
        std::array<float, 4096> input {};
        float time = 0.0f, level = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    class SourcePage : public Page
    {
    public:
        explicit SourcePage (HuldraProcessor& p)
            : Page (p), strip (p), waveform (p),
              ampGraph (p.apvts, P::AmpAttack, P::AmpDecay, P::AmpSustain, P::AmpRelease,
                        [&p] { return p.voiceDisplay.sources[(size_t) P::ModSource::AmpEnv].load(); }),
              playbackLock (p.apvts, locks::ids().classic), voiceLock (p.apvts, locks::ids().voice), ampLock (p.apvts, locks::ids().amp)
        {
            addAndMakeVisible (playbackLock);
            addAndMakeVisible (voiceLock);
            addAndMakeVisible (ampLock);
            addAndMakeVisible (strip);
            addAndMakeVisible (waveform);
            auto& s = p.apvts;
            playback.add (makeKnob (s, P::SmpStart, "Start"));
            playback.add (makeKnob (s, P::SmpEnd, "End"));
            playback.add (makeKnob (s, P::SmpStartRand, "Random"));
            playback.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::SmpReverse), "Rev"), "Direction"), 1.1f);
            playback.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::SmpLoopMode), nullptr, juce::StringArray { "Off", "Fwd", "Ping" }), "Loop"), 2.2f);
            playback.add (makeKnob (s, P::SmpLoopStart, "Loop Start"));
            playback.add (makeKnob (s, P::SmpLoopLen, "Length"));
            playback.add (makeKnob (s, P::SmpXfade, "X-Fade"));
            playback.addTo (*this);

            voice.add (makeKnob (s, P::Tune, "Tune"));
            voice.add (makeKnob (s, P::Fine, "Fine"));
            voice.add (makeKnob (s, P::KeyTrack, "Keytrack"));
            voice.add (makeKnob (s, P::Glide, "Glide"));
            voice.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::VoiceMode)), "Voices"), 2.0f);
            voice.add (makeKnob (s, P::Polyphony, "Poly"));
            voice.addTo (*this);

            amp.add (makeKnob (s, P::AmpAttack, "Attack"));
            amp.add (makeKnob (s, P::AmpDecay, "Decay"));
            amp.add (makeKnob (s, P::AmpSustain, "Sustain"));
            amp.add (makeKnob (s, P::AmpRelease, "Release"));
            amp.add (makeKnob (s, P::Level, "Level"));
            amp.add (makeKnob (s, P::Pan, "Pan"));
            amp.add (makeKnob (s, P::VelAmp, "Velocity"));
            amp.addTo (*this);
            addAndMakeVisible (ampGraph);
        }

        void tick() override
        {
            strip.tick();
            waveform.tick();
            ampGraph.repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, playbackArea.toFloat(), "Playback");
            drawPanel (g, voiceArea.toFloat(), "Voice");
            drawPanel (g, ampArea.toFloat(), "Amp");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            strip.setBounds (r.removeFromTop (32));
            r.removeFromTop (10);
            auto bottom = r.removeFromBottom (232);
            r.removeFromBottom (10);
            waveform.setBounds (r);

            auto top = bottom.removeFromTop (112);
            bottom.removeFromTop (8);
            playbackArea = top.removeFromLeft (top.getWidth() * 58 / 100);
            top.removeFromLeft (8);
            voiceArea = top;
            ampArea = bottom;
            playbackLock.place (playbackArea);
            voiceLock.place (voiceArea);
            ampLock.place (ampArea);
            playback.layout (playbackArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
            voice.layout (voiceArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
            auto a = ampArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6);
            ampGraph.setBounds (a.removeFromLeft (300).reduced (6, 2));
            a.removeFromLeft (10);
            amp.layout (a);
        }

    private:
        SampleStrip strip;
        WaveformView waveform;
        ControlRow playback, voice, amp;
        AdsrGraph ampGraph;
        PanelLock playbackLock, voiceLock, ampLock;   // keep this part when switching presets
        juce::Rectangle<int> playbackArea, voiceArea, ampArea;
    };
}
