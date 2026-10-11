#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace huldra::motion
{
    constexpr int kMaxSteps = 32;

    // One step of the motion pattern. In Melodic mode 'note' transposes the held key (semitones);
    // in Chops mode it picks a slice relative to the held key.
    struct Step
    {
        bool on = false;
        int note = 0;              // -24 .. +24
        float velocity = 0.8f;     // 0..1
        float gate = 0.8f;         // 0.05 .. 1 of the step (ties: > 1)
        float chance = 1.0f;       // 0..1
        int ratchet = 1;           // 1..4 repeats inside the step
        float lane = 0.0f;         // mod lane value 0..1 (source "Motion Lane")
        bool tie = false;          // hold into the next step (legato)

        bool operator== (const Step& o) const
        {
            return on == o.on && note == o.note && juce::exactlyEqual (velocity, o.velocity) && juce::exactlyEqual (gate, o.gate)
                && juce::exactlyEqual (chance, o.chance) && ratchet == o.ratchet && juce::exactlyEqual (lane, o.lane) && tie == o.tie;
        }
    };

    struct Pattern
    {
        std::array<Step, kMaxSteps> steps {};

        bool operator== (const Pattern& o) const { return steps == o.steps; }

        // "on:note:vel:gate:chance:ratchet:lane:tie" per step, space separated
        juce::String toString() const
        {
            juce::StringArray parts;
            for (const auto& s : steps)
                parts.add (juce::String (s.on ? 1 : 0) + ":" + juce::String (s.note) + ":" + juce::String (s.velocity, 3) + ":"
                           + juce::String (s.gate, 3) + ":" + juce::String (s.chance, 3) + ":" + juce::String (s.ratchet) + ":"
                           + juce::String (s.lane, 3) + ":" + juce::String (s.tie ? 1 : 0));
            return parts.joinIntoString (" ");
        }

        static Pattern fromString (const juce::String& text)
        {
            Pattern p = defaultPattern();
            const auto tokens = juce::StringArray::fromTokens (text, " ", "");
            if (tokens.isEmpty()) return p;
            p = {};
            for (int i = 0; i < std::min (kMaxSteps, tokens.size()); ++i)
            {
                const auto v = juce::StringArray::fromTokens (tokens[i], ":", "");
                auto& s = p.steps[(size_t) i];
                s.on = v[0].getIntValue() != 0;
                s.note = std::clamp (v[1].getIntValue(), -24, 24);
                s.velocity = std::clamp (v.size() > 2 ? v[2].getFloatValue() : 0.8f, 0.0f, 1.0f);
                s.gate = std::clamp (v.size() > 3 ? v[3].getFloatValue() : 0.8f, 0.05f, 1.0f);
                s.chance = std::clamp (v.size() > 4 ? v[4].getFloatValue() : 1.0f, 0.0f, 1.0f);
                s.ratchet = std::clamp (v.size() > 5 ? v[5].getIntValue() : 1, 1, 4);
                s.lane = std::clamp (v.size() > 6 ? v[6].getFloatValue() : 0.0f, 0.0f, 1.0f);
                s.tie = v.size() > 7 && v[7].getIntValue() != 0;
            }
            return p;
        }

        // A gentle default: root, fifth, octave, a lane that rises
        static Pattern defaultPattern()
        {
            Pattern p;
            const int notes[] = { 0, 0, 7, 0, 12, 0, 7, 10, 0, 0, 7, 0, 12, 14, 7, 5 };
            for (int i = 0; i < 16; ++i)
            {
                auto& s = p.steps[(size_t) i];
                s.on = i % 4 != 3;
                s.note = notes[i];
                s.velocity = i % 4 == 0 ? 0.95f : 0.7f;
                s.gate = 0.7f;
                s.lane = (float) i / 15.0f;
            }
            return p;
        }
    };
}
