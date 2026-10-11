# Third-party notices

Huldra itself is licensed under the AGPL-3.0 ([LICENSE](LICENSE)). It uses:

| Component | Use | License |
|---|---|---|
| [JUCE 8](https://juce.com) | application framework, plugin wrappers (downloaded at build time, not in this repository) | AGPLv3 (JUCE offers AGPLv3 or a commercial licence; Huldra uses the AGPLv3 option) |
| JUCE's bundled SDKs (VST3 SDK, AudioUnitSDK, LV2 headers, …) | plugin formats | their own permissive licences, see the JUCE repository |
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.3 | pitch / time stretching of the vocal engine, `external/signalsmith-stretch` | MIT, `external/signalsmith-stretch/LICENSE.txt` |
| [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) 0.6.4 | FFT and STFT used by Signalsmith Stretch, `external/signalsmith-linear` | MIT, `external/signalsmith-linear/LICENSE.txt` |
| [Manrope](https://github.com/googlefonts/manrope) (Regular, Medium, SemiBold) | UI font, embedded unmodified | SIL Open Font License 1.1, `resources/fonts/OFL.txt` |

"VST" is a trademark of Steinberg Media Technologies GmbH. "Audio Units" is a trademark of Apple Inc.
