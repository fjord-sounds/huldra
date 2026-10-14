#pragma once
#include <fjord_base/presets/Locks.h>
#include <fjord_base/ui/LockMenu.h>
#include "../Parameters.h"

namespace huldra::locks
{
    // Parts of a treatment that stay when another sound preset is loaded: an engine, the voice, the filter, an envelope, an
    // LFO, the matrix, the motion settings, the pattern, the effect chain … The mechanism (LockSet, padlock menu,
    // LockButton, preset manager hook) is fjord-base; this is Huldra's list. The switches are properties of the project
    // state – their names never change. Key edits, MSEGs, the pattern and the spectral mask are not parameters:
    // PresetManager::applyExtras asks isLockedProperty().
    struct Ids
    {
        int engine, classic, granular, spectral, vocal, chops, keys;
        int voice, filter, amp, filterEnv, matrix, macros, pad;
        int motion, generative, arp, scale, pattern;
        std::array<int, params::kNumLfos> lfos;
        std::array<int, params::kNumMsegs> msegs;
    };

    inline juce::String msegProperty (int mseg) { return "msegLock_" + juce::String (mseg + 1); }

    inline const std::pair<const fjord::base::locks::LockSet&, Ids>& setup()
    {
        static const auto pair = []
        {
            static fjord::base::locks::LockSet set;
            Ids i {};
            i.engine   = set.add ({ "engineLock",   "Engine and play mode", "engine|play_mode" });
            i.classic  = set.add ({ "classicLock",  "Sample playback (start, end, loop)", "smp_" });
            i.granular = set.add ({ "granularLock", "Granular engine", "grn_" });
            i.spectral = set.add ({ "spectralLock", "Spectral engine (incl. mask)", "spc_" });
            i.vocal    = set.add ({ "vocalLock",    "Vocal engine", "voc_" });
            i.chops    = set.add ({ "chopLock",     "Chops", "chop_" });
            i.keys     = set.add ({ "keysLock",     "Key edits", {} });
            i.voice    = set.add ({ "voiceLock",    "Voice (tuning, unison, glide)",
                                    "voice_mode|polyphony|glide|bend_range|key_track|tune|fine|drive|uni_" });
            i.filter    = set.add ({ "filterLock",    "Filter", "flt_" });
            i.amp       = set.add ({ "ampEnvLock",    "Amp (envelope, level, pan)", "amp_|level|pan|vel_amp" });
            i.filterEnv = set.add ({ "filterEnvLock", "Filter envelope", "fenv_" });
            for (int l = 0; l < params::kNumLfos; ++l)
                i.lfos[(size_t) l] = set.add ({ "lfoLock_" + juce::String (l + 1), "LFO " + juce::String (l + 1), "lfo" + juce::String (l + 1) + "_" });
            for (int m = 0; m < params::kNumMsegs; ++m)
                i.msegs[(size_t) m] = set.add ({ msegProperty (m), "MSEG " + juce::String (m + 1), "mseg" + juce::String (m + 1) + "_" });
            juce::String slots;
            for (int s = 1; s <= 16; ++s) slots += (s > 1 ? "|mod" : "mod") + juce::String (s) + "_";
            i.matrix = set.add ({ "matrixLock", "Modulation matrix", slots });
            i.macros = set.add ({ "macroLock",  "Macros", "macro" });
            i.pad    = set.add ({ "padLock",    "Morph pad", "pad_" });
            i.motion     = set.add ({ "motionLock",     "Motion (mode, rate, gate)", "mot_" });
            i.generative = set.add ({ "generativeLock", "Generative", "gen_" });
            i.arp        = set.add ({ "arpLock",        "Arpeggiator", "arp_" });
            i.scale      = set.add ({ "scaleLock",      "Scale, chords and retune", "scl_|chord_|retune_" });
            i.pattern    = set.add ({ "patternLock",    "Pattern", "pat_" });
            set.addEffectGroups();
            return std::pair<const fjord::base::locks::LockSet&, Ids> { set, i };
        }();
        return pair;
    }

    inline const fjord::base::locks::LockSet& set() { return setup().first; }
    inline const Ids& ids() { return setup().second; }

    inline fjord::base::ui::MenuLayout menuLayout()
    {
        const auto& i = ids();
        std::vector<int> envelopes { i.amp, i.filterEnv };
        for (int m : i.msegs) envelopes.push_back (m);
        std::vector<int> modulation (i.lfos.begin(), i.lfos.end());
        modulation.insert (modulation.end(), { i.matrix, i.macros, i.pad });
        fjord::base::ui::MenuLayout layout;
        layout.header ("Keep when switching presets")
              .item (i.engine)
              .submenu ("Engines", { i.classic, i.granular, i.spectral, i.vocal }, "All engines")
              .item (i.chops)
              .item (i.keys)
              .separator()
              .item (i.voice)
              .item (i.filter)
              .submenu ("Envelopes", envelopes, "All envelopes")
              .submenu ("Modulation", modulation)
              .submenu ("Motion", { i.motion, i.generative, i.arp, i.scale, i.pattern }, "All motion")
              .separator()
              .effects (set());
        return layout;
    }
}
