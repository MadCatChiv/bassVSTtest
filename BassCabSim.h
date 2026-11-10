// =============================================================================
//  BassCabSim.h  -  parametric cabinet model (HP -> peak -> peak -> high-shelf -> LP).
//  See BassVoicings.h for the rationale.  5 biquads per channel: ~0.5% of one core.
// =============================================================================
#pragma once
#include "BassBiquad.h"
#include "BassVoicings.h"

namespace bd
{
    class CabSim
    {
    public:
        void configure (int model, double fs)
        {
            model = std::min (std::max (model, 0), kNumBuiltInCabs - 1);
            const auto& c = kCabs[model];
            st[0].set (makeHighPass (fs, c.hpHz, c.hpQ));
            st[1].set (makePeak (fs, c.p1Hz, c.p1Q, c.p1Db));
            st[2].set (makePeak (fs, c.p2Hz, c.p2Q, c.p2Db));
            st[3].set (makeHighShelf (fs, c.shHz, 0.6, c.shDb));
            st[4].set (makeLowPass (fs, c.lpHz, c.lpQ));
        }
        void reset() { for (auto& s : st) s.reset(); }

        void process (float* const* data, int numCh, int n) noexcept
        {
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    float x = data[ch][i];
                    for (auto& s : st) x = s.process (ch, x);
                    data[ch][i] = x;
                }
        }
    private:
        Biquad st[5];
    };
}
