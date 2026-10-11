#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace huldra::params
{
    // Every parameter is listed once here. The columns are:
    //   enum name, parameter ID, display name, kind (F float, I int, C choice, B bool), min, max, default,
    //   curve (0 linear, -1 logarithmic, > 0 skewed so that this value sits in the middle), value format, choices
    //
    // Rules that keep projects and presets working:
    //   - IDs never change. New parameters go to the END of the list (the order defines mod-target indices).
    //   - Choice lists are append-only.
    //   - New parameters need neutral defaults (presets without them fall back to the default).
    //   - Everything between VoiceFirst and VoiceLast is evaluated per voice (per-note modulation).

    #define HULDRA_LFO_PARAMS(X, n)                                                                                  \
        X (Lfo##n##Shape,  "lfo" #n "_shape",  "LFO " #n " Shape",  C, 0, 6, 0,      0, None, "Sine|Triangle|Saw Up|Saw Down|Square|Sample & Hold|Smooth Random") \
        X (Lfo##n##Rate,   "lfo" #n "_rate",   "LFO " #n " Rate",   F, 0.01f, 40, 1, -1, Hz, "")                  \
        X (Lfo##n##Sync,   "lfo" #n "_sync",   "LFO " #n " Sync",   B, 0, 1, 0,      0, None, "")                  \
        X (Lfo##n##Beats,  "lfo" #n "_beats",  "LFO " #n " Note",   C, 0, 15, 5,     0, None, "8 Bars|4 Bars|2 Bars|1 Bar|1/2|1/4|1/8|1/16|1/32|1/2T|1/4T|1/8T|1/16T|1/2D|1/4D|1/8D") \
        X (Lfo##n##Retrig, "lfo" #n "_retrig", "LFO " #n " Retrigger", B, 0, 1, 0,   0, None, "")                  \
        X (Lfo##n##Fade,   "lfo" #n "_fade",   "LFO " #n " Fade In", F, 0, 8, 0,     1, Time, "")

    #define HULDRA_MSEG_PARAMS(X, n)                                                                                 \
        X (Mseg##n##Time, "mseg" #n "_time", "MSEG " #n " Length", F, 0.05f, 30, 2, 2, Time, "")                    \
        X (Mseg##n##Loop, "mseg" #n "_loop", "MSEG " #n " Loop",   B, 0, 1, 0,     0, None, "")

    #define HULDRA_MOD_SLOT(X, n)                                                                                    \
        X (Mod##n##Src, "mod" #n "_src", "Mod " #n " Source", C, 0, 27, 0, 0, None,                                  \
           "None|Velocity|Key|Random|Pressure|Slide|Mod Wheel|Pitch Bend|Amp Env|Filter Env|LFO 1|LFO 2|LFO 3|LFO 4|" \
           "MSEG 1|MSEG 2|MSEG 3|Macro 1|Macro 2|Macro 3|Macro 4|Macro 5|Macro 6|Macro 7|Macro 8|Motion Lane|Pad X|Pad Y") \
        X (Mod##n##Dst, "mod" #n "_dst", "Mod " #n " Target", C, 0, 0, 0, 0, None, "@targets")                     \
        X (Mod##n##Amt, "mod" #n "_amt", "Mod " #n " Amount", F, -1, 1, 0, 0, Bip, "")

    #define HULDRA_PARAMS(X)                                                                                         \
        /* Global */                                                                                                 \
        X (MasterGain,  "master_gain",  "Master",            F, -36, 6, 0,       0, Db, "")                          \
        X (PresetLevel, "preset_level", "Preset Level",      F, -24, 12, 0,      0, Db, "")                          \
        X (PlayMode,    "play_mode",    "Play Mode",         C, 0, 1, 0,         0, None, "Melodic|Chops")           \
        X (Engine,      "engine",       "Engine",            C, 0, 3, 0,         0, None, "Classic|Granular|Spectral|Vocal") \
        X (DemoVoice,   "demo_voice",   "Demo Voice",        C, 0, 7, 0,         0, None, "Aah|Ooh|Hum|Breath|Choir|Glass|Bowed|Phrase") \
        X (Polyphony,   "polyphony",    "Voices",            I, 1, 32, 16,       0, Plain, "")                       \
        X (VoiceMode,   "voice_mode",   "Voice Mode",        C, 0, 2, 0,         0, None, "Poly|Mono|Legato")        \
        X (Glide,       "glide",        "Glide",             F, 0, 2, 0,         0.2f, Time, "")                     \
        X (BendRange,   "bend_range",   "Bend Range",        I, 0, 24, 2,        0, Semi, "")                        \
        /* ---- per voice from here ---- */                                                                          \
        X (Tune,        "tune",         "Tune",              F, -24, 24, 0,      0, Semi, "")                        \
        X (Fine,        "fine",         "Fine",              F, -100, 100, 0,    0, Cents, "")                       \
        X (Level,       "level",        "Level",             F, 0, 1, 0.6f,      0, Pct, "")                         \
        X (Pan,         "pan",          "Pan",               F, -1, 1, 0,        0, Balance, "")                         \
        X (VelAmp,      "vel_amp",      "Velocity > Level",  F, 0, 1, 0.6f,      0, Pct, "")                         \
        X (KeyTrack,    "key_track",    "Key Tracking",      F, 0, 1, 1,         0, Pct, "")                         \
        /* Sample playback (all engines) */                                                                          \
        X (SmpStart,    "smp_start",    "Start",             F, 0, 1, 0,         0, Pct, "")                         \
        X (SmpEnd,      "smp_end",      "End",               F, 0, 1, 1,         0, Pct, "")                         \
        X (SmpStartRand,"smp_start_rand","Start Random",     F, 0, 1, 0,         0, Pct, "")                         \
        X (SmpReverse,  "smp_reverse",  "Reverse",           B, 0, 1, 0,         0, None, "")                        \
        X (SmpLoopMode, "smp_loop",     "Loop",              C, 0, 2, 0,         0, None, "Off|Forward|Ping-Pong")   \
        X (SmpLoopStart,"smp_loop_start","Loop Start",       F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (SmpLoopLen,  "smp_loop_len", "Loop Length",       F, 0.002f, 1, 0.4f, 0.2f, Pct, "")                      \
        X (SmpXfade,    "smp_xfade",    "Loop Crossfade",    F, 0, 1, 0.3f,      0, Pct, "")                         \
        /* Granular engine */                                                                                        \
        X (GrnPos,      "grn_pos",      "Grain Position",    F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (GrnScan,     "grn_scan",     "Grain Scan",        F, -2, 2, 0,        0, Speed, "")                       \
        X (GrnSpray,    "grn_spray",    "Grain Spray",       F, 0, 1, 0.08f,     0, Pct, "")                         \
        X (GrnSize,     "grn_size",     "Grain Size",        F, 5, 1000, 90,     90, Ms, "")                         \
        X (GrnDensity,  "grn_density",  "Grain Density",     F, 1, 200, 24,      24, Rate, "")                       \
        X (GrnPitchRand,"grn_pitch_rand","Grain Detune",     F, 0, 24, 0,        3, Semi, "")                        \
        X (GrnOctaves,  "grn_octaves",  "Grain Octaves",     F, 0, 1, 0,         0, Pct, "")                         \
        X (GrnPan,      "grn_pan",      "Grain Spread",      F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (GrnWindow,   "grn_window",   "Grain Shape",       C, 0, 4, 0,         0, None, "Soft|Gauss|Flat|Swell|Pluck") \
        X (GrnReverse,  "grn_reverse",  "Grain Reverse",     F, 0, 1, 0,         0, Pct, "")                         \
        X (GrnFreeze,   "grn_freeze",   "Grain Freeze",      B, 0, 1, 0,         0, None, "")                        \
        X (GrnJitter,   "grn_jitter",   "Grain Jitter",      F, 0, 1, 0,         0, Pct, "")                         \
        /* Spectral engine */                                                                                        \
        X (SpcPos,      "spc_pos",      "Spectral Position", F, 0, 1, 0,         0, Pct, "")                         \
        X (SpcScan,     "spc_scan",     "Spectral Speed",    F, -2, 2, 1,        0, Speed, "")                       \
        X (SpcFreeze,   "spc_freeze",   "Spectral Freeze",   B, 0, 1, 0,         0, None, "")                        \
        X (SpcBlur,     "spc_blur",     "Blur",              F, 0, 1, 0,         0, Pct, "")                         \
        X (SpcSmear,    "spc_smear",    "Smear",             F, 0, 1, 0,         0, Pct, "")                         \
        X (SpcShift,    "spc_shift",    "Frequency Shift",   F, -1000, 1000, 0,  0, Hz, "")                          \
        X (SpcStretch,  "spc_stretch",  "Partial Stretch",   F, 0.5f, 2, 1,      1, Mult, "")                        \
        X (SpcTilt,     "spc_tilt",     "Spectral Tilt",     F, -1, 1, 0,        0, Bip, "")                         \
        X (SpcHarm,     "spc_harm",     "Harmonize",         F, 0, 1, 0,         0, Pct, "")                         \
        X (SpcGate,     "spc_gate",     "Spectral Gate",     F, 0, 1, 0,         0, Pct, "")                         \
        X (SpcMask,     "spc_mask",     "Mask Amount",       F, 0, 1, 1,         0, Pct, "")                         \
        /* Vocal engine: pitch and time apart, formants kept */                                                      \
        X (VocSpeed,    "voc_speed",    "Speed",             F, 0, 4, 1,         1, Mult, "")                        \
        X (VocFormant,  "voc_formant",  "Formant",           F, -24, 24, 0,      0, Semi, "")                        \
        X (VocKeep,     "voc_keep",     "Keep Formants",     B, 0, 1, 1,         0, None, "")                        \
        X (VocTonal,    "voc_tonal",    "Tonality",          F, 0, 1, 0,         0, Pct, "")                         \
        /* Voice filter */                                                                                           \
        X (FltOn,       "flt_on",       "Filter On",         B, 0, 1, 0,         0, None, "")                        \
        X (FltType,     "flt_type",     "Filter Type",       C, 0, 7, 0,         0, None, "Low Pass 12|Low Pass 24|High Pass|Band Pass|Notch|Vowel|Comb|Ladder") \
        X (FltCutoff,   "flt_cutoff",   "Cutoff",            F, 20, 20000, 6000, -1, Hz, "")                         \
        X (FltReso,     "flt_reso",     "Resonance",         F, 0, 1, 0.15f,     0, Pct, "")                         \
        X (FltDrive,    "flt_drive",    "Filter Drive",      F, 0, 1, 0,         0, Pct, "")                         \
        X (FltKey,      "flt_key",      "Filter Keytrack",   F, 0, 1, 0,         0, Pct, "")                         \
        X (FltEnv,      "flt_env",      "Filter Env Amount", F, -6, 6, 0,        0, Oct, "")                         \
        X (FltVel,      "flt_vel",      "Velocity > Cutoff", F, 0, 1, 0,         0, Pct, "")                         \
        X (FltVowel,    "flt_vowel",    "Vowel",             F, 0, 4, 0,         0, Vowel, "")                       \
        /* Envelopes */                                                                                              \
        X (AmpAttack,   "amp_attack",   "Attack",            F, 0.0005f, 10, 0.004f, 0.3f, Time, "")                 \
        X (AmpDecay,    "amp_decay",    "Decay",             F, 0.005f, 20, 1,   1, Time, "")                        \
        X (AmpSustain,  "amp_sustain",  "Sustain",           F, 0, 1, 1,         0, Pct, "")                         \
        X (AmpRelease,  "amp_release",  "Release",           F, 0.005f, 20, 0.25f, 1, Time, "")                      \
        X (FenvAttack,  "fenv_attack",  "Filter Attack",     F, 0.0005f, 10, 0.004f, 0.3f, Time, "")                 \
        X (FenvDecay,   "fenv_decay",   "Filter Decay",      F, 0.005f, 20, 0.5f, 1, Time, "")                       \
        X (FenvSustain, "fenv_sustain", "Filter Sustain",    F, 0, 1, 0,         0, Pct, "")                         \
        X (FenvRelease, "fenv_release", "Filter Release",    F, 0.005f, 20, 0.3f, 1, Time, "")                       \
        /* Character */                                                                                              \
        X (Drive,       "drive",        "Drive",             F, 0, 1, 0,         0, Pct, "")                         \
        X (UniVoices,   "uni_voices",   "Unison",            I, 1, 4, 1,         0, Plain, "")                       \
        X (UniDetune,   "uni_detune",   "Unison Detune",     F, 0, 100, 15,      0, Cents, "")                       \
        X (UniSpread,   "uni_spread",   "Unison Spread",     F, 0, 1, 0.7f,      0, Pct, "")                         \
        /* ---- per voice until here ---- */                                                                         \
        /* Scale, retune, chords */                                                                                  \
        X (SclOn,       "scl_on",       "Scale Snap",        B, 0, 1, 0,         0, None, "")                        \
        X (SclRoot,     "scl_root",     "Key",               C, 0, 11, 0,        0, None, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B") \
        X (SclType,     "scl_type",     "Scale",             C, 0, 11, 0,        0, None, "Major|Minor|Dorian|Phrygian|Lydian|Mixolydian|Harmonic Minor|Major Pentatonic|Minor Pentatonic|Blues|Whole Tone|Chromatic") \
        X (RetuneAmt,   "retune_amt",   "Retune",            F, 0, 1, 0,         0, Pct, "")                         \
        X (RetuneSpeed, "retune_speed", "Retune Speed",      F, 0, 1, 0.6f,      0, Pct, "")                         \
        X (ChordType,   "chord_type",   "Chord",             C, 0, 14, 0,        0, None, "Off|Major|Minor|Sus2|Sus4|Major 7|Minor 7|Dominant 7|Add 9|Minor 9|Power|Octaves|Stacked Fifths|Scale Triad|Scale Seventh") \
        X (ChordSpread, "chord_spread", "Voicing Spread",    F, 0, 1, 0,         0, Pct, "")                         \
        X (ChordStrum,  "chord_strum",  "Strum",             F, 0, 250, 0,       40, Ms, "")                         \
        X (ChordInv,    "chord_inv",    "Inversion",         I, -3, 3, 0,        0, Plain, "")                       \
        /* Chops */                                                                                                  \
        X (ChopMode,    "chop_mode",    "Slice Mode",        C, 0, 3, 0,         0, None, "Transients|Syllables|Grid|Manual") \
        X (ChopSens,    "chop_sens",    "Sensitivity",       F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (ChopCount,   "chop_count",   "Slices",            I, 2, 64, 16,       0, Plain, "")                       \
        X (ChopBase,    "chop_base",    "First Key",         I, 24, 96, 60,      0, Note, "")                        \
        X (ChopPlay,    "chop_play",    "Slice Playback",    C, 0, 1, 0,         0, None, "One Shot|Gate")           \
        X (ChopChoke,   "chop_choke",   "Choke",             B, 0, 1, 1,         0, None, "")                        \
        X (ChopFade,    "chop_fade",    "Slice Fade",        F, 0, 50, 2,        8, Ms, "")                          \
        /* Modulation */                                                                                             \
        HULDRA_LFO_PARAMS (X, 1) HULDRA_LFO_PARAMS (X, 2) HULDRA_LFO_PARAMS (X, 3) HULDRA_LFO_PARAMS (X, 4)          \
        HULDRA_MSEG_PARAMS (X, 1) HULDRA_MSEG_PARAMS (X, 2) HULDRA_MSEG_PARAMS (X, 3)                                \
        X (Macro1, "macro1", "Macro 1", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro2, "macro2", "Macro 2", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro3, "macro3", "Macro 3", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro4, "macro4", "Macro 4", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro5, "macro5", "Macro 5", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro6, "macro6", "Macro 6", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro7, "macro7", "Macro 7", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro8, "macro8", "Macro 8", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (PadX,        "pad_x",        "Pad X",             F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (PadY,        "pad_y",        "Pad Y",             F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (PadDrift,    "pad_drift",    "Pad Drift",         F, 0, 1, 0,         0, Pct, "")                         \
        X (PadDriftRate,"pad_drift_rate","Drift Speed",      F, 0.01f, 2, 0.1f,  -1, Hz, "")                         \
        HULDRA_MOD_SLOT (X, 1)  HULDRA_MOD_SLOT (X, 2)  HULDRA_MOD_SLOT (X, 3)  HULDRA_MOD_SLOT (X, 4)               \
        HULDRA_MOD_SLOT (X, 5)  HULDRA_MOD_SLOT (X, 6)  HULDRA_MOD_SLOT (X, 7)  HULDRA_MOD_SLOT (X, 8)               \
        HULDRA_MOD_SLOT (X, 9)  HULDRA_MOD_SLOT (X, 10) HULDRA_MOD_SLOT (X, 11) HULDRA_MOD_SLOT (X, 12)              \
        HULDRA_MOD_SLOT (X, 13) HULDRA_MOD_SLOT (X, 14) HULDRA_MOD_SLOT (X, 15) HULDRA_MOD_SLOT (X, 16)              \
        /* Motion: arpeggiator, pattern, generative autoplay */                                                      \
        X (MotMode,     "mot_mode",     "Motion",            C, 0, 3, 0,         0, None, "Off|Arp|Pattern|Generative") \
        X (MotRate,     "mot_rate",     "Motion Rate",       C, 0, 7, 3,         0, None, "1/4|1/8|1/8T|1/16|1/16T|1/32|1/2|1 Bar") \
        X (MotSwing,    "mot_swing",    "Swing",             F, 0, 0.75f, 0,     0, Pct, "")                         \
        X (MotGate,     "mot_gate",     "Gate",              F, 0.05f, 2, 0.8f,  0, Pct, "")                         \
        X (MotFree,     "mot_free",     "Autoplay",          B, 0, 1, 0,         0, None, "")                        \
        X (MotBpm,      "mot_bpm",      "Internal Tempo",    F, 60, 200, 110,    0, Bpm, "")                         \
        X (MotLatch,    "mot_latch",    "Latch",             B, 0, 1, 0,         0, None, "")                        \
        X (ArpMode,     "arp_mode",     "Arp Mode",          C, 0, 6, 0,         0, None, "Up|Down|Up Down|Random|As Played|Chord|Converge") \
        X (ArpOct,      "arp_oct",      "Arp Octaves",       I, 1, 4, 1,         0, Oct, "")                         \
        X (PatLength,   "pat_length",   "Steps",             I, 1, 32, 16,       0, Steps, "")                       \
        X (GenDensity,  "gen_density",  "Density",           F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (GenRange,    "gen_range",    "Range",             I, 0, 4, 1,         0, Oct, "")                         \
        X (GenEvolve,   "gen_evolve",   "Evolve",            F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (GenSeed,     "gen_seed",     "Seed",              I, 1, 999, 1,       0, Plain, "")                       \
        X (GenPoly,     "gen_poly",     "Notes",             I, 1, 4, 1,         0, Plain, "")                       \
        X (GenLength,   "gen_length",   "Note Length",       F, 0.25f, 8, 1,     1, Mult, "")                        \
        X (GenVelRand,  "gen_vel_rand", "Velocity Random",   F, 0, 1, 0.3f,      0, Pct, "")                         \
        /* Effects */                                                                                                \
        X (SatOn,       "sat_on",       "Saturation On",     B, 0, 1, 0,         0, None, "")                        \
        X (SatMode,     "sat_mode",     "Saturation Mode",   C, 0, 5, 0,         0, None, "Tape|Tube|Diode|Fold|Hard|Rectify") \
        X (SatDrive,    "sat_drive",    "Saturation Drive",  F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (SatBias,     "sat_bias",     "Saturation Bias",   F, -1, 1, 0,        0, Bip, "")                         \
        X (SatTone,     "sat_tone",     "Saturation Tone",   F, -1, 1, 0,        0, Bip, "")                         \
        X (SatOs,       "sat_os",       "Oversampling",      C, 0, 2, 1,         0, None, "1x|2x|4x")                \
        X (SatMix,      "sat_mix",      "Saturation Mix",    F, 0, 1, 1,         0, Pct, "")                         \
        X (SatOut,      "sat_out",      "Saturation Output", F, -24, 12, 0,      0, Db, "")                          \
        X (LofiOn,      "lofi_on",      "Lo-Fi On",          B, 0, 1, 0,         0, None, "")                        \
        X (LofiWow,     "lofi_wow",     "Wow",               F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (LofiFlutter, "lofi_flutter", "Flutter",           F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (LofiBits,    "lofi_bits",    "Bits",              F, 3, 16, 16,       0, Bits, "")                        \
        X (LofiRate,    "lofi_rate",    "Downsample",        F, 1, 20, 1,        4, Mult, "")                        \
        X (LofiNoise,   "lofi_noise",   "Tape Hiss",         F, 0, 1, 0.1f,      0, Pct, "")                         \
        X (LofiAge,     "lofi_age",     "Age",               F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (LofiMix,     "lofi_mix",     "Lo-Fi Mix",         F, 0, 1, 1,         0, Pct, "")                         \
        X (CompOn,      "comp_on",      "Compressor On",     B, 0, 1, 0,         0, None, "")                        \
        X (CompThresh,  "comp_thresh",  "Comp Threshold",    F, -48, 0, -18,     0, Db, "")                          \
        X (CompRatio,   "comp_ratio",   "Comp Ratio",        F, 1, 20, 3,        4, Ratio, "")                       \
        X (CompAttack,  "comp_attack",  "Comp Attack",       F, 0.1f, 100, 10,   10, Ms, "")                         \
        X (CompRelease, "comp_release", "Comp Release",      F, 10, 1000, 150,   150, Ms, "")                        \
        X (CompKnee,    "comp_knee",    "Comp Knee",         F, 0, 18, 6,        0, Db, "")                          \
        X (CompMakeup,  "comp_makeup",  "Comp Makeup",       F, 0, 24, 0,        0, Db, "")                          \
        X (CompMix,     "comp_mix",     "Comp Mix",          F, 0, 1, 1,         0, Pct, "")                         \
        X (CompHpf,     "comp_hpf",     "Comp Sidechain HPF",F, 20, 500, 20,     -1, Hz, "")                         \
        X (SqOn,        "sq_on",        "Squash On",         B, 0, 1, 0,         0, None, "")                        \
        X (SqDepth,     "sq_depth",     "Squash Depth",      F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (SqTime,      "sq_time",      "Squash Time",       F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (SqUpward,    "sq_upward",    "Upward",            F, 0, 1, 0.6f,      0, Pct, "")                         \
        X (SqOut,       "sq_out",       "Squash Output",     F, -12, 12, 0,      0, Db, "")                          \
        X (EqOn,        "eq_on",        "EQ On",             B, 0, 1, 0,         0, None, "")                        \
        X (EqLowCut,    "eq_lowcut",    "EQ Low Cut",        F, 20, 1000, 20,    -1, Hz, "")                         \
        X (EqLowFreq,   "eq_low_freq",  "EQ Low Freq",       F, 20, 500, 120,    -1, Hz, "")                         \
        X (EqLowGain,   "eq_low_gain",  "EQ Low Gain",       F, -18, 18, 0,      0, Db, "")                          \
        X (EqMidFreq,   "eq_mid_freq",  "EQ Mid Freq",       F, 40, 8000, 400,   -1, Hz, "")                         \
        X (EqMidGain,   "eq_mid_gain",  "EQ Mid Gain",       F, -18, 18, 0,      0, Db, "")                          \
        X (EqMidQ,      "eq_mid_q",     "EQ Mid Q",          F, 0.2f, 10, 1,     1, Plain, "")                       \
        X (EqMid2Freq,  "eq_mid2_freq", "EQ Mid 2 Freq",     F, 200, 16000, 3000,-1, Hz, "")                         \
        X (EqMid2Gain,  "eq_mid2_gain", "EQ Mid 2 Gain",     F, -18, 18, 0,      0, Db, "")                          \
        X (EqMid2Q,     "eq_mid2_q",    "EQ Mid 2 Q",        F, 0.2f, 10, 1,     1, Plain, "")                       \
        X (EqHighFreq,  "eq_high_freq", "EQ High Freq",      F, 1000, 20000, 9000,-1, Hz, "")                        \
        X (EqHighGain,  "eq_high_gain", "EQ High Gain",      F, -18, 18, 0,      0, Db, "")                          \
        X (EqHighCut,   "eq_highcut",   "EQ High Cut",       F, 1000, 20000, 20000,-1, Hz, "")                       \
        X (ChrOn,       "chr_on",       "Motion FX On",      B, 0, 1, 0,         0, None, "")                        \
        X (ChrMode,     "chr_mode",     "Motion FX Mode",    C, 0, 4, 0,         0, None, "Chorus|Ensemble|Phaser|Flanger|Doubler") \
        X (ChrRate,     "chr_rate",     "Motion FX Rate",    F, 0.02f, 10, 0.5f, -1, Hz, "")                         \
        X (ChrDepth,    "chr_depth",    "Motion FX Depth",   F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (ChrFeedback, "chr_fb",       "Motion FX Feedback",F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (ChrMix,      "chr_mix",      "Motion FX Mix",     F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (DlyOn,       "dly_on",       "Delay On",          B, 0, 1, 0,         0, None, "")                        \
        X (DlyMode,     "dly_mode",     "Delay Mode",        C, 0, 2, 0,         0, None, "Clean|Tape|Grains")       \
        X (DlySync,     "dly_sync",     "Delay Sync",        B, 0, 1, 1,         0, None, "")                        \
        X (DlyBeats,    "dly_beats",    "Delay Note",        C, 0, 10, 7,        0, None, "1/32|1/16T|1/16|1/8T|1/16D|1/8|1/4T|1/8D|1/4|1/4D|1/2") \
        X (DlyTime,     "dly_time",     "Delay Time",        F, 1, 2000, 350,    350, Ms, "")                        \
        X (DlyFeedback, "dly_fb",       "Delay Feedback",    F, 0, 0.95f, 0.35f, 0, Pct, "")                         \
        X (DlyPingPong, "dly_pingpong", "Delay Ping-Pong",   B, 0, 1, 1,         0, None, "")                        \
        X (DlyDamp,     "dly_damp",     "Delay Damping",     F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (DlyLowCut,   "dly_lowcut",   "Delay Low Cut",     F, 20, 2000, 200,   -1, Hz, "")                         \
        X (DlyDuck,     "dly_duck",     "Delay Ducking",     F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (DlyMix,      "dly_mix",      "Delay Mix",         F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (VerbOn,      "verb_on",      "Reverb On",         B, 0, 1, 0,         0, None, "")                        \
        X (VerbMode,    "verb_mode",    "Reverb Mode",       C, 0, 4, 2,         0, None, "Room|Plate|Hall|Shimmer|Cloud") \
        X (VerbSize,    "verb_size",    "Reverb Size",       F, 0, 1, 0.6f,      0, Pct, "")                         \
        X (VerbDecay,   "verb_decay",   "Reverb Decay",      F, 0.1f, 20, 2.5f,  2, Time, "")                        \
        X (VerbDamp,    "verb_damp",    "Reverb Damping",    F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (VerbPre,     "verb_predelay","Reverb Pre-Delay",  F, 0, 0.25f, 0.02f, 0.05f, Time, "")                    \
        X (VerbLowCut,  "verb_lowcut",  "Reverb Low Cut",    F, 20, 1000, 180,   -1, Hz, "")                         \
        X (VerbWidth,   "verb_width",   "Reverb Width",      F, 0, 1, 1,         0, Pct, "")                         \
        X (VerbShimmer, "verb_shimmer", "Shimmer",           F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (VerbDuck,    "verb_duck",    "Reverb Ducking",    F, 0, 1, 0,         0, Pct, "")                         \
        X (VerbMix,     "verb_mix",     "Reverb Mix",        F, 0, 1, 0.25f,     0, Pct, "")                         \
        X (WidthOn,     "width_on",     "Width On",          B, 0, 1, 0,         0, None, "")                        \
        X (WidthAmount, "width_amount", "Stereo Width",      F, 0, 2, 1.2f,      0, Pct, "")                         \
        X (WidthMono,   "width_mono",   "Mono Below",        F, 20, 500, 100,    -1, Hz, "")                         \
        X (LimOn,       "lim_on",       "Limiter On",        B, 0, 1, 1,         0, None, "")                        \
        X (LimMode,     "lim_mode",     "Limiter Mode",      C, 0, 2, 0,         0, None, "Soft|Hard|Tape")          \
        X (LimDrive,    "lim_drive",    "Limiter Drive",     F, 0, 24, 0,        0, Db, "")                          \
        X (LimCeiling,  "lim_ceiling",  "Limiter Ceiling",   F, -12, 0, -0.3f,   0, Db, "")

    #define HULDRA_ENUM_ENTRY(e, ...) e,
    enum Id : int { HULDRA_PARAMS (HULDRA_ENUM_ENTRY) Count };
    #undef HULDRA_ENUM_ENTRY

    // The block of parameters that is evaluated per voice (per-note modulation)
    constexpr Id VoiceFirst = Tune, VoiceLast = UniSpread;
    inline bool isVoiceParam (int id) { return id >= VoiceFirst && id <= VoiceLast; }
    constexpr int kNumVoiceParams = VoiceLast - VoiceFirst + 1;

    enum class Kind { Float, Int, Choice, Bool };
    enum class Format { None, Pct, Bip, Hz, Time, Ms, Db, Semi, Cents, Balance, Mult, Oct, Bits, Ratio, Note, Bpm, Steps, Plain,
                        Speed, Rate, Vowel };

    struct Spec
    {
        Id id;
        const char* paramId;
        const char* name;
        Kind kind;
        float min, max, def, curve;
        Format format;
        juce::StringArray choices;
    };

    const std::array<Spec, Count>& specs();
    inline const Spec& spec (Id id) { return specs()[(size_t) id]; }
    inline juce::String paramId (Id id) { return spec (id).paramId; }

    // Mapping between plain values and 0..1 (same maths as the host sees)
    float toNormalised (const Spec&, float plain);
    float fromNormalised (const Spec&, float normalised);
    juce::String formatValue (const Spec&, float plain);

    // Modulation targets: every continuous parameter except the ones that make no sense to modulate.
    const std::vector<Id>& modTargets();
    constexpr int kNumModSlots = 16;
    constexpr int kNumMacros = 8;
    constexpr int kNumLfos = 4;
    constexpr int kNumMsegs = 3;
    inline Id modSrc (int slot) { return (Id) (Mod1Src + slot * 3); }
    inline Id modDst (int slot) { return (Id) (Mod1Dst + slot * 3); }
    inline Id modAmt (int slot) { return (Id) (Mod1Amt + slot * 3); }
    inline Id macro (int index) { return (Id) (Macro1 + index); }
    inline Id lfoParam (int lfo, int field) { return (Id) (Lfo1Shape + lfo * 6 + field); }   // field: 0 shape .. 5 fade
    inline Id msegTime (int m) { return (Id) (Mseg1Time + m * 2); }
    inline Id msegLoop (int m) { return (Id) (Mseg1Loop + m * 2); }

    enum class ModSource { None, Velocity, Key, Random, Pressure, Slide, ModWheel, PitchBend, AmpEnv, FilterEnv,
                           Lfo1, Lfo2, Lfo3, Lfo4, Mseg1, Mseg2, Mseg3,
                           Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8, MotionLane, PadX, PadY, Count };
    constexpr int kNumModSources = (int) ModSource::Count;

    // Sources that differ per note (the others are shared by all voices)
    inline bool isPerVoiceSource (ModSource s)
    {
        return s == ModSource::Velocity || s == ModSource::Key || s == ModSource::Random || s == ModSource::Pressure
            || s == ModSource::Slide || s == ModSource::AmpEnv || s == ModSource::FilterEnv
            || s == ModSource::Lfo1 || s == ModSource::Lfo2 || s == ModSource::Lfo3 || s == ModSource::Lfo4
            || s == ModSource::Mseg1 || s == ModSource::Mseg2 || s == ModSource::Mseg3;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::String noteName (int midiNote);

    // Plain values of all parameters, read once per audio block.
    struct Values
    {
        std::array<float, Count> v {};
        float operator[] (Id id) const { return v[(size_t) id]; }
        float& operator[] (Id id) { return v[(size_t) id]; }
        int i (Id id) const { return (int) std::lround (v[(size_t) id]); }
        bool b (Id id) const { return v[(size_t) id] > 0.5f; }
    };

    class Reader
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState& state)
        {
            for (int i = 0; i < Count; ++i)
            {
                raw[(size_t) i] = state.getRawParameterValue (specs()[(size_t) i].paramId);
                jassert (raw[(size_t) i] != nullptr);
            }
        }

        void read (Values& out) const
        {
            for (size_t i = 0; i < raw.size(); ++i)
                out.v[i] = raw[i]->load (std::memory_order_relaxed);
        }

    private:
        std::array<std::atomic<float>*, Count> raw {};
    };

    // Defaults as Values (for tests and offline rendering)
    Values defaults();
}
