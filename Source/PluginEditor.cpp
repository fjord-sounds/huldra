#include "PluginEditor.h"

using namespace huldra;

namespace
{
    const juce::URL githubUrl { "https://github.com/fjord-sounds/huldra" };

    juce::Path githubMark()
    {
        static const juce::Path mark = juce::Drawable::parseSVGPath (
            "M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49"
            "-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52"
            ".28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18"
            " 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87"
            " 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z");
        return mark;
    }

    juce::Path undoArrow (juce::Rectangle<float> r, bool redo)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float rad = r.getWidth() * 0.32f;
        p.addCentredArc (c.x, c.y + 1.5f, rad, rad, 0.0f, -2.2f, 1.2f, true);
        const auto start = c.translated (0, 1.5f).getPointOnCircumference (rad, -2.2f);
        p.startNewSubPath (start.translated (-0.5f, -3.5f));
        p.lineTo (start);
        p.lineTo (start.translated (3.5f, 0.5f));
        if (redo)
            p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, c.x, c.y));
        return p;
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf;wave");
    }
}

HuldraView::HuldraView (HuldraProcessor& p)
    : proc (p),
      sourcePage (p), slicePage (p), keymapPage (p), enginePage (p), spectralPage (p), keysPage (p), modPage (p), motionPage (p), fxPage (p),
      pages { &sourcePage, &slicePage, &keymapPage, &enginePage, &spectralPage, &keysPage, &modPage, &motionPage, &fxPage },
      footer (p),
      presetBar (p.presetManager),
      master (p.apvts, params::paramId (params::MasterGain), "Master"),
      playMode (p.apvts, params::paramId (params::PlayMode), nullptr, { "Melodic", "Chops" }),
      engine (p.apvts, params::paramId (params::Engine), nullptr, { "Classic", "Granular", "Spectral", "Vocal" }),
      browser (p.presetManager),
      saveDialog (p.presetManager),
      levelMeter ([&p] { return p.takeOutputPeaks(); }, [&p] { return p.voiceDisplay.noteCounter.load(); }),
      about ([&p] { return p.getDebugReport(); }, [&p] { p.requestPanic(); })
{
    setLookAndFeel (&lookAndFeel);
    ui::Theme::get().addChangeListener (this);

    for (auto* pg : pages) addChildComponent (pg);
    addAndMakeVisible (footer);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (master);
    addAndMakeVisible (playMode);
    addAndMakeVisible (engine);
    addAndMakeVisible (levelMeter);
    playMode.setTooltip ("Melodic: the sound plays across the keyboard.  Chops: every key plays one slice.");
    engine.setTooltip ("The engine that plays the sample (zones and keys can choose their own)");

    addChildComponent (browser);
    addChildComponent (saveDialog);
    addChildComponent (about);
    presetBar.onOpenBrowser = [this] { setBrowserVisible (! browser.isVisible()); };
    presetBar.onSave = [this] { saveDialog.open(); };
    browser.onSave = [this] { saveDialog.open(); };
    browser.onClose = [this] { presetBar.setBrowserOpen (false); playMode.setVisible (true); engine.setVisible (true); };

    setWantsKeyboardFocus (false);
    applyTheme();
    showPage (juce::jlimit (0, 8, (int) proc.apvts.state.getProperty ("uiPage", 0)));
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

HuldraView::~HuldraView()
{
    ui::Theme::get().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void HuldraView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyTheme();
}

void HuldraView::applyTheme()
{
    lookAndFeel.applyPalette();
    sendLookAndFeelChange();
    repaint();
}

ui::Page* HuldraView::currentPage() const { return pages[(size_t) page]; }

void HuldraView::timerCallback() { tickAnimations(); }

void HuldraView::tickAnimations()
{
    if (auto* pg = currentPage(); pg != nullptr && pg->isVisible() && ! browser.isVisible())
        pg->tick();
    footer.tick();
    if (! isTimerRunning() || ! isShowing())
        levelMeter.tick();

    // The logo dot lights up with every note
    const auto notes = proc.voiceDisplay.noteCounter.load();
    if (notes != lastNotes)
    {
        lastNotes = notes;
        logoFlash = 0.4f + 0.6f * proc.voiceDisplay.lastVelocity.load();
        repaint (logoBounds());
    }
    else if (logoFlash > 0.0f)
    {
        logoFlash = std::max (0.0f, logoFlash - 0.08f);
        repaint (logoBounds());
    }

    if (const auto err = proc.instrument.getLastError(); err.isNotEmpty() && err != lastError)
    {
        lastError = err;
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Huldra", err);
    }
}

void HuldraView::setBrowserVisible (bool shouldShow)
{
    if (shouldShow) browser.open();
    else            browser.close();
    presetBar.setBrowserOpen (shouldShow);
    playMode.setVisible (! shouldShow);
    engine.setVisible (! shouldShow);
}

void HuldraView::showPage (int newPage)
{
    setBrowserVisible (false);
    page = newPage;
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible ((int) i == page);
    proc.apvts.state.setProperty ("uiPage", page, nullptr);
    repaint();
}

juce::Rectangle<int> HuldraView::tabBounds (int index) const
{
    return { margin + index * tabWidth, headerHeight, tabWidth, tabHeight };
}

juce::Rectangle<int> HuldraView::themeToggleBounds() const
{
    return { getWidth() - margin - 130, (headerHeight - 28) / 2, 28, 28 };
}

juce::String HuldraView::getTooltip()
{
    switch (hover)
    {
        case HeaderHover::Logo:   return "Click: about and debug info  |  Double-click: stop all sound";
        case HeaderHover::Github: return "Huldra on GitHub";
        case HeaderHover::Theme:  return "Light / dark";
        case HeaderHover::Undo:   return "Undo (Cmd+Z)";
        case HeaderHover::Redo:   return "Redo (Cmd+Shift+Z)";
        case HeaderHover::None:
        default:                  return {};
    }
}

void HuldraView::paint (juce::Graphics& g)
{
    const auto& pal = ui::Theme::get().palette();
    const bool dark = ui::Theme::get().isDark();
    g.fillAll (pal.background);
    ui::drawVeilGlow (g, getLocalBounds().toFloat().withHeight ((float) (headerHeight + tabHeight) * 1.6f), dark);

    // Logo: lower case, light, with the rose dot that glows on every note
    g.setColour (pal.text);
    const auto logoFont = ui::font (30.0f).withExtraKerningFactor (0.04f);
    g.setFont (logoFont);
    g.drawText ("huldra", margin + 4, 0, 130, headerHeight, juce::Justification::centredLeft);
    const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, "huldra");
    const juce::Point<float> dot { (float) margin + 4.0f + logoW + 5.5f, (float) headerHeight * 0.5f + 7.5f };
    if (logoFlash > 0.0f)
    {
        const float rad = 6.0f + 16.0f * logoFlash;
        g.setGradientFill (juce::ColourGradient (pal.accent.withAlpha (0.55f * logoFlash), dot, pal.accent.withAlpha (0.0f),
                                                 dot.translated (rad, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (dot));
    }
    g.setColour (pal.accent.interpolatedWith (dark ? juce::Colours::white : pal.accent, logoFlash * 0.5f));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));

    // Tabs
    for (int i = 0; i < tabNames.size(); ++i)
    {
        const auto r = tabBounds (i).toFloat();
        const bool active = i == page;
        g.setColour (active ? pal.text : (i == hoveredTab ? pal.text.withAlpha (0.75f) : pal.textDim));
        ui::drawSpacedText (g, tabNames[i].toUpperCase(), r, 11.0f, juce::Justification::centred, 0.2f, active);
        if (active)
        {
            g.setColour (pal.accent);
            g.fillRoundedRectangle (r.getCentreX() - 10.0f, r.getBottom() - 9.0f, 20.0f, 2.0f, 1.0f);
        }
    }

    // Undo / redo
    auto* um = &proc.undoManager;
    for (bool redo : { false, true })
    {
        const auto r = (redo ? redoBounds() : undoBounds()).toFloat().reduced (5.0f);
        const bool can = redo ? um->canRedo() : um->canUndo();
        const bool hov = hover == (redo ? HeaderHover::Redo : HeaderHover::Undo);
        g.setColour (can ? (hov ? pal.text : pal.textDim) : pal.line);
        g.strokePath (undoArrow (r, redo), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Theme switch: sun / moon
    {
        const auto r = themeToggleBounds().toFloat().reduced (6.0f);
        g.setColour (hover == HeaderHover::Theme ? pal.text : pal.textDim);
        if (dark)
        {
            g.drawEllipse (r.reduced (2.0f), 1.4f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                const auto c = r.getCentre();
                g.drawLine ({ c.getPointOnCircumference (r.getWidth() * 0.62f, a), c.getPointOnCircumference (r.getWidth() * 0.78f, a) }, 1.2f);
            }
        }
        else
        {
            juce::Path moon;
            moon.addEllipse (r);
            juce::Path cut;
            cut.addEllipse (r.translated (r.getWidth() * 0.38f, -r.getHeight() * 0.22f));
            moon.setUsingNonZeroWinding (false);
            moon.addPath (cut);
            g.fillPath (moon);
        }
    }

    {
        auto mark = githubMark();
        const auto r = githubBounds().toFloat().reduced (6.0f);
        mark.applyTransform (mark.getTransformToScaleToFit (r, true));
        g.setColour (hover == HeaderHover::Github ? pal.text : pal.textDim);
        g.fillPath (mark);
    }

    g.setColour (pal.line);
    g.fillRect (margin, headerHeight - 1, getWidth() - 2 * margin, 1);
}

void HuldraView::paintOverChildren (juce::Graphics& g)
{
    if (! fileDragOver) return;
    const auto& pal = ui::Theme::get().palette();
    const bool dark = ui::Theme::get().isDark();
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    g.setColour (pal.background.withAlpha (0.78f));
    g.fillRoundedRectangle (r, 16.0f);
    ui::drawVeilGlow (g, r, dark, 2.5f);
    g.setColour (pal.accent);
    g.drawRoundedRectangle (r, 16.0f, 2.0f);
    g.setColour (pal.text);
    g.setFont (ui::font (26.0f));
    g.drawText ("Drop to load", r.withTrimmedBottom (40.0f), juce::Justification::centred);
    g.setColour (pal.textDim);
    ui::drawSpacedText (g, "ONE FILE REPLACES THE SOUND  " + ui::utf8 ("\xc2\xb7") + "  SEVERAL ARE SPREAD OVER THE KEYS",
                        r.withTrimmedTop (60.0f), 10.5f, juce::Justification::centred, 0.2f);
}

void HuldraView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    master.setBounds (header.removeFromRight (margin + 92).withTrimmedRight (margin).reduced (0, 6));
    presetBar.setBounds (header.withSizeKeepingCentre (440, 36).withX ((getWidth() - 440) / 2));
    auto tabs = r.removeFromTop (tabHeight);
    tabs.removeFromRight (margin);
    engine.setBounds (tabs.removeFromRight (260).withSizeKeepingCentre (260, 26).translated (0, -2));
    tabs.removeFromRight (10);
    playMode.setBounds (tabs.removeFromRight (140).withSizeKeepingCentre (140, 26).translated (0, -2));

    r.reduce (margin, 0);
    footer.setBounds (r.removeFromBottom (footerHeight + margin).withTrimmedBottom (margin));
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* pg : pages)
        pg->setBounds (r);
    browser.setBounds (getLocalBounds().reduced (margin, 0).withTrimmedTop (headerHeight + 14).withBottom (footer.getY() - 12));
    saveDialog.setBounds (getLocalBounds());
    about.setBounds (getLocalBounds());
    levelMeter.setBounds (logoBounds().getRight() + 14, (headerHeight - 30) / 2, 170, 30);
}

