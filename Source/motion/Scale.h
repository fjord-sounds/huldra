#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace huldra::motion
{
    // Scale types in the order of the "scl_type" choice list.
    struct ScaleDef { int size; std::array<int, 12> steps; };

    inline const ScaleDef& scaleDef (int type)
    {
        static const ScaleDef defs[] = {
            { 7, { 0, 2, 4, 5, 7, 9, 11 } },       // Major
            { 7, { 0, 2, 3, 5, 7, 8, 10 } },       // Minor
            { 7, { 0, 2, 3, 5, 7, 9, 10 } },       // Dorian
            { 7, { 0, 1, 3, 5, 7, 8, 10 } },       // Phrygian
            { 7, { 0, 2, 4, 6, 7, 9, 11 } },       // Lydian
            { 7, { 0, 2, 4, 5, 7, 9, 10 } },       // Mixolydian
            { 7, { 0, 2, 3, 5, 7, 8, 11 } },       // Harmonic minor
            { 5, { 0, 2, 4, 7, 9 } },              // Major pentatonic
            { 5, { 0, 3, 5, 7, 10 } },             // Minor pentatonic
            { 6, { 0, 3, 5, 6, 7, 10 } },          // Blues
            { 6, { 0, 2, 4, 6, 8, 10 } },          // Whole tone
            { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } };   // Chromatic
        return defs[std::clamp (type, 0, 11)];
    }

    inline bool inScale (int note, int root, int type)
    {
        const auto& d = scaleDef (type);
        const int pc = ((note - root) % 12 + 12) % 12;
        for (int i = 0; i < d.size; ++i)
            if (d.steps[(size_t) i] == pc) return true;
        return false;
    }

    // Nearest scale note to a (fractional) MIDI pitch.
    inline float snapToScale (float pitch, int root, int type)
    {
        const int base = (int) std::floor (pitch);
        float best = (float) base, bestDist = 100.0f;
        for (int n = base - 2; n <= base + 3; ++n)
            if (inScale (n, root, type))
            {
                const float d = std::abs ((float) n - pitch);
                if (d < bestDist) { bestDist = d; best = (float) n; }
            }
        return best;
    }

    // Integer note snapped into the scale (ties go down).
    inline int snapNote (int note, int root, int type)
    {
        if (inScale (note, root, type)) return note;
        for (int d = 1; d < 6; ++d)
        {
            if (inScale (note - d, root, type)) return note - d;
            if (inScale (note + d, root, type)) return note + d;
        }
        return note;
    }

    // Moves 'steps' scale degrees from a note that is in the scale.
    inline int scaleStep (int note, int steps, int root, int type)
    {
        int n = snapNote (note, root, type);
        const int dir = steps >= 0 ? 1 : -1;
        for (int s = 0; s != steps; s += dir)
        {
            do { n += dir; } while (! inScale (n, root, type) && std::abs (n) < 200);
        }
        return n;
    }

    // Chord intervals for the "chord_type" list (index 0 = off). The last two follow the scale.
    struct Chord { int size; std::array<int, 6> notes; };

    inline Chord chordFor (int type, int note, int root, int scaleType)
    {
        switch (type)
        {
            case 1:  return { 3, { 0, 4, 7 } };
            case 2:  return { 3, { 0, 3, 7 } };
            case 3:  return { 3, { 0, 2, 7 } };
            case 4:  return { 3, { 0, 5, 7 } };
            case 5:  return { 4, { 0, 4, 7, 11 } };
            case 6:  return { 4, { 0, 3, 7, 10 } };
            case 7:  return { 4, { 0, 4, 7, 10 } };
            case 8:  return { 4, { 0, 4, 7, 14 } };
            case 9:  return { 5, { 0, 3, 7, 10, 14 } };
            case 10: return { 2, { 0, 7 } };
            case 11: return { 3, { 0, 12, 24 } };
            case 12: return { 4, { 0, 7, 14, 21 } };
            case 13:
            case 14:
            {
                const int count = type == 13 ? 3 : 4;
                Chord c { count, {} };
                const int n0 = snapNote (note, root, scaleType);
                for (int i = 0; i < count; ++i)
                    c.notes[(size_t) i] = scaleStep (n0, i * 2, root, scaleType) - note;
                return c;
            }
            default: return { 1, { 0 } };
        }
    }

    // Applies inversion (-3..3) and spread (0..1: every other note an octave up) to chord notes.
    inline void voiceChord (Chord& c, int inversion, float spread)
    {
        std::sort (c.notes.begin(), c.notes.begin() + c.size);
        for (int i = 0; i < std::abs (inversion); ++i)
        {
            if (inversion > 0) { c.notes[(size_t) (i % c.size)] += 12; }
            else               { c.notes[(size_t) (c.size - 1 - i % c.size)] -= 12; }
        }
        if (spread > 0.33f)
            for (int i = 1; i < c.size; i += 2) c.notes[(size_t) i] += 12;
        if (spread > 0.75f && c.size > 2) c.notes[0] -= 12;
        std::sort (c.notes.begin(), c.notes.begin() + c.size);
    }
}
