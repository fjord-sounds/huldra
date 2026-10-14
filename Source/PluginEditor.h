#pragma once
#include "PluginProcessor.h"
#include "ui/SourcePage.h"
#include "ui/SlicePage.h"
#include "ui/KeymapPage.h"
#include "ui/EnginePage.h"
#include "ui/SpectralPage.h"
#include "ui/KeysPage.h"
#include "ui/ModPage.h"
#include "ui/MotionPage.h"
#include "ui/Footer.h"
#include <fjord_base/fx/FxPage.h>

// The actual UI at a fixed size (1200 x 800) inside the shared frame of fjord-base (header, presets, About).
// The editor scales it as a whole.
class HuldraView : public huldra::ui::PluginView,
                   public juce::FileDragAndDropTarget
{
public:
    static constexpr int width = 1200, height = 800;

    explicit HuldraView (HuldraProcessor&);

    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // For the screenshot tool
    static inline const juce::StringArray pageNames { "source", "slice", "keymap", "engine", "spectral", "keys", "mod", "motion", "fx" };
    void selectPage (int index) { showPage (index); }
    fjord::base::fx::FxPage& getFxPage() { return fxPage; }

protected:
    juce::Rectangle<int> layoutContent (juce::Rectangle<int>) override;
    void layoutTabRow (juce::Rectangle<int>) override;
    void browserVisibilityChanged (bool open) override;
    void tickContent (bool offscreen) override;

private:
    static huldra::ui::ViewSetup makeSetup (HuldraProcessor&);

    HuldraProcessor& proc;
    huldra::ui::SourcePage sourcePage;
    huldra::ui::SlicePage slicePage;
    huldra::ui::KeymapPage keymapPage;
    huldra::ui::EnginePage enginePage;
    huldra::ui::SpectralPage spectralPage;
    huldra::ui::KeysPage keysPage;
    huldra::ui::ModPage modPage;
    huldra::ui::MotionPage motionPage;
    fjord::base::fx::FxPage fxPage;

    huldra::ui::Footer footer;
    huldra::ui::Segmented playMode, engine;
    bool fileDragOver = false;
    juce::String lastError;

    static constexpr int footerHeight = 92;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HuldraView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class HuldraEditor : public huldra::ui::ScaledEditor<HuldraView, HuldraProcessor>
{
public:
    using ScaledEditor::ScaledEditor;
};