void HuldraView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            return showPage (i);

    if (themeToggleBounds().contains (e.getPosition()))
        ui::Theme::get().setDark (! ui::Theme::get().isDark());
    else if (githubBounds().contains (e.getPosition()))
        githubUrl.launchInDefaultBrowser();
    else if (undoBounds().contains (e.getPosition()))
        proc.undoManager.undo();
    else if (redoBounds().contains (e.getPosition()))
        proc.undoManager.redo();
    repaint (0, 0, getWidth(), headerHeight);
}

void HuldraView::mouseUp (const juce::MouseEvent& e)
{
    if (! logoBounds().contains (e.getPosition()) || e.getNumberOfClicks() > 1 || e.mouseWasDraggedSinceMouseDown())
        return;
    const int click = ++logoClicks;
    juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20,
                                 [safe = juce::Component::SafePointer<HuldraView> (this), click]
                                 {
                                     if (safe != nullptr && safe->logoClicks == click)
                                         safe->about.open();
                                 });
}

void HuldraView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (logoBounds().contains (e.getPosition()))
    {
        ++logoClicks;
        proc.requestPanic();
    }
}

void HuldraView::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int tab = -1;
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (pos))
            tab = i;

    auto h = HeaderHover::None;
    if (logoBounds().contains (pos))             h = HeaderHover::Logo;
    else if (themeToggleBounds().contains (pos)) h = HeaderHover::Theme;
    else if (githubBounds().contains (pos))      h = HeaderHover::Github;
    else if (undoBounds().contains (pos))        h = HeaderHover::Undo;
    else if (redoBounds().contains (pos))        h = HeaderHover::Redo;

    if (tab != hoveredTab || h != hover)
    {
        hoveredTab = tab;
        hover = h;
        setMouseCursor (tab >= 0 || h != HeaderHover::None ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (0, 0, getWidth(), headerHeight + tabHeight);
    }
}

