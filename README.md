# Bass Driver

A DI/preamp-style bass driver (SansAmp-flavoured) as a VST3 / Standalone (+AU on macOS), built with JUCE.

## Build
Needs CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on macOS, g++ on Linux).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE is downloaded automatically. With COPY_PLUGIN_AFTER_BUILD the VST3 is installed to your
system VST3 folder (on Windows you may need to run the build from an admin shell, or copy
`build/BassDriver_artefacts/Release/VST3/Bass Driver.vst3` to `C:\Program Files\Common Files\VST3` yourself).
Linux needs: libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev

## Controls
- **Drive** 0-10: up to 40 dB of gain into the saturator. Level-compensated, so it changes grit, not volume.
- **Blend**: clean (0%) to driven (100%). Below ~100 Hz the signal always stays clean, so the low end stays tight.
- **Voice**: Tube (asymmetric, even harmonics, warm) / Solid State (hard knee, symmetric, punchy) / Fuzz (lopsided, gritty).
- **Bass / Mid / Mid Freq / Treble / Presence**: post-drive tone stack (+/-12 dB; mid sweeps 200 Hz - 2.5 kHz).
- **Cab Sim**: gentle ~5 kHz roll-off for a speaker-ish top end when you are not running a cab IR after it.
- **Auto Input**: slowly levels what you play to a fixed reference so Drive feels the same for any bass/DI level.
- **Auto Output**: matches the loudness coming out to the loudness going in (+/-24 dB), so you can sweep tone and drive without level jumps. Output knob is a trim on top. Settles in about a second after big level changes.
- 5 built-in programs: Clean DI, Tube Warmth, Classic Scoop, Solid Rock, Fuzz Grind.

## Notes
- 4x oversampled saturation (linear-phase), latency is reported to the host and the dry path is delay-compensated.
- This is a voiced approximation of the DI-driver topology (split, saturate highs, blend, post EQ), not a circuit model of any specific product.
