#include "PluginEditor.h"
#include "presets/Locks.h"

using namespace huldra;

namespace
{
    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf;wave");
    }
}

ui::ViewSetup HuldraView::makeSetup (HuldraProcessor& p)
{
    ui::ViewSetup s { p.apvts, p.presetManager };
    s.undo = &p.undoManager;
    s.masterParamId = params::paramId (params::MasterGain);
    s.tabs = { "Source", "Slice", "Keymap", "Engine", "Spectral", "Keys", "Mod", "Motion", "FX" };
    s.tabWidth = 78;
    s.peaks = [&p] { return p.takeOutputPeaks(); };
    s.noteCounter = [&p] { return p.voiceDisplay.noteCounter.load(); };
    s.lastVelocity = [&p] { return p.voiceDisplay.lastVelocity.load(); };
    s.debugReport = [&p] { return p.getDebugReport(); };
    s.panic = [&p] { p.requestPanic(); };
    return s;
}

HuldraView::HuldraView (HuldraProcessor& p)
    : PluginView (makeSetup (p), width, height),
      proc (p),
      sourcePage (p), slicePage (p), keymapPage (p), enginePage (p), spectralPage (p), keysPage (p), modPage (p), motionPage (p),
      fxPage ({ p.apvts, &p.undoManager, p.fxMeters, [&p] { return p.getFxOrder(); }, [&p] (const fjord::base::fx::Order& o) { p.setFxOrder (o); }, &p.chainPresets }),
      footer (p),
      playMode (p.apvts, params::paramId (params::PlayMode), nullptr, { "Melodic", "Chops" }),
      engine (p.apvts, params::paramId (params::Engine), nullptr, { "Classic", "Granular", "Spectral", "Vocal" })
{
    // The padlock in the preset bar: parts of the treatment that stay when switching presets
    fjord::base::ui::LockMenu::attach (getPresetBar(), p.apvts, locks::set(), locks::menuLayout());
    addAndMakeVisible (footer);
    addAndMakeVisible (playMode);
    addAndMakeVisible (engine);
    playMode.setTooltip ("Melodic: the sound plays across the keyboard.  Chops: every key plays one slice.");
    engine.setTooltip ("The engine that plays the sample (zones and keys can choose their own)");
    setPages ({ &sourcePage, &slicePage, &keymapPage, &enginePage, &spectralPage, &keysPage, &modPage, &motionPage, &fxPage });
}

void HuldraView::tickContent (bool offscreen)
{
    footer.tick();
    juce::ignoreUnused (offscreen);
    if (const auto err = proc.instrument.getLastError(); err.isNotEmpty() && err != lastError)
    {
        lastError = err;
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Huldra", err);
    }
}

void HuldraView::browserVisibilityChanged (bool open)
{
    playMode.setVisible (! open);
    engine.setVisible (! open);
}

void HuldraView::layoutTabRow (juce::Rectangle<int> tabs)
{
    engine.setBounds (tabs.removeFromRight (260).withSizeKeepingCentre (260, 26).translated (0, -2));
    tabs.removeFromRight (10);
    playMode.setBounds (tabs.removeFromRight (140).withSizeKeepingCentre (140, 26).translated (0, -2));
}

juce::Rectangle<int> HuldraView::layoutContent (juce::Rectangle<int> r)
{
    footer.setBounds (r.removeFromBottom (footerHeight + margin).withTrimmedBottom (margin));
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* pg : std::initializer_list<juce::Component*> { &sourcePage, &slicePage, &keymapPage, &enginePage, &spectralPage,
                                                               &keysPage, &modPage, &motionPage, &fxPage })
        pg->setBounds (r);
    return r.withBottom (footer.getY() - 12);
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
