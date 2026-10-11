#pragma once
#include "PluginProcessor.h"
#include "ui/LookAndFeelHuldra.h"
#include "ui/SourcePage.h"
#include "ui/SlicePage.h"
#include "ui/KeymapPage.h"
#include "ui/EnginePage.h"
#include "ui/SpectralPage.h"
#include "ui/KeysPage.h"
#include "ui/ModPage.h"
#include "ui/MotionPage.h"
#include "ui/FxPage.h"
#include "ui/Footer.h"
#include "ui/PresetBar.h"
#include "ui/PresetBrowser.h"
#include "ui/Header.h"

// The actual UI at a fixed size (1200 x 800). The editor scales it as a whole.
class HuldraView : public juce::Component,
                   public juce::TooltipClient,
                   public juce::FileDragAndDropTarget,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    static constexpr int baseWidth = 1200, baseHeight = 800;

    explicit HuldraView (HuldraProcessor&);
    ~HuldraView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // For the screenshot tool
    static inline const juce::StringArray pageNames { "source", "slice", "keymap", "engine", "spectral", "keys", "mod", "motion", "fx" };
    void selectPage (int index) { showPage (juce::jlimit (0, (int) tabNames.size() - 1, index)); }
    void showBrowser (bool shouldShow) { setBrowserVisible (shouldShow); }
    void showAbout() { about.open(); }
    void showSaveDialog() { saveDialog.open(); }
    void tickAnimations();
    huldra::ui::FxPage& getFxPage() { return fxPage; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void applyTheme();
    void showPage (int);
    void setBrowserVisible (bool);
    huldra::ui::Page* currentPage() const;
    juce::Rectangle<int> tabBounds (int index) const;
    juce::Rectangle<int> themeToggleBounds() const;
    juce::Rectangle<int> githubBounds() const { return themeToggleBounds().translated (-36, 0); }
    juce::Rectangle<int> redoBounds() const { return githubBounds().translated (-44, 0); }
    juce::Rectangle<int> undoBounds() const { return redoBounds().translated (-30, 0); }
    juce::Rectangle<int> logoBounds() const { return { margin, 0, 104, headerHeight }; }

    HuldraProcessor& proc;
    huldra::ui::LookAndFeelHuldra lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    huldra::ui::SourcePage sourcePage;
    huldra::ui::SlicePage slicePage;
    huldra::ui::KeymapPage keymapPage;
    huldra::ui::EnginePage enginePage;
    huldra::ui::SpectralPage spectralPage;
    huldra::ui::KeysPage keysPage;
    huldra::ui::ModPage modPage;
    huldra::ui::MotionPage motionPage;
    huldra::ui::FxPage fxPage;
    std::array<huldra::ui::Page*, 9> pages;

    huldra::ui::Footer footer;
    huldra::ui::PresetBar presetBar;
    huldra::ui::Knob master;
    huldra::ui::Segmented playMode, engine;
    huldra::ui::PresetBrowser browser;
    huldra::ui::SaveDialog saveDialog;
    huldra::ui::LightningMeter levelMeter;
    huldra::ui::AboutPanel about;
    int logoClicks = 0;
    uint32_t lastNotes = 0;
    float logoFlash = 0.0f;
    bool fileDragOver = false;
    juce::String lastError;

    int page = 0;
    int hoveredTab = -1;
    enum class HeaderHover { None, Logo, Theme, Github, Undo, Redo } hover = HeaderHover::None;

    static inline const juce::StringArray tabNames { "Source", "Slice", "Keymap", "Engine", "Spectral", "Keys", "Mod", "Motion", "FX" };
    static constexpr int headerHeight = 64, tabHeight = 40, footerHeight = 92, margin = 16, tabWidth = 78;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HuldraView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class HuldraEditor : public juce::AudioProcessorEditor
{
public:
    explicit HuldraEditor (HuldraProcessor&);
    void resized() override;
    HuldraView& getView() { return view; }

private:
    HuldraProcessor& proc;
    HuldraView view;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HuldraEditor)
};
