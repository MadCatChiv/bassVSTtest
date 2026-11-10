// =============================================================================
//  BassLoudness.h  -  ITU-R BS.1770 style loudness for the auto-gain system.
//
//  WHY LOUDNESS INSTEAD OF RMS?  Plain RMS counts sub-bass energy that we barely hear and
//  under-counts presence.  Saturation moves energy from the fundamental into harmonics, so
//  an RMS-matched signal sounds quieter (or louder) than the clean one.  The K-weighting
//  curve (38 Hz high-pass "RLB" + ~+4 dB high-shelf above ~1.5 kHz) is the standard model of
//  perceived loudness, so matching K-weighted level keeps drive/EQ changes perceptually level.
//
//  Filter parameters are the published ITU-R BS.1770 values expressed as RBJ sections
//  (identical to the 48 kHz reference coefficients, and they re-derive correctly at other
//  sample rates instead of being hard-coded for 48 kHz).
//
//  MEASUREMENT:  a block-rate ring buffer of mean-square values gives sliding rectangular
//  windows (short 0.6 s for in/out matching, long 3 s for input levelling) with O(1) cost.
//  A running count means the average is correct from the very first block (no start-up bias).
// =============================================================================
#pragma once
#include "BassBiquad.h"
#include <vector>

namespace bd
{
    struct KWeighting
    {
        Biquad shelf, hp;
        void prepare (double fs)
        {
            shelf.set (makeHighShelf (fs, 1681.974450955533, 0.7071752369554196, 3.999843853973347));
            hp.set    (makeHighPass  (fs, 38.13547087602444, 0.5003270373238773));
            shelf.reset(); hp.reset();
        }
        inline float process (int ch, float x) noexcept { return hp.process (ch, shelf.process (ch, x)); }
    };

    class LoudnessWindow
    {
    public:
        void prepare (int shortBlocks, int longBlocks)
        {
            ns = std::max (1, shortBlocks);
            nl = std::max (ns, longBlocks);
            ring.assign ((size_t) nl, 0.0);
            idx = 0; count = 0; sumShort = sumLong = 0.0;
        }

        /** @param ms sum over channels of the mean-square of the K-weighted block (BS.1770 channel sum). */
        void push (double ms) noexcept
        {
            if (count >= ns) sumShort -= ring[(size_t) ((idx - ns + nl) % nl)];
            sumShort += ms;
            if (count >= nl) sumLong -= ring[(size_t) idx];
            sumLong += ms;
            ring[(size_t) idx] = ms;
            idx = (idx + 1) % nl;
            if (count < nl) ++count;
        }

        float lufsShort() const noexcept { return toLufs (sumShort / (double) std::min (std::max (count, 1), ns)); }
        float lufsLong()  const noexcept { return toLufs (sumLong  / (double) std::min (std::max (count, 1), nl)); }

    private:
        static float toLufs (double ms) noexcept { return (float) (-0.691 + 10.0 * std::log10 (std::max (ms, 1.0e-12))); }
        std::vector<double> ring;
        int ns = 1, nl = 1, idx = 0, count = 0;
        double sumShort = 0.0, sumLong = 0.0;
    };
}