void HuldraView::mouseExit (const juce::MouseEvent&)
{
    hoveredTab = -1;
    hover = HeaderHover::None;
    repaint (0, 0, getWidth(), headerHeight + tabHeight);
}

bool HuldraView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undoManager.undo(); repaint(); return true; }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { proc.undoManager.redo(); repaint(); return true; }
    return false;
}

bool HuldraView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f)) return true;
    return false;
}

void HuldraView::fileDragEnter (const juce::StringArray&, int, int) { fileDragOver = true; repaint(); }
void HuldraView::fileDragExit (const juce::StringArray&) { fileDragOver = false; repaint(); }

void HuldraView::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    juce::Array<juce::File> list;
    for (const auto& f : files)
        if (isAudioFile (f)) list.add (juce::File (f));
    if (! list.isEmpty())
        proc.instrument.loadFilesAsync (list, list.size() > 1 ? InstrumentState::AddMode::AddAutoMap : InstrumentState::AddMode::Replace);
    repaint();
}

// ---------------------------------------------------------------------------------------------
HuldraEditor::HuldraEditor (HuldraProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (HuldraView::baseWidth * 3 / 4, HuldraView::baseHeight * 3 / 4,
                     HuldraView::baseWidth * 3 / 2, HuldraView::baseHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) HuldraView::baseWidth / HuldraView::baseHeight);

    const auto& st = p.apvts.state;
    const float saved = st.hasProperty ("uiScale") ? (float) (double) st.getProperty ("uiScale") : ui::Theme::get().getScale();
    const float scale = juce::jlimit (0.75f, 1.5f, saved);
    setSize (juce::roundToInt (HuldraView::baseWidth * scale), juce::roundToInt (HuldraView::baseHeight * scale));
    ready = true;
}

void HuldraEditor::resized()
{
    const float scale = (float) getWidth() / (float) HuldraView::baseWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, HuldraView::baseWidth, HuldraView::baseHeight);

    if (ready && scale >= 0.74f && scale <= 1.51f)
    {
        ui::Theme::get().setScale (scale);
        proc.apvts.state.setProperty ("uiScale", scale, nullptr);
    }
}
