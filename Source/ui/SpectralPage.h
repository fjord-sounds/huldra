#pragma once
#include "Widgets.h"

namespace huldra::ui
{
    // Frequency axis used by the spectrogram and the mask (log, 40 Hz .. 20 kHz) – the same as the DSP mask.
    inline float freqToY (float hz, juce::Rectangle<float> r)
    {
        const float t = std::log2 (std::max (40.0f, hz) / 40.0f) / std::log2 (500.0f);
        return r.getBottom() - std::clamp (t, 0.0f, 1.0f) * r.getHeight();
    }

    inline float yToFreq (float y, juce::Rectangle<float> r)
    {
        const float t = std::clamp ((r.getBottom() - y) / r.getHeight(), 0.0f, 1.0f);
        return 40.0f * std::pow (500.0f, t);
    }

    // Colour ramp for the spectrogram: background -> rose -> lilac -> ice -> white
    inline juce::Colour heat (float x, bool dark)
    {
        const auto& p = Theme::get().palette();
        const auto c = veilColours (dark);
        x = std::clamp (x, 0.0f, 1.0f);
        if (x < 0.3f) return p.panel.interpolatedWith (c[0].withMultipliedBrightness (dark ? 0.6f : 1.0f), x / 0.3f);
        if (x < 0.6f) return c[0].withMultipliedBrightness (dark ? 0.6f : 1.0f).interpolatedWith (c[1], (x - 0.3f) / 0.3f);
        if (x < 0.85f) return c[1].interpolatedWith (c[2], (x - 0.6f) / 0.25f);
        return c[2].interpolatedWith (dark ? juce::Colours::white : c[2].darker (0.5f), (x - 0.85f) / 0.15f);
    }

    class Spectrogram : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Spectrogram (HuldraProcessor& p) : proc (p), pos (p.apvts, params::SpcPos)
        {
            setTooltip ("The sound as a spectrogram. Drag to set the spectral position (where it freezes).");
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }

