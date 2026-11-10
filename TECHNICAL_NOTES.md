# Technical notes (2.0 release pass)

Every change below has a measured or well-established reason. Numbers come from the offline test
harness (48 kHz, stereo, release build) run during development.

| Area | v1 | v2 | Why |
|---|---|---|---|
| Anti-aliasing | 4x oversampling, naive shaper | 4x half-band FIR **+ first-order ADAA** + band-limit before the clipper | Worst-case aliasing (2.9 kHz tone, heavy drive): Grind -61 dB vs -41 dB naive 4x; at 2x ADAA alone gave 15 dB |
| Saturation | one waveshaper (3 curves) | two cascaded stages (tanh preamp -> voicing clipper), drive split 35/65 | Real preamps distribute gain; harmonic content evolves smoothly with Drive |
| Level compensation | reference-peak makeup | RMS-based makeup from the actual transfer curve + loudness trim | Drive changes tone, not volume; output flat within about 1 dB over the full Drive range |
| Crossover | 1st-order split (-6 dB/oct) | **LR4** (4th-order Linkwitz-Riley) | Clean band isolation; bands sum to an all-pass, so blend 0 is magnitude-flat (measured +/-0.0 dB to 1.6 kHz) |
| Compressor | none | dual-stage: RMS opto-style leveller (dual-release, program dependent) + fast peak stage, 55 Hz sidechain HP, linked stereo | Sustain without squash; low notes do not modulate the gain (harmonic distortion -58 dB at maximum settings on a 41 Hz note) |
| Voices | Tube / Solid State / Fuzz | 5 voicings: clipper + pre-emphasis + post filters + crossover + tone corners | A voicing is a whole signal flow, not a curve |
| EQ | fixed-Q RBJ, 32-bit state, per-block coefficient jumps | low-Q shelves, proportional-Q mid, double-precision state, smoothed parameters, coefficient updates every 64 samples | Analogue-like behaviour; no zipper noise or low-frequency state noise |
| Cabinet | 5 kHz low-pass | 3 parametric models + user IR convolution (500 ms cap) | Real cab behaviour without shipping IR data |
| Auto gain | RMS ratio | K-weighted loudness (BS.1770), 0.6 s match window, 3 s input window, dead-zone, asymmetric slew, gated | Matches perceived loudness; steady-state error within +/-0.3 LU for all voicings with extreme EQ |
| Control rate | per host block | fixed 64-sample sub-blocks | Predictable smoothing independent of host buffer size |
| Switching voicing/cab | instant | 1.3 ms duck either side of the change | No clicks |
| Robustness | - | NaN trap with state reset, soft safety limiter, fuzz-tested (hostile input, random parameters) | 0 non-finite samples in 30 s fuzz run |
| CPU | 4x oversampling always | skips the heavy path at Blend 0 (0.3% of a core), mono = half cost, no heap allocation on the audio thread | 2.8% (Grind) to 4.5% (Tube Stack) of one core, stereo |

Known limits: the peak stage has no look-ahead (zero added latency; overshoot is caught by the
saturator/limiter); built-in cabinets are parametric approximations (load an IR for exact speakers);
K-weighting under-represents sub-40 Hz energy by design.
