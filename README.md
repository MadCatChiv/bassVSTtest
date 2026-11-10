# Bass Driver 2.0

A DI / preamp-style bass processor: dual-stage compressor -> anti-aliased two-stage saturation with
a tight clean low end -> analogue-style EQ -> cabinet (built-in models or your own IR), with
loudness-aware automatic gain matching. VST3 + Standalone (AU on macOS), built with JUCE 8.

## Build (Windows, via GitHub - no Visual Studio needed)
1. Upload ALL files in this folder to the root of your repo (replace the old ones).
   The `.github/workflows/build-windows.yml` you already have is fine; it is included here too.
2. Actions tab -> "Build Windows VST3" -> Run workflow -> download the `BassDriver-Windows` artifact.
3. Copy the `Bass Driver.vst3` FOLDER to `C:\Program Files\Common Files\VST3\` and rescan in your DAW.

Local build: `cmake -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`.

## Controls
| Section | Control | What it does |
|---|---|---|
| Drive | **Voicing** | DI Drive (SansAmp-style), Tube Stack (SVT-style), Studio Clean (hi-fi), Grind (modern aggressive), Fuzz |
| | **Drive** | Saturation amount; level-compensated, so it changes grit, not volume |
| | **Blend** | Clean <-> driven (lows always stay clean) |
| | **Tight** | Crossover x0.5..x2: how much low end stays clean |
| Compressor | **Comp** | Slow opto-style leveller (program-dependent release) |
| | **Peak** | Fast peak catcher in series after Comp |
| Tone | **Bass / Mid / Mid Freq / Treble / Presence** | Analogue-style EQ; corner frequencies follow the voicing; Mid is proportional-Q |
| Cabinet | **Cab** + model | Vintage 1x15 / Punch 4x10 / Big 8x10 / **User IR** (any .wav/.aif/.flac, capped at 500 ms) |
| Output | **Auto In** | Slowly levels your signal to -20 LUFS so Drive/Comp feel identical for any bass |
| | **Auto Out** | Matches output loudness to input loudness (ITU-R BS.1770 K-weighted) |
| | **Output** | Final trim after auto-matching |

Workflow: **A/B** slots (A>B copies), **Undo/Redo** (every drag/click is one step), **Shift-drag** for
fine adjustment, **double-click** to reset, preset browser with 12 factory presets and user presets
(Save... stores `.bdpreset` files; "Show preset folder" opens them), tooltips on every control.

## Technical facts
- Latency 61 samples (1.27 ms @ 48 kHz), reported to the host; bypass is latency-compensated.
- CPU: roughly 3-4.5% of one core for the full stereo chain at 48 kHz; mono tracks cost about half.
- Sessions saved by version 1 load correctly (old Voice choice is mapped to the new voicings).
- See `TECHNICAL_NOTES.md` for the DSP design decisions and the measurements behind them.

Voicing names describe the *character family* each is inspired by. They are original voicings,
not circuit models of, nor affiliated with, any commercial product.
