#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // ---------------------------------------------------------------------------------------------
    // Step editor: one column per step, lanes for note, velocity, gate, chance, ratchet and the mod lane.
    class StepEditor : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
    {
    public:
        explicit StepEditor (HuldraProcessor& p) : proc (p)
        {
            proc.instrument.addChangeListener (this);
            setTooltip ("Click the top row to switch steps on / off. Drag in a lane to draw. Right-click a step for options.");
        }

        ~StepEditor() override { proc.instrument.removeChangeListener (this); }

        void tick()
        {
            const int step = proc.getCurrentStep();
            const int len = steps();
            const int now = step >= 0 ? step % std::max (1, len) : -1;
            if (now != lastStep) { lastStep = now; flash = 1.0f; }
            flash = std::max (0.0f, flash - 0.12f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto pattern = proc.instrument.getPattern();
            const int len = steps();
            const bool running = proc.isHostPlaying() || proc.voiceDisplay.activeVoices.load() > 0;

            for (int l = 0; l < kLanes; ++l)
            {
                const auto lane = laneRect (l);
                g.setColour (p.textDim);
                drawSpacedText (g, laneName (l), lane.withWidth (labelW - 8.0f), 9.0f, juce::Justification::centredLeft, 0.18f);
            }

            for (int i = 0; i < motion::kMaxSteps; ++i)
            {
                const auto& s = pattern.steps[(size_t) i];
                const bool inside = i < len;
                const auto col = veilAt (dark, (float) i / 31.0f);
                const float alpha = inside ? 1.0f : 0.25f;
                const bool playing = running && i == lastStep;
                // column background
                auto colRect = column (i);
                if (i % 4 == 0)
                {
                    g.setColour (p.line.withAlpha (0.25f * alpha));
                    g.fillRect (colRect.withWidth (1.0f));
                }
                if (playing)
                {
                    g.setColour (p.accent.withAlpha (0.12f + 0.2f * flash));
                    g.fillRoundedRectangle (colRect.reduced (1.0f, 0.0f), 4.0f);
                }

                // on/off
                auto onR = cell (0, i).reduced (3.0f, 6.0f);
                g.setColour (s.on ? col.withAlpha (alpha) : p.line.withAlpha (alpha));
                if (s.on) g.fillRoundedRectangle (onR, 3.0f);
                else      g.drawRoundedRectangle (onR, 3.0f, 1.0f);

                // note: a dot at the transposition (-12..+12 visible, clamped)
                auto nr = cell (1, i);
                const float ny = nr.getCentreY() - std::clamp ((float) s.note / 12.0f, -1.0f, 1.0f) * nr.getHeight() * 0.44f;
                g.setColour (p.line.withAlpha (0.4f * alpha));
                g.fillRect (nr.getX() + 2.0f, nr.getCentreY(), nr.getWidth() - 4.0f, 1.0f);
                if (s.on)
                {
                    const float r = playing ? 7.0f + 4.0f * flash : 6.0f;
                    if (playing)
                    {
                        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.6f), nr.getCentreX(), ny, col.withAlpha (0.0f), nr.getCentreX() + 16.0f, ny, true));
                        g.fillEllipse (juce::Rectangle<float> (32.0f, 32.0f).withCentre ({ nr.getCentreX(), ny }));
                    }
                    g.setColour (col.withAlpha (alpha));
                    g.fillEllipse (juce::Rectangle<float> (r, r).withCentre ({ nr.getCentreX(), ny }));
                    if (s.note != 0 && nr.getWidth() > 20.0f)
                    {
                        g.setColour (p.textDim.withAlpha (alpha));
                        g.setFont (font (8.5f));
                        g.drawText ((s.note > 0 ? "+" : "") + juce::String (s.note), juce::Rectangle<float> (nr.getX(), ny + (s.note > 0 ? 5.0f : -16.0f), nr.getWidth(), 11.0f),
                                    juce::Justification::centred);
                    }
                    if (s.tie)
                    {
                        g.setColour (col.withAlpha (0.6f * alpha));
                        g.fillRect (nr.getCentreX(), ny - 1.0f, nr.getWidth(), 2.0f);
                    }
                }
                // bar lanes
                drawBar (g, cell (2, i), s.velocity, col, alpha * (s.on ? 1.0f : 0.4f));
                drawBar (g, cell (3, i), s.gate, col, alpha * (s.on ? 1.0f : 0.4f));
                drawBar (g, cell (4, i), s.chance, col, alpha * (s.on ? 1.0f : 0.4f));
                // ratchet: up to four small blocks
                auto rr = cell (5, i).reduced (3.0f, 4.0f);
                for (int k = 0; k < 4; ++k)
                {
                    const auto b = rr.withHeight (rr.getHeight() / 4.0f - 2.0f).translated (0.0f, (float) (3 - k) * rr.getHeight() / 4.0f);
                    g.setColour (k < s.ratchet ? col.withAlpha (alpha * (s.on ? 0.9f : 0.3f)) : p.line.withAlpha (0.4f * alpha));
                    g.fillRoundedRectangle (b, 1.5f);
                }
                drawBar (g, cell (6, i), s.lane, veilAt (dark, 1.0f - (float) i / 31.0f), alpha);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = stepAt (e.position.x), l = laneAt (e.position.y);
            if (i < 0 || l < 0) return;
            proc.undoManager.beginNewTransaction ("Edit pattern");
            auto pattern = proc.instrument.getPattern();
            if (e.mods.isPopupMenu()) return stepMenu (i);
            if (l == 0)
            {
                paintOn = ! pattern.steps[(size_t) i].on;
                pattern.steps[(size_t) i].on = paintOn;
                proc.instrument.setPattern (pattern, true);
            }
            dragLane = l;
            lastStepDrawn = -1;
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragLane < 0) return;
            const int i = stepAt (e.position.x);
            if (i < 0) return;
            auto pattern = proc.instrument.getPattern();
            auto& s = pattern.steps[(size_t) i];
            const auto c = cell (dragLane, i);
            const float v = juce::jlimit (0.0f, 1.0f, (c.getBottom() - e.position.y) / c.getHeight());
            switch (dragLane)
            {
                case 0: s.on = paintOn; break;
                case 1: s.note = juce::jlimit (-12, 12, juce::roundToInt ((c.getCentreY() - e.position.y) / (c.getHeight() * 0.44f) * 12.0f)); s.on = true; break;
                case 2: s.velocity = std::max (0.05f, v); break;
                case 3: s.gate = std::max (0.05f, v); break;
                case 4: s.chance = v; break;
                case 5: s.ratchet = juce::jlimit (1, 4, 1 + (int) (v * 4.0f)); break;
                default: s.lane = v; break;
            }
            proc.instrument.setPattern (pattern, true);
        }

        void mouseUp (const juce::MouseEvent&) override { dragLane = -1; }

    private:
        static constexpr int kLanes = 7;
        static constexpr float labelW = 74.0f;
        static const char* laneName (int l)
        {
            static const char* names[] = { "STEP", "NOTE", "VELOCITY", "GATE", "CHANCE", "RATCHET", "MOD LANE" };
            return names[l];
        }

        int steps() const { return juce::roundToInt (proc.apvts.getRawParameterValue ("pat_length")->load()); }

        juce::Rectangle<float> laneRect (int l) const
        {
            static const float weights[] = { 0.7f, 2.0f, 1.0f, 1.0f, 1.0f, 0.9f, 1.0f };
            float total = 0.0f;
            for (float w : weights) total += w;
            const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
            float y = r.getY();
            for (int i = 0; i < l; ++i) y += r.getHeight() * weights[i] / total;
            return { r.getX(), y, r.getWidth(), r.getHeight() * weights[l] / total - 4.0f };
        }

        juce::Rectangle<float> column (int i) const
        {
            const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f).withTrimmedLeft (labelW);
            const float w = r.getWidth() / (float) motion::kMaxSteps;
            return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
        }

        juce::Rectangle<float> cell (int lane, int i) const
        {
            const auto c = column (i), l = laneRect (lane);
            return { c.getX() + 1.0f, l.getY(), c.getWidth() - 2.0f, l.getHeight() };
        }

        int stepAt (float x) const
        {
            for (int i = 0; i < motion::kMaxSteps; ++i)
                if (column (i).getX() <= x && x < column (i).getRight()) return i;
            return -1;
        }

        int laneAt (float y) const
        {
            for (int l = 0; l < kLanes; ++l)
                if (laneRect (l).expanded (0.0f, 2.0f).contains (laneRect (l).getX() + 1.0f, y)) return l;
            return -1;
        }

        static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float v, juce::Colour col, float alpha)
        {
            const auto& p = Theme::get().palette();
            r = r.reduced (3.0f, 2.0f);
            g.setColour (p.line.withAlpha (0.3f * alpha));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (col.withAlpha (0.85f * alpha));
            g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * (1.0f - v)), 2.0f);
        }

        void stepMenu (int i)
        {
            juce::PopupMenu m;
            const auto pattern = proc.instrument.getPattern();
            m.addItem (1, "Tie into the next step", true, pattern.steps[(size_t) i].tie);
            m.addSeparator();
            m.addItem (2, "Shift pattern left");
            m.addItem (3, "Shift pattern right");
            m.addItem (4, "Random variation");
            m.addItem (5, "Clear");
            m.addItem (6, "Default pattern");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r)
            {
                auto p = proc.instrument.getPattern();
                const int len = std::max (1, steps());
                juce::Random rnd;
                if (r == 1) p.steps[(size_t) i].tie = ! p.steps[(size_t) i].tie;
                if (r == 2) std::rotate (p.steps.begin(), p.steps.begin() + 1, p.steps.begin() + len);
                if (r == 3) std::rotate (p.steps.begin(), p.steps.begin() + len - 1, p.steps.begin() + len);
                if (r == 4)
                    for (int k = 0; k < len; ++k)
                    {
                        auto& s = p.steps[(size_t) k];
                        if (rnd.nextFloat() < 0.3f) s.on = ! s.on;
                        if (rnd.nextFloat() < 0.3f) { const int intervals[] = { 0, 0, 3, 5, 7, 7, 10, 12, -5, 12 }; s.note = intervals[rnd.nextInt (10)]; }
                        s.velocity = juce::jlimit (0.2f, 1.0f, s.velocity + (rnd.nextFloat() - 0.5f) * 0.4f);
                        s.lane = rnd.nextFloat();
                    }
                if (r == 5) for (auto& s : p.steps) s = {};
                if (r == 6) p = motion::Pattern::defaultPattern();
                if (r > 0) proc.instrument.setPattern (p, true);
            });
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        HuldraProcessor& proc;
        int lastStep = -1, dragLane = -1, lastStepDrawn = -1;
        bool paintOn = true;
        float flash = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Constellation: played notes drift from right to left as stars, joined by thin lines.
    class Constellation : public juce::Component
    {
    public:
        explicit Constellation (HuldraProcessor& p) : proc (p) {}

        void tick()
        {
            time += 1.0f / 30.0f;
            const auto& m = proc.getMotion();
            const int w = m.historyWrite.load (std::memory_order_acquire);
            while (readPos != w)
            {
                const auto& h = m.history[(size_t) readPos];
                stars.push_back ({ h.note, h.velocity, time, rng.nextFloat() });
                readPos = (readPos + 1) % motion::MotionEngine::kHistory;
            }
            stars.erase (std::remove_if (stars.begin(), stars.end(), [this] (const Star& s) { return time - s.born > span; }), stars.end());
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (8.0f);
            // faint background stars
            for (int i = 0; i < 60; ++i)
            {
                const float x = r.getX() + std::fmod ((float) i * 97.31f, r.getWidth());
                const float y = r.getY() + std::fmod ((float) i * 53.17f, r.getHeight());
                const float tw = 0.5f + 0.5f * std::sin (time * (0.7f + (float) (i % 5) * 0.3f) + (float) i);
                g.setColour (p.textDim.withAlpha (0.08f + 0.12f * tw));
                g.fillEllipse (x, y, 1.6f, 1.6f);
            }
            int lo = 127, hi = 0;
            for (const auto& s : stars) { lo = std::min (lo, s.note); hi = std::max (hi, s.note); }
            if (stars.empty()) { lo = 48; hi = 72; }
            lo -= 3; hi += 3;
            auto toScreen = [&] (const Star& s)
            {
                const float age = (time - s.born) / span;
                return juce::Point<float> (r.getRight() - age * r.getWidth(), r.getBottom() - (float) (s.note - lo) / (float) std::max (1, hi - lo) * r.getHeight());
            };
            // lines between consecutive notes
            for (size_t i = 1; i < stars.size(); ++i)
            {
                const auto a = toScreen (stars[i - 1]), b = toScreen (stars[i]);
                const float fade = 1.0f - (time - stars[i].born) / span;
                g.setColour (veilAt (dark, 0.5f).withAlpha (0.25f * fade));
                g.drawLine ({ a, b }, 1.0f);
            }
            for (const auto& s : stars)
            {
                const auto pt = toScreen (s);
                const float age = (time - s.born) / span;
                const float fresh = std::max (0.0f, 1.0f - (time - s.born) / 0.4f);
                const auto col = veilAt (dark, (float) ((s.note % 12)) / 12.0f);
                const float size = 4.0f + 6.0f * s.velocity + 10.0f * fresh;
                const float tw = 0.75f + 0.25f * std::sin (time * 3.0f + s.seed * 20.0f);
                g.setGradientFill (juce::ColourGradient (col.withAlpha (0.5f * (1.0f - age) * tw), pt, col.withAlpha (0.0f), pt.translated (size * 1.8f, 0.0f), true));
                g.fillEllipse (juce::Rectangle<float> (size * 3.6f, size * 3.6f).withCentre (pt));
                g.setColour (col.withAlpha (1.0f - age));
                g.fillEllipse (juce::Rectangle<float> (size * 0.45f, size * 0.45f).withCentre (pt));
                if (fresh > 0.0f)
                {
                    g.setColour (p.text.withAlpha (fresh));
                    g.drawLine (pt.x - size, pt.y, pt.x + size, pt.y, 0.8f);
                    g.drawLine (pt.x, pt.y - size, pt.x, pt.y + size, 0.8f);
                }
            }
            if (stars.empty())
            {
                g.setColour (p.textDim);
                g.setFont (font (13.0f));
                g.drawText ("Hold a key or switch on Autoplay - the notes appear here as stars.", r, juce::Justification::centred);
            }
        }

    private:
        struct Star { int note; float velocity, born, seed; };
        HuldraProcessor& proc;
        std::vector<Star> stars;
        int readPos = 0;
        float time = 0.0f;
        static constexpr float span = 8.0f;
        juce::Random rng;
    };

    // ---------------------------------------------------------------------------------------------
    class MotionPage : public Page
    {
    public:
        explicit MotionPage (HuldraProcessor& p) : Page (p), steps (p), stars (p)
        {
            auto& s = p.apvts;
            addAndMakeVisible (steps);
            addAndMakeVisible (stars);
            common.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::MotMode)), "Motion"), 4.2f);
            common.add (labelled (std::make_unique<Choice> (s, P::paramId (P::MotRate)), "Rate"), 1.2f);
            common.add (makeKnob (s, P::MotSwing, "Swing"));
            common.add (makeKnob (s, P::MotGate, "Gate"));
            common.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::MotFree), "Autoplay"), "Without keys"), 1.4f);
            common.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::MotLatch), "Latch"), "Hold"), 1.1f);
            common.add (makeKnob (s, P::MotBpm, "Tempo"));
            common.addTo (*this);

            arp.add (labelled (std::make_unique<Choice> (s, P::paramId (P::ArpMode)), "Arp order"), 1.6f);
            arp.add (makeKnob (s, P::ArpOct, "Octaves"));
            arp.add (makeKnob (s, P::PatLength, "Steps"));
            arp.add (labelled (std::make_unique<Choice> (s, P::paramId (P::ChordType)), "Chord"), 1.6f);
            arp.addTo (*this);

            gen.add (makeKnob (s, P::GenDensity, "Density"));
            gen.add (makeKnob (s, P::GenRange, "Range"));
            gen.add (makeKnob (s, P::GenEvolve, "Evolve"));
            gen.add (makeKnob (s, P::GenSeed, "Seed"));
            gen.add (makeKnob (s, P::GenPoly, "Notes"));
            gen.add (makeKnob (s, P::GenLength, "Length"));
            gen.add (makeKnob (s, P::GenVelRand, "Dynamics"));
            gen.add (labelled (std::make_unique<Choice> (s, P::paramId (P::SclType)), "Scale"), 1.6f);
            gen.addTo (*this);
        }

        void tick() override
        {
            const int mode = juce::roundToInt (proc.apvts.getRawParameterValue ("mot_mode")->load());
            steps.setVisible (mode == 2);
            stars.setVisible (mode != 2);
            if (steps.isVisible()) steps.tick();
            stars.tick();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, topArea.toFloat(), "");
            drawPanel (g, mainArea.toFloat(), "");
            drawPanel (g, arpArea.toFloat(), "Arp and pattern");
            drawPanel (g, genArea.toFloat(), "Generative");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            topArea = r.removeFromTop (78);
            r.removeFromTop (10);
            auto bottom = r.removeFromBottom (112);
            r.removeFromBottom (10);
            mainArea = r;
            steps.setBounds (mainArea.reduced (8, 6));
            stars.setBounds (mainArea.reduced (2));
            common.layout (topArea.reduced (12, 0).withTrimmedTop (8).withTrimmedBottom (2));
            arpArea = bottom.removeFromLeft (bottom.getWidth() * 38 / 100);
            bottom.removeFromLeft (10);
            genArea = bottom;
            arp.layout (arpArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
            gen.layout (genArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
        }

    private:
        StepEditor steps;
        Constellation stars;
        ControlRow common, arp, gen;
        juce::Rectangle<int> topArea, mainArea, arpArea, genArea;
    };
}
