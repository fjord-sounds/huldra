#include "PresetManager.h"
#include "BinaryData.h"
#include "../fx/FxChain.h"

namespace huldra
{
    namespace
    {
        const juce::Identifier presetNameProp { "presetName" };
        const juce::Identifier presetCategoryProp { "presetCategory" };
        const juce::Identifier macroNamesProp { "macroNames" };

        int categoryRank (const juce::String& c)
        {
            const int i = PresetManager::categoryOrder.indexOf (c);
            return i < 0 ? PresetManager::categoryOrder.size() : i;
        }

        const juce::StringArray defaultMacroNames { "Tone", "Space", "Motion", "Texture", "Width", "Grit", "Air", "Wobble" };
    }

    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, InstrumentState& inst, juce::UndoManager* um)
        : state (s), instrument (inst), undo (um)
    {
        loadFavorites();
        rescan();
    }

    void PresetManager::loadFavorites()
    {
        favorites.clear();
        if (ignoreUserPresets) return;
        favorites.addLines (getUserPresetFolder().getChildFile ("favorites.txt").loadFileAsString());
        favorites.removeEmptyStrings();
    }

    void PresetManager::saveFavorites() const
    {
        auto folder = getUserPresetFolder();
        if (folder.createDirectory())
            folder.getChildFile ("favorites.txt").replaceWithText (favorites.joinIntoString ("\n"));
    }

    bool PresetManager::isFavorite (int index) const
    {
        return juce::isPositiveAndBelow (index, (int) presets.size())
            && favorites.contains (favoriteKey (presets[(size_t) index]));
    }

    void PresetManager::toggleFavorite (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;
        const auto key = favoriteKey (presets[(size_t) index]);
        if (favorites.contains (key)) favorites.removeString (key);
        else                          favorites.add (key);
        saveFavorites();
        sendChangeMessage();
    }

    bool PresetManager::deleteUserPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].isFactory)
            return false;
        const auto key = favoriteKey (presets[(size_t) index]);
        if (! presets[(size_t) index].file.moveToTrash())
            return false;
        if (favorites.contains (key))
        {
            favorites.removeString (key);
            saveFavorites();
        }
        rescan();
        return true;
    }

    juce::File PresetManager::getUserPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Fjord/Huldra/Presets");
    }

    bool PresetManager::parse (const juce::String& xmlText, Preset& out)
    {
        // Quick scan: read only the root element. The parameters are parsed on load.
        const int start = xmlText.indexOf ("<HuldraPreset");
        const int end = start >= 0 ? xmlText.indexOfChar (start, '>') : -1;
        if (start < 0 || end < 0)
            return false;

        auto header = xmlText.substring (start, end);
        if (header.endsWithChar ('/')) header = header.dropLastCharacters (1);
        auto xml = juce::parseXML (header + "/>");
        if (xml == nullptr || ! xml->hasTagName ("HuldraPreset"))
            return false;

        out.name = xml->getStringAttribute ("name", "Untitled");
        out.category = xml->getStringAttribute ("category", "User");
        out.author = xml->getStringAttribute ("author");
        out.description = xml->getStringAttribute ("description");
        out.tags = juce::StringArray::fromTokens (xml->getStringAttribute ("tags"), ",", "");
        out.tags.trim();
        out.tags.removeEmptyStrings();
        out.xml = xmlText;
        return true;
    }

    void PresetManager::rescan()
    {
        const auto currentName = getCurrentName();
        presets.clear();

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String file (BinaryData::originalFilenames[i]);
            if (! file.endsWithIgnoreCase (".huldra"))
                continue;
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            {
                Preset p;
                if (parse (juce::String::fromUTF8 (data, size), p))
                    presets.push_back (std::move (p));
            }
        }

        if (! ignoreUserPresets)
            for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, true, "*.huldra"))
            {
                Preset p;
                if (parse (f.loadFileAsString(), p))
                {
                    p.file = f;
                    p.isFactory = false;
                    presets.push_back (std::move (p));
                }
            }

        std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
        {
            if (a.isFactory != b.isFactory) return a.isFactory;
            const int ra = categoryRank (a.category), rb = categoryRank (b.category);
            if (ra != rb) return ra < rb;
            return a.name.compareNatural (b.name) < 0;
        });

        currentIndex = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == currentName)
                currentIndex = (int) i;

        sendChangeMessage();
    }

    juce::String PresetManager::getCurrentName() const
    {
        return state.state.getProperty (presetNameProp, "Init").toString();
    }

    juce::String PresetManager::getCurrentCategory() const
    {
        return state.state.getProperty (presetCategoryProp, "").toString();
    }

    void PresetManager::setCurrent (int index, const juce::String& name, const juce::String& category)
    {
        currentIndex = index;
        state.state.setProperty (presetNameProp, name, nullptr);
        state.state.setProperty (presetCategoryProp, category, nullptr);
        sendChangeMessage();
    }

    juce::StringArray PresetManager::getMacroNames() const
    {
        auto names = juce::StringArray::fromTokens (state.state.getProperty (macroNamesProp).toString(), "|", "");
        for (int i = 0; i < params::kNumMacros; ++i)
            if (names[i].trim().isEmpty())
            {
                while (names.size() <= i) names.add ({});
                names.set (i, defaultMacroNames[i]);
            }
        names.removeRange (params::kNumMacros, names.size());
        return names;
    }

    void PresetManager::setMacroName (int index, const juce::String& name)
    {
        auto names = getMacroNames();
        names.set (index, name.removeCharacters ("|").substring (0, 16));
        state.state.setProperty (macroNamesProp, names.joinIntoString ("|"), undo);
    }

    void PresetManager::applyXml (const juce::XmlElement& xml, bool forcePattern)
    {
        std::map<juce::String, float> values;
        for (auto* e : xml.getChildWithTagNameIterator ("Param"))
            values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

        if (undo != nullptr) undo->beginNewTransaction ("Load preset");

        for (auto* p : state.processor.getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (param == nullptr)
                continue;
            if (param->getParameterID() == params::paramId (params::MasterGain))
                continue;   // the master level belongs to the user, not to the preset
            const auto it = values.find (param->getParameterID());
            const float normalised = it != values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
            param->beginChangeGesture();
            param->setValueNotifyingHost (normalised);
            param->endChangeGesture();
        }

        // Keys, MSEGs, mask always; the pattern only when wanted
        const auto keepPattern = instrument.getPattern();
        instrument.applyTreatment (xml, undo);
        if (! forcePattern && ! getLoadPatterns())
            instrument.setPattern (keepPattern);

        state.state.setProperty (macroNamesProp, xml.getStringAttribute ("macros"), undo);
        state.state.setProperty ("fxOrder", fx::orderToString (fx::orderFromString (xml.getStringAttribute ("fxOrder"))), undo);
        instrument.rebuildNow();
    }

    std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name, const juce::String& category,
                                                                       const juce::StringArray& tags) const
    {
        auto xml = std::make_unique<juce::XmlElement> ("HuldraPreset");
        xml->setAttribute ("name", name);
        xml->setAttribute ("category", category);
        xml->setAttribute ("author", "User");
        if (! tags.isEmpty())
            xml->setAttribute ("tags", tags.joinIntoString (","));
        xml->setAttribute ("macros", state.state.getProperty (macroNamesProp).toString());
        xml->setAttribute ("fxOrder", state.state.getProperty ("fxOrder").toString());

        for (auto* p : state.processor.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml->createNewChildElement ("Param");
                e->setAttribute ("id", param->getParameterID());
                e->setAttribute ("value", param->convertFrom0to1 (param->getValue()));
            }
        instrument.writeTreatment (*xml);
        return xml;
    }

    void PresetManager::load (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;
        const auto& p = presets[(size_t) index];
        if (auto xml = juce::parseXML (p.xml))
        {
            applyXml (*xml);
            setCurrent (index, p.name, p.category);
        }
    }

    void PresetManager::loadInit()
    {
        applyXml (juce::XmlElement ("HuldraPreset"), true);
        setCurrent (-1, "Init", "");
    }

    void PresetManager::step (int delta)
    {
        if (presets.empty())
            return;
        const int n = (int) presets.size();
        const int start = currentIndex < 0 ? (delta > 0 ? -1 : 0) : currentIndex;
        load (((start + delta) % n + n) % n);
    }

    juce::StringArray PresetManager::getCurrentTags() const
    {
        return juce::isPositiveAndBelow (currentIndex, (int) presets.size()) ? presets[(size_t) currentIndex].tags
                                                                             : juce::StringArray();
    }

    bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category, const juce::StringArray& tags)
    {
        const auto cleanName = name.trim().isEmpty() ? juce::String ("Untitled") : name.trim();
        auto xml = createPresetXml (cleanName, category, tags);

        auto folder = getUserPresetFolder().getChildFile (category);
        if (! folder.createDirectory())
            return false;

        auto file = folder.getChildFile (juce::File::createLegalFileName (cleanName) + ".huldra");
        if (! xml->writeTo (file))
            return false;

        state.state.setProperty (presetNameProp, cleanName, nullptr);
        rescan();
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].file == file)
                setCurrent ((int) i, cleanName, category);
        return true;
    }

    void PresetManager::restoreFromState()
    {
        const auto name = getCurrentName();
        currentIndex = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                currentIndex = (int) i;
        sendChangeMessage();
    }
}
