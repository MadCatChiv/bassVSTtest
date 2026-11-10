// =============================================================================
//  BassCompressor.h  -  dual-stage "intelligent" bass compressor, two simple controls.
//
//  STAGE 1  "COMP"  - slow opto-style leveller.
//     * RMS detector (10 ms) so it reacts to energy, not to single waveform peaks.
//     * Soft knee (10 dB) + gentle ratio (1.5:1 .. 5:1): sustain without "squash".
//     * PROGRAM-DEPENDENT release: two followers in parallel - a fast one (90 ms) and a slow
//       one (900 ms, scaled by 0.7).  The applied reduction is the larger of the two.  Short
//       peaks therefore recover quickly (the fast follower), while sustained heavy compression
//       releases slowly (the slow follower) - the behaviour of an opto cell, without an
//       explicit (and fragile) memory model.
//     * 20 ms attack lets the first transient of a note through: bass "pluck" is preserved.
//
//  STAGE 2  "PEAK"  - fast peak catcher.
//     * Looks at the signal AFTER stage 1 (series, not parallel), so it only has to deal with
//       what the leveller let through.
//     * 1.5 ms attack / 80 ms release, 4:1 .. 12:1, 4 dB knee.  Catches fast transients before
//       they hit the saturator hard (which would otherwise give uneven distortion per note).
//
//  SIDECHAIN:  2nd-order high-pass at 55 Hz on the DETECTOR path only.  Without it, the
//     fundamental of low notes (41 Hz E1) makes the gain ripple at audio rate = intermod
//     distortion ("compressor farts").  Audio path is untouched.
//
//  STEREO:  detector uses max(|L|,|R|) -> one linked gain, so the image never wanders.
//
//  Thresholds are relative to the -20 LUFS reference that Auto Input levels to, so the two
//  controls feel the same for any bass / DI level when Auto Input is on.
// =============================================================================
#pragma once
#include "BassBiquad.h"
#include <cmath>

namespace bd
{
    class DualCompressor
    {
    public:
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            auto coef = [this] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (fs * seconds))); };
            aRms = coef (0.010);
            attFast1 = coef (0.020);  relFast1 = coef (0.090);
            attSlow1 = coef (0.150);  relSlow1 = coef (0.900);
            att2 = coef (0.0015);     rel2 = coef (0.080);
            const auto hp = makeHighPass (fs, 55.0, 0.7071);
            detHp.set (hp);
            reset();
        }

        void reset() noexcept { detHp.reset(); ms1 = gFast = gSlow = gr2 = 0.f; }

        /** @return average total gain reduction (dB, >= 0) over the block, for metering. */
        float process (float* const* data, int numCh, int n, float comp01, float peak01) noexcept
        {
            const bool on1 = comp01 > 0.005f, on2 = peak01 > 0.005f;
            if (! on1 && ! on2) { gFast = gSlow = gr2 = 0.f; return 0.f; }

            // ---- parameter mapping ------------------------------------------------------------
            const float thr1 = on1 ? -17.f - 15.f * comp01 : 200.f;
            const float ratio1 = 1.5f + 3.5f * comp01;
            const float thr2 = on2 ? -6.f - 10.f * peak01 : 200.f;
            const float ratio2 = 4.f + 8.f * peak01;
            // Static makeup: roughly half the reduction expected at the reference level.
            const float makeupDb = (on1 ? 0.5f * reduction (-20.f, thr1, ratio1, 10.f) : 0.f)
                                 + (on2 ? 0.3f * reduction (-8.f,  thr2, ratio2, 4.f)  : 0.f);

            float grSum = 0.f;
            for (int i = 0; i < n; ++i)
            {
                float det = 0.f;
                for (int ch = 0; ch < numCh; ++ch)
                    det = std::max (det, std::abs (detHp.process (ch, data[ch][i])));

                // stage 1: RMS -> gain computer -> dual-release ballistics
                ms1 += aRms * (det * det - ms1);
                const float t1 = reduction (10.f * std::log10 (ms1 + 1.0e-12f), thr1, ratio1, 10.f);
                gFast += (t1 > gFast ? attFast1 : relFast1) * (t1 - gFast);
                gSlow += (t1 > gSlow ? attSlow1 : relSlow1) * (t1 - gSlow);
                const float gr1 = std::max (gFast, 0.7f * gSlow);

                // stage 2: peak after stage 1
                const float det2 = det * std::exp (-gr1 * 0.11512925f);
                const float t2 = reduction (20.f * std::log10 (det2 + 1.0e-9f), thr2, ratio2, 4.f);
                gr2 += (t2 > gr2 ? att2 : rel2) * (t2 - gr2);

                const float totalDb = gr1 + gr2;
                grSum += totalDb;
                const float g = std::exp ((makeupDb - totalDb) * 0.11512925f);
                for (int ch = 0; ch < numCh; ++ch) data[ch][i] *= g;
            }
            return grSum / (float) std::max (n, 1);
        }

    private:
        /** Soft-knee static gain computer; returns reduction in dB (>= 0). */
        static inline float reduction (float lvlDb, float thrDb, float ratio, float kneeDb) noexcept
        {
            const float over = lvlDb - thrDb;
            const float slope = 1.f - 1.f / ratio;
            if (2.f * over < -kneeDb) return 0.f;
            if (2.f * std::abs (over) <= kneeDb) { const float t = over + 0.5f * kneeDb; return slope * t * t / (2.f * kneeDb); }
            return slope * over;
        }

        double fs = 44100.0;
        float aRms = 0, attFast1 = 0, relFast1 = 0, attSlow1 = 0, relSlow1 = 0, att2 = 0, rel2 = 0;
        float ms1 = 0, gFast = 0, gSlow = 0, gr2 = 0;
        Biquad detHp;
    };
}
