#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // Glyphs for the voice filter types
    inline juce::Path filterTypeGlyph (int index, juce::Rectangle<float> r)
    {
        juce::Path p;
        const float l = r.getX(), rt = r.getRight(), t = r.getY(), b = r.getBottom(), w = r.getWidth(), m = r.getCentreY();
        switch (index)
        {
            case 0:  p.startNewSubPath (l, t + 3.0f); p.lineTo (l + w * 0.5f, t + 3.0f); p.quadraticTo (l + w * 0.75f, t, rt, b); break;
            case 1:  p.startNewSubPath (l, t + 3.0f); p.lineTo (l + w * 0.55f, t + 3.0f); p.quadraticTo (l + w * 0.65f, t, l + w * 0.75f, b); p.lineTo (rt, b); break;
            case 2:  p.startNewSubPath (l, b); p.quadraticTo (l + w * 0.25f, t, l + w * 0.5f, t + 3.0f); p.lineTo (rt, t + 3.0f); break;
            case 3:  p.startNewSubPath (l, b); p.quadraticTo (l + w * 0.5f, t - 6.0f, rt, b); break;
            case 4:  p.startNewSubPath (l, t + 3.0f); p.lineTo (l + w * 0.35f, t + 3.0f); p.quadraticTo (l + w * 0.5f, b + 6.0f, l + w * 0.65f, t + 3.0f); p.lineTo (rt, t + 3.0f); break;
            case 5:  // vowel: three formant bumps
                p.startNewSubPath (l, b);
                p.quadraticTo (l + w * 0.15f, t - 4.0f, l + w * 0.3f, m + 2.0f);
                p.quadraticTo (l + w * 0.5f, t + 2.0f, l + w * 0.65f, m + 3.0f);
                p.quadraticTo (l + w * 0.8f, m - 2.0f, rt, b);
                break;
            case 6:  // comb: teeth
                p.startNewSubPath (l, b);
                for (int i = 0; i < 5; ++i)
                {
                    const float x = l + w * (float) i / 5.0f;
                    p.quadraticTo (x + w * 0.1f, t - 2.0f, x + w * 0.2f, b);
                }
                break;
            default: // ladder: low pass with a resonant hump
                p.startNewSubPath (l, m);
                p.lineTo (l + w * 0.45f, m);
                p.quadraticTo (l + w * 0.6f, t - 6.0f, l + w * 0.68f, m);
                p.quadraticTo (l + w * 0.75f, b, rt, b);
                break;
        }
        return p;
    }

    // ---------------------------------------------------------------------------------------------
    // The four engines as cards; each draws a small animated emblem.
    class EngineCards : public juce::Component
    {
    public:
        explicit EngineCards (HuldraProcessor& p) : proc (p), engine (p.apvts, params::Engine) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void tick() { time += 1.0f / 30.0f; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            static const char* names[] = { "Classic", "Granular", "Spectral", "Vocal" };
            static const char* descs[] = { "Pitch by playback speed. Loops, ping-pong, unison.",
                                           "Clouds of tiny grains. Freeze, scatter, shimmer.",
                                           "Rebuilt from its spectrum. Blur, shift, smear.",
                                           "Pitch and time apart, formants kept. For voices." };
            const int current = juce::roundToInt (engine.plain());
            for (int i = 0; i < 4; ++i)
            {
                auto r = card (i).toFloat();
                const bool on = i == current, hov = i == hovered;
                const auto col = engineColour (i, dark);
                g.setColour (on ? col.withAlpha (dark ? 0.14f : 0.1f) : p.panel);
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (on ? col : (hov ? p.textDim : p.line));
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, on ? 1.5f : 1.0f);
                auto emblem = r.removeFromLeft (r.getHeight()).reduced (14.0f);
                drawEmblem (g, i, emblem, col, on);
                auto text = r.reduced (4.0f, 14.0f);
                g.setColour (on ? p.text : p.text.withAlpha (0.85f));
                drawSpacedText (g, juce::String (names[i]).toUpperCase(), text.removeFromTop (18.0f), 11.5f, juce::Justification::centredLeft, 0.22f, true);
                g.setColour (p.textDim);
                g.setFont (font (11.5f));
                g.drawFittedText (descs[i], text.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int h = -1;
            for (int i = 0; i < 4; ++i) if (card (i).contains (e.getPosition())) h = i;
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < 4; ++i)
                if (card (i).contains (e.getPosition())) { engine.begin(); engine.setPlain ((float) i); engine.end(); }
        }

    private:
        juce::Rectangle<int> card (int i) const
        {
            const int w = (getWidth() - 3 * 10) / 4;
            return { i * (w + 10), 0, w, getHeight() };
        }

        void drawEmblem (juce::Graphics& g, int i, juce::Rectangle<float> r, juce::Colour col, bool on)
        {
            const float speed = on ? 1.0f : 0.25f;
            const auto c = r.getCentre();
            const float rad = r.getWidth() * 0.5f;
            g.setColour (col.withAlpha (0.25f));
            g.drawEllipse (r, 1.0f);
            switch (i)
            {
                case 0:   // a wave travelling round the circle
                {
                    juce::Path w;
                    for (int k = 0; k <= 48; ++k)
                    {
                        const float a = (float) k / 48.0f * juce::MathConstants<float>::twoPi;
                        const float rr = rad * (0.62f + 0.16f * std::sin (a * 5.0f + time * 2.0f * speed));
                        const auto pt = c.getPointOnCircumference (rr, a);
                        if (k == 0) w.startNewSubPath (pt); else w.lineTo (pt);
                    }
                    g.setColour (col);
                    g.strokePath (w, juce::PathStrokeType (1.5f));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (rad, time * 1.5f * speed)));
                    break;
                }
                case 1:   // drifting grains
                    for (int k = 0; k < 14; ++k)
                    {
                        const float ph = std::fmod (time * 0.4f * speed + (float) k * 0.137f, 1.0f);
                        const float a = (float) k * 2.399f;
                        const auto pt = c.getPointOnCircumference (rad * 0.8f * ph, a);
                        const float s = 2.0f + 5.0f * std::sin (ph * juce::MathConstants<float>::pi);
                        g.setColour (col.withAlpha (0.9f * std::sin (ph * juce::MathConstants<float>::pi)));
                        g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (pt));
                    }
                    break;
                case 2:   // spectrum bars breathing
                    for (int k = 0; k < 9; ++k)
                    {
                        const float x = r.getX() + r.getWidth() * (0.18f + 0.08f * (float) k);
                        const float h = rad * (0.25f + 0.55f * std::abs (std::sin ((float) k * 0.9f + time * 1.3f * speed))) * (1.0f - 0.06f * (float) k);
                        g.setColour (col.withAlpha (0.4f + 0.06f * (float) k));
                        g.fillRoundedRectangle (x - 1.5f, c.y - h, 3.0f, h * 2.0f, 1.5f);
                    }
                    break;
                default:  // a mouth: formant curve with a moving peak
                {
                    juce::Path w;
                    for (int k = 0; k <= 40; ++k)
                    {
                        const float x = (float) k / 40.0f;
                        const float peak1 = std::exp (-std::pow ((x - 0.3f - 0.06f * std::sin (time * speed)) * 9.0f, 2.0f));
                        const float peak2 = std::exp (-std::pow ((x - 0.65f) * 8.0f, 2.0f)) * 0.7f;
                        const auto pt = juce::Point<float> (r.getX() + r.getWidth() * (0.12f + 0.76f * x), c.y + rad * 0.4f - (peak1 + peak2) * rad * 0.8f);
                        if (k == 0) w.startNewSubPath (pt); else w.lineTo (pt);
                    }
                    g.setColour (col);
                    g.strokePath (w, juce::PathStrokeType (1.6f));
                    break;
                }
            }
        }

        HuldraProcessor& proc;
        ParamDrag engine;
        int hovered = -1;
        float time = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // The visual of the selected engine
    class EngineVisual : public juce::Component
    {
    public:
        explicit EngineVisual (HuldraProcessor& p) : proc (p) {}

        void tick()
        {
            time += 1.0f / 30.0f;
            // grains (granular)
            auto& d = proc.voiceDisplay;
            const int n = d.numGrains.load();
            for (int i = 0; i < n; ++i)
            {
                Dot dot { d.grainPos[(size_t) i].load(), d.grainAge[(size_t) i].load(), d.grainPitch[(size_t) i].load(), d.grainPan[(size_t) i].load() };
                dots.push_back (dot);
            }
            if (dots.size() > 900) dots.erase (dots.begin(), dots.begin() + (long) (dots.size() - 900));
            for (auto& dt : dots) dt.fade += 1.0f / 30.0f;
            dots.erase (std::remove_if (dots.begin(), dots.end(), [] (const Dot& x) { return x.fade > 0.6f; }), dots.end());
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 10.0f);
            g.reduceClipRegion (clip);
            const int engine = juce::roundToInt (proc.apvts.getRawParameterValue ("engine")->load());
            const auto* s = displaySample (proc);
            const auto col = engineColour (engine, dark);
            if (s != nullptr)
            {
                switch (engine)
                {
                    case 0:  drawOrbit (g, r, *s, col, dark); break;
                    case 1:  drawCloud (g, r, *s, dark); break;
                    case 2:  drawSpectrum (g, r, *s, dark); break;
                    default: drawFormants (g, r, *s, col, dark); break;
                }
            }
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }

    private:
        struct Dot { float pos, age, pitch, pan; float fade = 0.0f; };

        // Classic: the sample laid around a ring, the loop arc highlighted, playheads orbiting
        void drawOrbit (juce::Graphics& g, juce::Rectangle<float> r, const SampleData& s, juce::Colour col, bool dark)
        {
            const auto& p = Theme::get().palette();
            const auto c = r.getCentre();
            const float radius = std::min (r.getWidth(), r.getHeight()) * 0.36f;
            const int n = 240;
            const float gain = 0.95f / std::max (0.05f, s.peak);
            for (int i = 0; i < n; ++i)
            {
                const int b = i * SampleData::overviewSize / n;
                const float amp = std::max (s.overviewMax[(size_t) b], -s.overviewMin[(size_t) b]) * gain;
                const float a = (float) i / (float) n * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
                const auto inner = c.getPointOnCircumference (radius - amp * radius * 0.35f, a + juce::MathConstants<float>::halfPi);
                const auto outer = c.getPointOnCircumference (radius + amp * radius * 0.35f, a + juce::MathConstants<float>::halfPi);
                g.setColour (veilAt (dark, 1.0f - s.overviewBright[(size_t) b]).withAlpha (0.85f));
                g.drawLine ({ inner, outer }, 1.6f);
            }
            // loop arc
            const auto& v = proc.apvts;
            if (v.getRawParameterValue ("smp_loop")->load() > 0.5f)
            {
                const float st = v.getRawParameterValue ("smp_start")->load(), en = v.getRawParameterValue ("smp_end")->load();
                const float a0 = st + (en - st) * v.getRawParameterValue ("smp_loop_start")->load();
                const float a1 = std::min (en, a0 + (en - st) * v.getRawParameterValue ("smp_loop_len")->load());
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, radius * 1.45f, radius * 1.45f, 0.0f, a0 * juce::MathConstants<float>::twoPi, a1 * juce::MathConstants<float>::twoPi, true);
                g.setColour (col.withAlpha (0.8f));
                g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            // playheads
            forEachPlayhead (proc, &s, [&] (float pos, float lvl, int)
            {
                const float a = pos * juce::MathConstants<float>::twoPi;
                const auto pt = c.getPointOnCircumference (radius * 1.45f, a);
                const float glow = 10.0f + 14.0f * lvl;
                g.setGradientFill (juce::ColourGradient (col.withAlpha (0.6f), pt, col.withAlpha (0.0f), pt.translated (glow, 0.0f), true));
                g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (pt));
                g.setColour (p.text);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pt));
                g.setColour (p.text.withAlpha (0.3f));
                g.drawLine ({ c, pt }, 1.0f);
            });
            g.setColour (p.textDim);
            drawSpectrumCaption (g, r, "CLASSIC  " + utf8 ("\xc2\xb7") + "  THE SAMPLE AS A RING, PLAYHEADS ORBIT");
        }

        // Granular: grains as glowing dots, x = where in the sample, y = pitch
        void drawCloud (juce::Graphics& g, juce::Rectangle<float> r, const SampleData& s, bool dark)
        {
            auto w = r.reduced (16.0f, 28.0f);
            drawWaveform (g, s, w, 0.0f, 1.0f, 0.12f);
            const auto& p = Theme::get().palette();
            g.setColour (p.line.withAlpha (0.6f));
            g.fillRect (w.getX(), w.getCentreY(), w.getWidth(), 1.0f);
            for (const auto& d : dots)
            {
                const float x = w.getX() + d.pos * w.getWidth();
                const float y = w.getCentreY() - std::clamp (d.pitch / 14.0f, -1.0f, 1.0f) * w.getHeight() * 0.46f + d.pan * 8.0f;
                const float env = std::sin (std::clamp (d.age, 0.0f, 1.0f) * juce::MathConstants<float>::pi);
                const float a = (1.0f - d.fade / 0.6f) * (0.25f + 0.75f * env);
                const float size = 3.0f + 9.0f * env;
                const auto c = veilAt (dark, 0.5f + std::clamp (d.pitch / 24.0f, -0.5f, 0.5f));
                g.setColour (c.withAlpha (a * 0.25f));
                g.fillEllipse (juce::Rectangle<float> (size * 2.2f, size * 2.2f).withCentre ({ x, y }));
                g.setColour (c.withAlpha (a));
                g.fillEllipse (juce::Rectangle<float> (size * 0.6f, size * 0.6f).withCentre ({ x, y }));
            }
            forEachPlayhead (proc, &s, [&] (float pos, float, int)
            {
                const float x = w.getX() + pos * w.getWidth();
                g.setColour (p.text.withAlpha (0.6f));
                g.fillRect (x - 0.5f, w.getY(), 1.0f, w.getHeight());
            });
            g.setColour (p.textDim);
            drawSpectrumCaption (g, r, "GRAINS  " + utf8 ("\xc2\xb7") + "  ACROSS: POSITION   UP: PITCH");
        }

        // Spectral: the frame at the playhead as bars (log frequency)
        void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const SampleData& s, bool dark)
        {
            if (s.numSpecFrames <= 0) return;
            auto w = r.reduced (16.0f, 28.0f);
            float pos = proc.apvts.getRawParameterValue ("spc_pos")->load();
            forEachPlayhead (proc, &s, [&] (float ph, float, int) { pos = ph; });
            const int frame = std::clamp ((int) (pos * (float) s.numFrames / SampleData::specHop), 0, s.numSpecFrames - 1);
            const float* mag = s.specFrameMag (frame);
            const int bars = 96;
            float maxM = 1.0e-6f;
            for (int k = 1; k < SampleData::numBins; ++k) maxM = std::max (maxM, mag[k]);
            for (int b = 0; b < bars; ++b)
            {
                const float f0 = 40.0f * std::pow (500.0f, (float) b / bars), f1 = 40.0f * std::pow (500.0f, (float) (b + 1) / bars);
                const int k0 = std::max (1, (int) (f0 * SampleData::fftSize / s.sampleRate)), k1 = std::max (k0 + 1, (int) (f1 * SampleData::fftSize / s.sampleRate));
                float m = 0.0f;
                for (int k = k0; k < k1 && k < SampleData::numBins; ++k) m = std::max (m, mag[k]);
                const float db = juce::Decibels::gainToDecibels (m / maxM, -70.0f);
                const float h = (1.0f + db / 70.0f) * w.getHeight();
                const float x = w.getX() + w.getWidth() * (float) b / bars;
                const auto c = veilAt (dark, (float) b / bars);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.9f), x, w.getBottom() - h, c.withAlpha (0.15f), x, w.getBottom(), false));
                g.fillRoundedRectangle (x + 1.0f, w.getBottom() - h, w.getWidth() / bars - 2.0f, h, 1.5f);
            }
            g.setColour (Theme::get().palette().textDim);
            drawSpectrumCaption (g, r, "SPECTRAL  " + utf8 ("\xc2\xb7") + "  THE FRAME BEING PLAYED  " + utf8 ("\xc2\xb7") + "  MORE ON THE SPECTRAL PAGE");
        }

        // Vocal: the spectral envelope (formants) of the voice, and where the formant shift moves it
        void drawFormants (juce::Graphics& g, juce::Rectangle<float> r, const SampleData& s, juce::Colour col, bool dark)
        {
            if (s.numSpecFrames <= 0) return;
            auto w = r.reduced (16.0f, 28.0f);
            float pos = proc.apvts.getRawParameterValue ("smp_start")->load() + 0.25f;
            forEachPlayhead (proc, &s, [&] (float ph, float, int) { pos = ph; });
            const int frame = std::clamp ((int) (pos * (float) s.numFrames / SampleData::specHop), 0, s.numSpecFrames - 1);
            const float* mag = s.specFrameMag (frame);
            // smooth envelope over ~ 1/3 octave
            const int points = 160;
            std::array<float, 161> env {};
            float maxE = 1.0e-6f;
            for (int i = 0; i <= points; ++i)
            {
                const float hz = 60.0f * std::pow (100.0f, (float) i / points);
                const int k = (int) (hz * SampleData::fftSize / s.sampleRate);
                const int width = std::max (2, k / 6);
                float sum = 0.0f;
                for (int j = std::max (1, k - width); j <= std::min (SampleData::numBins - 1, k + width); ++j) sum += mag[j];
                env[(size_t) i] = sum / (float) (2 * width + 1);
                maxE = std::max (maxE, env[(size_t) i]);
            }
            // to dB, then smooth (a soft envelope instead of the harmonic comb)
            for (auto& e : env) e = juce::Decibels::gainToDecibels (e / maxE, -60.0f);
            for (int pass = 0; pass < 6; ++pass)
            {
                auto copy = env;
                for (int i = 1; i < points; ++i)
                    env[(size_t) i] = 0.25f * copy[(size_t) i - 1] + 0.5f * copy[(size_t) i] + 0.25f * copy[(size_t) i + 1];
            }
            const float shift = proc.apvts.getRawParameterValue ("voc_formant")->load();
            auto curve = [&] (float semis)
            {
                juce::Path path;
                for (int i = 0; i <= points; ++i)
                {
                    const float x = w.getX() + w.getWidth() * ((float) i / points + semis / 12.0f / std::log2 (100.0f));
                    const float db = env[(size_t) i];
                    const float y = w.getBottom() - (1.0f + db / 60.0f) * w.getHeight();
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                return path;
            };
            const auto& p = Theme::get().palette();
            const auto original = curve (0.0f);
            g.setColour (p.textDim.withAlpha (0.6f));
            g.strokePath (original, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto moved = curve (shift);
            juce::Path fill (moved);
            fill.lineTo (moved.getBounds().getRight(), w.getBottom());
            fill.lineTo (moved.getBounds().getX(), w.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.35f), 0.0f, w.getY(), veilAt (dark, 1.0f).withAlpha (0.03f), 0.0f, w.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col);
            g.strokePath (moved, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
            {
                const float x = w.getX() + w.getWidth() * std::log2 (hz / 60.0f) / std::log2 (100.0f);
                g.setColour (p.line);
                g.fillRect (x, w.getY(), 1.0f, w.getHeight());
                g.setColour (p.textDim);
                g.setFont (font (9.5f));
                g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz), juce::Rectangle<float> (x + 3.0f, w.getY(), 40.0f, 12.0f),
                            juce::Justification::centredLeft);
            }
            g.setColour (p.textDim);
            drawSpectrumCaption (g, r, "VOCAL  " + utf8 ("\xc2\xb7") + "  FORMANTS (GREY: ORIGINAL)  " + utf8 ("\xc2\xb7") + "  SHIFT "
                                       + juce::String (shift, 1) + " ST");
        }

        static void drawSpectrumCaption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
        {
            drawSpacedText (g, text, r.reduced (16.0f, 8.0f).removeFromTop (14.0f), 9.0f, juce::Justification::centredLeft, 0.2f);
        }

        HuldraProcessor& proc;
        std::vector<Dot> dots;
        float time = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Frequency response of the voice filter, with the cutoff moving live with the filter envelope.
    class FilterCurve : public juce::Component
    {
    public:
        explicit FilterCurve (HuldraProcessor& p) : proc (p) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 6.0f);
            auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
            const bool on = v ("flt_on") > 0.5f;
            const int type = juce::roundToInt (v ("flt_type"));
            const float envNow = proc.voiceDisplay.sources[(size_t) params::ModSource::FilterEnv].load();
            const float cutoff = std::clamp (v ("flt_cutoff") * std::exp2 (v ("flt_env") * envNow), 20.0f, 20000.0f);
            const float reso = v ("flt_reso"), vowelPos = v ("flt_vowel");
            const double sr = 48000.0;
            const double q = 0.5 + reso * 9.0;

            dsp::Biquad a, b, c;
            static const float formants[5][3] = { { 800, 1150, 2900 }, { 400, 2000, 2800 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            auto mag = [&] (double f) -> double
            {
                switch (type)
                {
                    case 0: a.lowpass (sr, cutoff, q); return a.magnitude (f, sr);
                    case 1: a.lowpass (sr, cutoff, q); return std::pow (a.magnitude (f, sr), 2.0);
                    case 2: a.highpass (sr, cutoff, q); return a.magnitude (f, sr);
                    case 3: a.bandpass (sr, cutoff, q); return a.magnitude (f, sr) * 1.5;
                    case 4: a.notch (sr, cutoff, q); return a.magnitude (f, sr);
                    case 5:
                    {
                        const int i0 = std::clamp ((int) vowelPos, 0, 4), i1 = std::min (i0 + 1, 4);
                        const float t = vowelPos - (float) i0;
                        const float shift = std::sqrt (cutoff / 1000.0f);
                        double sum = 0.0;
                        const double gains[] = { 1.0, 0.6, 0.35 };
                        dsp::Biquad* bq[] = { &a, &b, &c };
                        for (int k = 0; k < 3; ++k)
                        {
                            const double fk = std::clamp ((formants[i0][k] + (formants[i1][k] - formants[i0][k]) * t) * shift, 40.0f, 20000.0f);
                            bq[k]->bandpass (sr, fk, 4.0 + reso * 10.0);
                            sum += bq[k]->magnitude (f, sr) * gains[k];
                        }
                        return sum * 1.4;
                    }
                    case 6:
                    {
                        const double fb = 0.5 + reso * 0.48, period = 1.0 / cutoff;
                        const std::complex<double> e = std::polar (1.0, -juce::MathConstants<double>::twoPi * f * period);
                        return std::abs (1.0 / (1.0 - fb * e)) * (1.0 - fb * 0.5) * 0.6;
                    }
                    default:
                    {
                        a.lowpass (sr, cutoff, 0.5 + reso * 4.0);
                        return std::pow (a.magnitude (f, sr), 2.0);
                    }
                }
            };

            juce::Path curve;
            const int n = 140;
            for (int i = 0; i <= n; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, (double) i / n);
                const double db = on ? juce::Decibels::gainToDecibels (mag (f), -60.0) : 0.0;
                const float x = r.getX() + r.getWidth() * (float) i / n;
                const float y = r.getCentreY() - (float) std::clamp (db, -36.0, 18.0) / 36.0f * r.getHeight() * 0.48f - r.getHeight() * 0.12f;
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            juce::Path fill (curve);
            fill.lineTo (r.getRight(), r.getBottom());
            fill.lineTo (r.getX(), r.getBottom());
            fill.closeSubPath();
            const auto col = on ? p.accent : p.textDim;
            g.setGradientFill (juce::ColourGradient (veilAt (dark, 0.0f).withAlpha (on ? 0.3f : 0.08f), 0.0f, r.getY(), veilAt (dark, 1.0f).withAlpha (0.02f), 0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col);
            g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (on)
            {
                const float x = r.getX() + r.getWidth() * std::log (cutoff / 20.0f) / std::log (1000.0f);
                g.setColour (p.text.withAlpha (0.5f));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, r.getBottom() - 6.0f }));
            }
        }

    private:
        HuldraProcessor& proc;
    };

    // ---------------------------------------------------------------------------------------------
    class EnginePage : public Page
    {
    public:
        explicit EnginePage (HuldraProcessor& p)
            : Page (p), cards (p), visual (p), curve (p),
              fenvGraph (p.apvts, P::FenvAttack, P::FenvDecay, P::FenvSustain, P::FenvRelease,
                         [&p] { return p.voiceDisplay.sources[(size_t) P::ModSource::FilterEnv].load(); }),
              filterLock (p.apvts, locks::ids().filter), fenvLock (p.apvts, locks::ids().filterEnv),
              engineLocks { PanelLock (p.apvts, locks::ids().voice), PanelLock (p.apvts, locks::ids().granular),
                            PanelLock (p.apvts, locks::ids().spectral), PanelLock (p.apvts, locks::ids().vocal) }
        {
            addAndMakeVisible (filterLock);
            addAndMakeVisible (fenvLock);
            for (auto& l : engineLocks) addChildComponent (l);
            auto& s = p.apvts;
            addAndMakeVisible (cards);
            addAndMakeVisible (visual);

            // Classic
            rows[0].add (makeKnob (s, P::UniVoices, "Unison"));
            rows[0].add (makeKnob (s, P::UniDetune, "Detune"));
            rows[0].add (makeKnob (s, P::UniSpread, "Spread"));
            rows[1].add (makeKnob (s, P::Drive, "Drive"));
            rows[1].add (makeKnob (s, P::KeyTrack, "Keytrack"));
            rows[1].add (makeKnob (s, P::Glide, "Glide"));
            // Granular
            rows[2].add (makeKnob (s, P::GrnPos, "Position"));
            rows[2].add (makeKnob (s, P::GrnScan, "Scan"));
            rows[2].add (makeKnob (s, P::GrnSpray, "Spray"));
            rows[2].add (makeKnob (s, P::GrnSize, "Size"));
            rows[2].add (makeKnob (s, P::GrnDensity, "Density"));
            rows[2].add (labelled (std::make_unique<Toggle> (s, P::paramId (P::GrnFreeze), "Freeze"), "Hold"), 1.1f);
            rows[3].add (makeKnob (s, P::GrnPitchRand, "Detune"));
            rows[3].add (makeKnob (s, P::GrnOctaves, "Octaves"));
            rows[3].add (makeKnob (s, P::GrnPan, "Spread"));
            rows[3].add (makeKnob (s, P::GrnReverse, "Reverse"));
            rows[3].add (makeKnob (s, P::GrnJitter, "Jitter"));
            rows[3].add (labelled (std::make_unique<Choice> (s, P::paramId (P::GrnWindow)), "Shape"), 1.1f);
            // Spectral
            rows[4].add (makeKnob (s, P::SpcPos, "Position"));
            rows[4].add (makeKnob (s, P::SpcScan, "Speed"));
            rows[4].add (makeKnob (s, P::SpcBlur, "Blur"));
            rows[4].add (labelled (std::make_unique<Toggle> (s, P::paramId (P::SpcFreeze), "Freeze"), "Hold"), 1.1f);
            rows[5].add (makeKnob (s, P::SpcSmear, "Smear"));
            rows[5].add (makeKnob (s, P::SpcHarm, "Harmonize"));
            rows[5].add (makeKnob (s, P::SpcShift, "Shift"));
            rows[5].add (makeKnob (s, P::SpcStretch, "Stretch"));
            // Vocal
            rows[6].add (makeKnob (s, P::VocSpeed, "Speed"));
            rows[6].add (makeKnob (s, P::VocFormant, "Formant"));
            rows[6].add (labelled (std::make_unique<Toggle> (s, P::paramId (P::VocKeep), "Keep"), "Formants"), 1.1f);
            rows[6].add (makeKnob (s, P::VocTonal, "Tonality"));
            rows[7].add (makeKnob (s, P::RetuneAmt, "Retune"));
            rows[7].add (makeKnob (s, P::RetuneSpeed, "Speed"));
            rows[7].add (labelled (std::make_unique<Toggle> (s, P::paramId (P::SclOn), "Scale"), "Snap"), 1.1f);
            rows[7].add (labelled (std::make_unique<Choice> (s, P::paramId (P::SclRoot)), "Key"), 1.0f);
            rows[7].add (labelled (std::make_unique<Choice> (s, P::paramId (P::SclType)), "Scale"), 1.6f);
            for (auto& r : rows) r.addTo (*this);

            filterTop.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::FltOn), "On"), "Filter"), 0.9f);
            filterTop.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::FltType), filterTypeGlyph), "Type"), 5.0f);
            filterTop.addTo (*this);
            filterKnobs.add (makeKnob (s, P::FltCutoff, "Cutoff"));
            filterKnobs.add (makeKnob (s, P::FltReso, "Reso"));
            filterKnobs.add (makeKnob (s, P::FltDrive, "Drive"));
            filterKnobs.add (makeKnob (s, P::FltEnv, "Env"));
            filterKnobs.add (makeKnob (s, P::FltKey, "Keytrack"));
            filterKnobs.add (makeKnob (s, P::FltVel, "Velocity"));
            filterKnobs.add (makeKnob (s, P::FltVowel, "Vowel"));
            filterKnobs.addTo (*this);
            addAndMakeVisible (curve);
            fenv.add (makeKnob (s, P::FenvAttack, "A"));
            fenv.add (makeKnob (s, P::FenvDecay, "D"));
            fenv.add (makeKnob (s, P::FenvSustain, "S"));
            fenv.add (makeKnob (s, P::FenvRelease, "R"));
            fenv.addTo (*this);
            addAndMakeVisible (fenvGraph);
            updateVisibility();
        }

        void tick() override
        {
            cards.tick();
            visual.tick();
            curve.repaint();
            fenvGraph.repaint();
            const int e = juce::roundToInt (proc.apvts.getRawParameterValue ("engine")->load());
            if (e != shownEngine) { updateVisibility(); repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            static const char* titles[] = { "Classic", "Granular", "Spectral", "Vocal" };
            drawPanel (g, knobArea.toFloat(), titles[std::clamp (shownEngine, 0, 3)]);
            drawPanel (g, filterArea.toFloat(), "Filter");
            drawPanel (g, fenvArea.toFloat(), "Filter Envelope");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            cards.setBounds (r.removeFromTop (78));
            r.removeFromTop (10);
            auto bottom = r.removeFromBottom (186);
            r.removeFromBottom (10);
            knobArea = r.removeFromRight (470);
            r.removeFromRight (10);
            visual.setBounds (r);
            auto k = knobArea.reduced (10, 0).withTrimmedTop (30);
            const int h = (k.getHeight() - 10) / 2;
            for (int e = 0; e < 4; ++e)
            {
                auto kk = k;
                rows[(size_t) e * 2].layout (kk.removeFromTop (h));
                kk.removeFromTop (10);
                rows[(size_t) e * 2 + 1].layout (kk.removeFromTop (h));
            }

            filterArea = bottom.removeFromLeft (bottom.getWidth() * 66 / 100);
            bottom.removeFromLeft (10);
            fenvArea = bottom;
            filterLock.place (filterArea);
            fenvLock.place (fenvArea);
            for (auto& l : engineLocks) l.place (knobArea);
            auto f = filterArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (8);
            auto top = f.removeFromTop (48);
            curve.setBounds (top.removeFromRight (230).reduced (2, 0));
            top.removeFromRight (10);
            filterTop.layout (top.translated (0, 6));
            filterKnobs.layout (f.withTrimmedTop (6));
            auto e = fenvArea.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (8);
            fenvGraph.setBounds (e.removeFromTop (60).reduced (4, 0));
            fenv.layout (e.withTrimmedTop (4));
        }

    private:
        void updateVisibility()
        {
            shownEngine = juce::roundToInt (proc.apvts.getRawParameterValue ("engine")->load());
            for (int i = 0; i < (int) rows.size(); ++i)
                rows[(size_t) i].setVisible (i / 2 == shownEngine);
            for (int e = 0; e < 4; ++e) engineLocks[(size_t) e].setVisible (e == shownEngine);
        }

        EngineCards cards;
        EngineVisual visual;
        FilterCurve curve;
        AdsrGraph fenvGraph;
        PanelLock filterLock, fenvLock;
        std::array<PanelLock, 4> engineLocks;   // classic (voice), granular, spectral, vocal: the panel shows the shown engine's lock
        std::array<ControlRow, 8> rows;
        ControlRow filterTop, filterKnobs, fenv;
        juce::Rectangle<int> knobArea, filterArea, fenvArea;
        int shownEngine = -1;
    };
}
