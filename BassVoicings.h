// =============================================================================
//  BassVoicings.h  -  amp-inspired voicing table + cabinet models.
//
//  A "voicing" is NOT just a different waveshaper.  Real bass preamps differ in the whole
//  signal flow around the clipping element, so each voicing defines:
//
//    * the clipper itself (symmetry/knee),
//    * how hard each of the two gain stages is pushed (driveMaxDb),
//    * what the clipper SEES (pre-emphasis: high-pass "tightening", a focus bump, band-limit),
//    * what happens AFTER it (fizz filter, fixed "colour" peak and "air" shelf),
//    * the crossover frequency that keeps the lows clean,
//    * where the Bass / Treble / Presence corner frequencies sit.
//
//  The names describe the *character family* each voicing is inspired by.  None of these is
//  a circuit model of any commercial product; they are original voicings.
// =============================================================================
#pragma once

namespace bd
{
    enum class ShaperType { Tube, Poly, Fuzz };

    struct Voicing
    {
        const char* name;
        const char* inspiration;
        ShaperType shaper;
        float bias;                   // Tube shaper asymmetry (-> even harmonics)
        float driveMaxDb;             // total gain at Drive = 10
        float preHpHz;                // tightening high-pass in front of the clipper
        float preMidHz, preMidDb, preMidQ;   // focus bump in front of the clipper
        float preLpHz;                // band-limit in front of the clipper (anti-alias + fizz control)
        float postLpHz;               // fizz filter after the clipper
        float xoverHz;                // crossover: below this stays clean (scaled by Tight)
        float bassHz, trebleHz, presenceHz;  // tone-stack corners
        float colourHz, colourDb, colourQ;   // fixed post-clipper character peak
        float airDb;                  // fixed high shelf @ 4.5 kHz
    };

    inline constexpr Voicing kVoicings[] =
    {
        // SansAmp-family: transparent DI driver, tube-ish asymmetry, scooped mids, bright and open.
        { "DI Drive",     "SansAmp-style DI driver",       ShaperType::Tube, 0.12f, 36.f,  60.f,  800.f, 0.f, 1.0f, 6500.f,  9000.f,  90.f,  90.f, 3000.f, 6500.f,  500.f, -3.0f, 0.7f,  1.5f },
        // SVT-family: big, round, mid-forward, strong even harmonics, band-limited like a tube/transformer chain.
        { "Tube Stack",   "SVT-style tube head",           ShaperType::Tube, 0.22f, 30.f,  40.f,  250.f, 4.f, 0.7f, 4500.f,  6500.f,  70.f,  70.f, 2500.f, 5000.f,  400.f,  2.5f, 0.8f,  0.0f },
        // Tone-Hammer-family: hi-fi, wide-open, gentle harmonic enhancement rather than overdrive.
        { "Studio Clean", "Hi-fi studio preamp",           ShaperType::Tube, 0.04f, 20.f,  30.f, 1000.f, 0.f, 1.0f, 12000.f, 16000.f, 120.f, 100.f, 3500.f, 8000.f, 1000.f,  0.0f, 1.0f,  1.0f },
        // Darkglass-family: tight, hard-knee, aggressive upper-mid growl, scooped lows-mids.
        { "Grind",        "Modern aggressive grind",       ShaperType::Poly, 0.00f, 44.f, 120.f,  900.f, 5.f, 1.0f, 5500.f,  7500.f, 140.f,  80.f, 3500.f, 7000.f,  500.f, -4.0f, 0.9f,  2.0f },
        // Vintage fuzz: lopsided, gated-sounding clipping, dark top.
        { "Fuzz",         "Vintage fuzz",                  ShaperType::Fuzz, 0.00f, 40.f,  90.f,  600.f, 3.f, 0.8f, 4500.f,  5500.f, 110.f, 100.f, 2500.f, 5000.f,  800.f, -2.0f, 0.8f,  0.0f },
    };
    constexpr int kNumVoicings = (int) (sizeof (kVoicings) / sizeof (kVoicings[0]));

    // -------------------------------------------------------------------------
    //  Built-in cabinet models: parametric approximations (no embedded IR data, so ~zero CPU
    //  and no licensing questions).  Each is HP -> peak -> peak -> high shelf -> LP, which
    //  captures the four things that define a bass cab in a mix: the low-end tuning/rolloff,
    //  the cone/port resonance, the mid "box" character, and the HF beaming rolloff.
    //  For exact speaker response the user can load a real IR (convolution) instead.
    // -------------------------------------------------------------------------
    struct CabModel
    {
        const char* name;
        float hpHz, hpQ;
        float p1Hz, p1Db, p1Q;
        float p2Hz, p2Db, p2Q;
        float shHz, shDb;
        float lpHz, lpQ;
    };

    inline constexpr CabModel kCabs[] =
    {
        { "Vintage 1x15", 48.f, 0.8f,  90.f,  4.0f, 1.2f,  450.f, -3.0f, 0.8f, 2200.f, -4.0f, 3800.f, 0.6f },
        { "Punch 4x10",   58.f, 0.8f, 120.f,  2.0f, 1.0f, 1800.f,  3.0f, 0.9f, 3800.f, -2.0f, 5500.f, 0.6f },
        { "Big 8x10",     40.f, 0.8f,  75.f,  5.0f, 1.1f,  320.f, -2.5f, 0.8f, 3000.f, -2.5f, 4600.f, 0.6f },
    };
    constexpr int kNumBuiltInCabs = (int) (sizeof (kCabs) / sizeof (kCabs[0]));
    constexpr int kCabUserIR = kNumBuiltInCabs;   // index of the "User IR" choice
}
