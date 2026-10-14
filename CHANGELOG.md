# Changelog

All notable changes to Huldra. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] – 2026-10-14
- Built on [fjord-base](https://github.com/fjord-sounds/fjord-base), the code shared by all Fjord plugins: clone it next to
  this repository (or CMake downloads the pinned release). Header, preset bar, browser, controls, theme, the FX page,
  build scripts, CI workflows and the screenshot tooling now come from there.
- **Unified effects**: fifteen effects and the clipper in every Fjord plugin – Saturation, Lo-Fi, Transient, Compressor,
  Squash, EQ, Filter, Motion, Delay, Reverb, Width and the new Tremolo / Auto-Pan, Frequency Shifter, Exciter and
  Harmonizer – with at least 40 presets per effect and 35 effect chains (**FX CHAIN**, **Lock FX**). Existing effects were
  extended (e.g. filter LFO and per-note envelope, delay modes, reverb modes with freeze, EQ slopes, motion modes).
- **Locks**: the padlock in the preset bar keeps engines, chops, key edits, voice, filter, envelopes, MSEGs, LFOs, matrix,
  macros, motion settings and effects when switching presets; padlocks in the panels of the pages.
- *Window size* in the options menu (75–150 %).
- The FX page is the same in all plugins: live tiles in two rows, big graph, per-effect preset menu.
- Effect parameter ids are now `fx_<effect>_<param>` and the factory presets use them; the effect order is stored as ids.
- Huldra's own effect code was replaced by the shared effects (the limiter is the Clipper); the factory presets were converted.
- The screenshot tool reads `tools/screenshots/shots.conf`.

## [0.1.0] – first version
- Sound design sampler: load (drag and drop, several files at once) or record your own sounds; pitch, melody,
  transients, syllables and spectrum are analysed on arrival. Sounds are embedded in the project (FLAC).
- Four engines: classic (loops, ping-pong, reverse, unison, mip maps), granular (64 grains per voice), spectral
  (phase-vocoder resynthesis with freeze, blur, smear, shift, stretch, tilt, harmonize, gate and a painted mask) and
  vocal (Signalsmith Stretch: pitch and time apart, formants kept or shifted).
- Chops by transients, syllables, grid or manual markers; keymap with zones, auto mapping by pitch; every key editable
  (tuning, gain, pan, brightness, start, formant, engine, slice, reverse, mute, choke group).
- Voice filter with eight types (including vowel, comb and ladder), two envelopes, drive, unison, glide, poly / mono
  / legato, MPE.
- Scale snap, retune (auto-tune of the sound's own melody), chord mode with fifteen chord types, strum and spread.
- Modulation: four LFOs, three MSEGs, eight macros, morph pad with drift, sixteen-slot matrix.
- Motion: arpeggiator, 32-step pattern with ratchets, chance and a mod lane, generative player; autoplay without keys.
- Nine reorderable effects (saturation, lo-fi, compressor, squash, EQ, motion, delay, reverb with shimmer and cloud,
  width) and a limiter.
- Bounce (resample in place), A/B comparison, undo / redo, drag-to-DAW WAV export.
- 325 factory presets in eleven categories, eight synthesized demo voices to hear them.
- VST3 (macOS, Windows, Linux), Audio Unit (macOS), LV2 (Linux) and a standalone app.