        void tick() { repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            const auto* s = displaySample (proc);
            const auto area = plot();
            if (s != nullptr && s->numSpecFrames > 1)
            {
                if (s != cachedFor || dark != cachedDark || image.getWidth() != (int) area.getWidth())
                    renderImage (*s, (int) area.getWidth(), (int) area.getHeight(), dark);
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path clip;
                clip.addRoundedRectangle (area, 6.0f);
                g.reduceClipRegion (clip);
                g.drawImage (image, area);

                // mask: attenuated bands are veiled
                const auto mask = proc.instrument.snapshot().mask;
                const float amount = proc.apvts.getRawParameterValue ("spc_mask")->load();
                for (int b = 0; b < kMaskBands; ++b)
                {
                    const float y0 = area.getBottom() - area.getHeight() * (float) (b + 1) / kMaskBands;
                    const float shade = (1.0f - mask[(size_t) b]) * amount;
                    if (shade > 0.01f)
                    {
                        g.setColour (p.panel.withAlpha (shade * 0.75f));
                        g.fillRect (area.getX(), y0, area.getWidth(), area.getHeight() / kMaskBands + 0.5f);
                    }
                }

                // spectral position / freeze line
                const float sp = proc.apvts.getRawParameterValue ("spc_pos")->load();
                const bool frozen = proc.apvts.getRawParameterValue ("spc_freeze")->load() > 0.5f;
                const float x = area.getX() + sp * area.getWidth();
                g.setColour ((frozen ? p.accent : p.text).withAlpha (0.85f));
                const float dash[] = { 4.0f, 4.0f };
                g.drawDashedLine ({ x, area.getY(), x, area.getBottom() }, dash, 2, frozen ? 2.0f : 1.0f);
                if (frozen)
                {
                    g.setColour (p.accent);
                    drawSpacedText (g, "FROZEN", { x + 6.0f, area.getY() + 4.0f, 70.0f, 14.0f }, 9.0f, juce::Justification::centredLeft, 0.2f, true);
                }

                forEachPlayhead (proc, s, [&] (float ph, float lvl, int)
                {
                    const float px = area.getX() + ph * area.getWidth();
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), px - 14.0f, 0.0f,
                                                             juce::Colours::white.withAlpha (0.35f * std::min (1.0f, lvl + 0.3f)), px, 0.0f, false));
                    g.fillRect (px - 14.0f, area.getY(), 14.0f, area.getHeight());
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.fillRect (px - 0.5f, area.getY(), 1.0f, area.getHeight());
                });
            }
            // frequency labels
            g.setColour (p.textDim);
            g.setFont (font (9.5f));
            for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
                g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (r.getX() + 4.0f, freqToY (hz, area) - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override { pos.begin(); mouseDrag (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto a = plot();
            pos.setPlain (juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth()));
        }
        void mouseUp (const juce::MouseEvent&) override { pos.end(); }

        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f).withTrimmedLeft (30.0f); }

    private:
        void renderImage (const SampleData& s, int w, int h, bool dark)
        {
            cachedFor = &s;
            cachedDark = dark;
            image = juce::Image (juce::Image::RGB, std::max (1, w), std::max (1, h), false);
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
            // reference level: the loudest bin
            float maxM = 1.0e-6f;
            for (size_t i = 0; i < s.specMag.size(); i += 7) maxM = std::max (maxM, s.specMag[i]);
            const juce::Rectangle<float> unit (0.0f, 0.0f, 1.0f, (float) h);
            std::vector<int> binOfRow ((size_t) h);
            for (int y = 0; y < h; ++y)
                binOfRow[(size_t) y] = std::clamp ((int) (yToFreq ((float) y + 0.5f, unit) * SampleData::fftSize / s.sampleRate), 1, SampleData::numBins - 1);
            for (int x = 0; x < w; ++x)
            {
                const int frame = std::clamp ((int) ((float) x / (float) w * (float) s.numFrames / SampleData::specHop), 0, s.numSpecFrames - 1);
                const float* mag = s.specFrameMag (frame);
                for (int y = 0; y < h; ++y)
                {
                    const int k = binOfRow[(size_t) y];
                    const int k2 = y > 0 ? binOfRow[(size_t) y - 1] : k;
                    float m = 0.0f;
                    for (int j = k; j <= std::max (k, k2); ++j) m = std::max (m, mag[j]);
                    const float db = juce::Decibels::gainToDecibels (m / maxM, -80.0f);
                    data.setPixelColour (x, y, heat (1.0f + db / 80.0f, dark));
                }
            }
        }

        HuldraProcessor& proc;
        ParamDrag pos;
        juce::Image image;
        const SampleData* cachedFor = nullptr;
        bool cachedDark = false;
    };

    // Paint the spectral mask: one bar per band, drag across to draw. Shows the live output spectrum behind.
    class MaskPainter : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MaskPainter (HuldraProcessor& p) : proc (p), fft (11)
        {
            setTooltip ("Paint the spectral mask (drag left to cut a band, right to let it through). Double-click: reset.");
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
        }

        juce::Rectangle<float> area;   // set by the page to align with the spectrogram

        void tick()
        {
            std::array<float, 4096> buf {};
            proc.scope.copyLatest (buf.data(), nullptr, 2048);
            for (int i = 0; i < 2048; ++i) buf[(size_t) i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 2048.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int b = 0; b < 64; ++b)
            {
                const float f0 = 40.0f * std::pow (500.0f, (float) b / 64.0f), f1 = 40.0f * std::pow (500.0f, (float) (b + 1) / 64.0f);
                const double sr = std::max (8000.0, proc.getCurrentSampleRate());
                float m = 0.0f;
                for (int k = (int) (f0 * 2048.0 / sr); k <= (int) (f1 * 2048.0 / sr) && k < 1024; ++k) m = std::max (m, buf[(size_t) k]);
                const float db = juce::Decibels::gainToDecibels (m / 300.0f, -80.0f);
                live[(size_t) b] = std::max (live[(size_t) b] - 1.5f, db);
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
            const auto a = area.isEmpty() ? r.reduced (10.0f) : area;

            // live output spectrum (horizontal: level)
            juce::Path spec;
            for (int b = 0; b < 64; ++b)
            {
                const float y = a.getBottom() - a.getHeight() * ((float) b + 0.5f) / 64.0f;
                const float x = a.getX() + a.getWidth() * std::clamp (1.0f + live[(size_t) b] / 70.0f, 0.0f, 1.0f);
                if (b == 0) spec.startNewSubPath (a.getX(), y);
                spec.lineTo (x, y);
            }
            spec.lineTo (a.getX(), a.getY());
            g.setColour (p.textDim.withAlpha (0.18f));
            g.fillPath (spec);

            const auto mask = proc.instrument.snapshot().mask;
            const float bh = a.getHeight() / kMaskBands;
            for (int b = 0; b < kMaskBands; ++b)
            {
                const float y = a.getBottom() - bh * (float) (b + 1);
                const float v = mask[(size_t) b];
                const auto c = veilAt (dark, (float) b / kMaskBands);
                g.setColour (c.withAlpha (0.18f));
                g.fillRoundedRectangle (a.getX(), y + 1.0f, a.getWidth(), bh - 2.0f, 2.0f);
                g.setColour (c.withAlpha (0.85f));
                g.fillRoundedRectangle (a.getX(), y + 1.0f, std::max (2.0f, a.getWidth() * v), bh - 2.0f, 2.0f);
            }
            g.setColour (p.textDim);
            drawSpacedText (g, "MASK", r.reduced (10.0f, 4.0f).removeFromTop (12.0f), 8.5f, juce::Justification::centredLeft, 0.25f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            proc.undoManager.beginNewTransaction ("Paint mask");
            last = e.position;
            paintAt (e.position, e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            paintAt (last, e.position);
            last = e.position;
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            std::array<float, kMaskBands> m;
            m.fill (1.0f);
            proc.instrument.setMask (m, true);
        }

    private:
        // Interpolate between the last and the current mouse position so fast strokes leave no gaps
        void paintAt (juce::Point<float> from, juce::Point<float> to)
        {
            const auto a = area.isEmpty() ? getLocalBounds().toFloat().reduced (10.0f) : area;
            auto m = proc.instrument.getMask();
            const int steps = 1 + (int) (std::abs (to.y - from.y) / 2.0f);
            for (int i = 0; i <= steps; ++i)
            {
                const auto pt = from + (to - from) * ((float) i / (float) steps);
                const int b = std::clamp ((int) ((a.getBottom() - pt.y) / a.getHeight() * kMaskBands), 0, kMaskBands - 1);
                m[(size_t) b] = std::clamp ((pt.x - a.getX()) / a.getWidth(), 0.0f, 1.0f);
            }
            proc.instrument.setMask (m, false);
        }

        HuldraProcessor& proc;
        juce::dsp::FFT fft;
        std::array<float, 64> live {};
        juce::Point<float> last;
    };

    class SpectralPage : public Page
    {
    public:
        explicit SpectralPage (HuldraProcessor& p) : Page (p), spectrogram (p), mask (p), useSpectral ("Use the spectral engine", true)
        {
            auto& s = p.apvts;
            addAndMakeVisible (spectrogram);
            addAndMakeVisible (mask);
            row.add (makeKnob (s, P::SpcPos, "Position"));
            row.add (makeKnob (s, P::SpcScan, "Speed"));
            row.add (labelled (std::make_unique<Toggle> (s, P::paramId (P::SpcFreeze), "Freeze"), "Hold"), 1.0f);
            row.add (makeKnob (s, P::SpcBlur, "Blur"));
            row.add (makeKnob (s, P::SpcSmear, "Smear"));
            row.add (makeKnob (s, P::SpcShift, "Shift"));
            row.add (makeKnob (s, P::SpcStretch, "Stretch"));
            row.add (makeKnob (s, P::SpcTilt, "Tilt"));
            row.add (makeKnob (s, P::SpcHarm, "Harmonize"));
            row.add (makeKnob (s, P::SpcGate, "Gate"));
            row.add (makeKnob (s, P::SpcMask, "Mask"));
            row.addTo (*this);
            addAndMakeVisible (useSpectral);
            useSpectral.onClick = [this]
            {
                auto* prm = proc.apvts.getParameter (params::paramId (params::Engine));
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (2.0f));
                prm->endChangeGesture();
            };
        }

        void tick() override
        {
            spectrogram.tick();
            mask.tick();
            useSpectral.setVisible (juce::roundToInt (proc.apvts.getRawParameterValue ("engine")->load()) != 2);
        }

        void paint (juce::Graphics& g) override { drawPanel (g, controls.toFloat(), "Spectral Engine"); }

        void resized() override
        {
            auto r = getLocalBounds();
            controls = r.removeFromBottom (112);
            r.removeFromBottom (10);
            mask.setBounds (r.removeFromRight (190));
            r.removeFromRight (10);
            spectrogram.setBounds (r);
            const auto plot = spectrogram.plot();
            mask.area = juce::Rectangle<float> (12.0f, plot.getY(), (float) mask.getWidth() - 24.0f, plot.getHeight());
            row.layout (controls.reduced (10, 0).withTrimmedTop (30).withTrimmedBottom (6));
            useSpectral.setBounds (controls.getRight() - 214, controls.getY() + 6, 200, 24);
        }

    private:
        Spectrogram spectrogram;
        MaskPainter mask;
        ControlRow row;
        PillButton useSpectral;
        juce::Rectangle<int> controls;
    };
}
