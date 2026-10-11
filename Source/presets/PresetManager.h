#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../state/InstrumentState.h"

namespace huldra
{
    // Presets are XML files (.huldra) with plain values and the "treatment" (per-key edits, MSEGs, pattern, mask):
    //   <HuldraPreset name="Paper Moon" category="Bedroom Pop" author="Fjord" tags="Pad,Airy" macros="..." fxOrder="...">
    //     <Param id="engine" value="1"/> … <Keys/> <Msegs/> <Pattern/> <Mask/>
    // A preset never contains audio: it is applied to whatever the user has loaded or recorded (or to a demo voice).
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    class PresetManager : public juce::ChangeBroadcaster
    {
    public:
        struct Preset
        {
            juce::String name, category, author, description;
            juce::StringArray tags;
            juce::String xml;          // file contents
            juce::File file;           // empty for factory presets
            bool isFactory = true;
        };

        // Tag vocabulary in display order (must match the tags used by the factory presets)
        static inline const juce::StringArray tagOrder {
            "Pad", "Keys", "Pluck", "Lead", "Bass", "Chops", "Choir", "Texture", "Airy", "Warm", "Dark", "Bright",
            "Intimate", "Wide", "Lo-Fi", "Evolving", "Rhythmic", "Autoplay", "Arp", "Granular", "Spectral", "Vocal",
            "Glitch", "Cinematic" };

        static inline const juce::StringArray categoryOrder {
            "Pads", "Keys", "Plucks", "Leads & Bass", "Choirs", "Voices", "Vocal Chops", "Textures", "Motion", "Risers & FX",
            "Experimental", "User" };

        // Screenshots: show only the factory presets, never the user's own presets or favourites.
        static inline bool ignoreUserPresets = false;

        PresetManager (juce::AudioProcessorValueTreeState&, InstrumentState&, juce::UndoManager* = nullptr);

        void rescan();
        const std::vector<Preset>& getPresets() const { return presets; }

        int getCurrentIndex() const { return currentIndex; }
        juce::String getCurrentName() const;
        juce::String getCurrentCategory() const;

        void load (int index);
        void loadInit();
        void next()     { step (1); }
        void previous() { step (-1); }

        bool saveUserPreset (const juce::String& name, const juce::String& category, const juce::StringArray& tags = {});
        juce::StringArray getCurrentTags() const;
        static juce::File getUserPresetFolder();

        bool isFavorite (int index) const;
        void toggleFavorite (int index);
        bool deleteUserPreset (int index);

        // When off, loading a preset keeps the current motion pattern
        bool getLoadPatterns() const { return state.state.getProperty ("loadPatterns", true); }
        void setLoadPatterns (bool shouldLoad) { state.state.setProperty ("loadPatterns", shouldLoad, nullptr); }

        // When on, a preset also switches the demo voice (only matters while no own sample is loaded)
        std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name, const juce::String& category,
                                                           const juce::StringArray& tags = {}) const;
        void applyXml (const juce::XmlElement&, bool forcePattern = false);
        void restoreFromState();

        juce::StringArray getMacroNames() const;
        void setMacroName (int index, const juce::String& name);

    private:
        void step (int delta);
        void setCurrent (int index, const juce::String& name, const juce::String& category);
        static bool parse (const juce::String& xmlText, Preset& out);
        static juce::String favoriteKey (const Preset& p) { return (p.isFactory ? "factory/" : "user/") + p.category + "/" + p.name; }
        void loadFavorites();
        void saveFavorites() const;

        juce::AudioProcessorValueTreeState& state;
        InstrumentState& instrument;
        juce::UndoManager* undo = nullptr;
        std::vector<Preset> presets;
        int currentIndex = -1;
        juce::StringArray favorites;
    };
}
