// =============================================================================
//  BassPresets.h  -  factory presets.  User presets live as .bdpreset XML files in the
//  per-user application-data folder (see BassDriverAudioProcessor::userPresetFolder()).
// =============================================================================
#pragma once

namespace bd
{
    struct FactoryPreset
    {
        const char* name;
        float drive, blend, tight, comp, peak, bass, mid, midFreq, treble, presence;
        int voicing;        // index into kVoicings
        bool cab;
        int cabModel;       // index into kCabs
    };

    inline constexpr FactoryPreset kFactoryPresets[] =
    {
        //  name                 drive blend tight comp peak bass  mid  midHz treb  pres  voice cab  model
        { "Init - Clean DI",       1.0f, 20.f, 50.f, 0.f, 0.f,  0.f,  0.f,  700.f,  0.f,  0.f, 0, false, 1 },
        { "Studio Transparent",    3.0f, 35.f, 50.f, 3.f, 2.f,  1.5f, 0.f,  700.f,  1.f,  0.f, 2, false, 1 },
        { "Smooth Leveller",       2.0f, 25.f, 50.f, 6.f, 3.f,  1.f,  0.f,  600.f,  0.f,  0.f, 2, false, 1 },
        { "Dub Sub",               1.5f, 20.f, 40.f, 5.f, 1.f,  5.f,  0.f,  300.f, -6.f, -3.f, 2, false, 1 },
        { "DI Drive Classic",      5.5f, 65.f, 50.f, 2.f, 1.f,  4.f, -4.f,  500.f,  3.f,  3.f, 0, false, 1 },
        { "Pick Attack",           4.0f, 55.f, 55.f, 3.f, 4.f,  0.f,  3.f, 1200.f,  5.f,  4.f, 0, false, 1 },
        { "Tube Stack Classic",    4.0f, 60.f, 50.f, 4.f, 2.f,  3.f,  2.f,  400.f,  1.f,  0.f, 1, true,  2 },
        { "Tube Stack Pushed",     7.0f, 75.f, 50.f, 5.f, 3.f,  3.f,  1.f,  450.f,  1.f,  1.f, 1, true,  2 },
        { "Motown Round",          2.5f, 40.f, 40.f, 6.f, 1.f,  2.f,  1.f,  350.f, -3.f, -2.f, 1, true,  0 },
        { "Grind Tight",           6.0f, 70.f, 70.f, 3.f, 3.f,  1.f, -3.f,  600.f,  2.f,  3.f, 3, true,  1 },
        { "Grind Heavy",           8.5f, 85.f, 75.f, 4.f, 5.f,  2.f, -2.f,  800.f,  3.f,  4.f, 3, true,  1 },
        { "Fuzz Rig",              7.5f, 80.f, 45.f, 3.f, 2.f,  2.f,  2.f, 1000.f, -2.f,  0.f, 4, true,  2 },
    };
    constexpr int kNumFactoryPresets = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
}
