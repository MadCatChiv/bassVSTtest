// =============================================================================
//  BassSaturation.h  -  anti-aliased two-stage saturation.
//
//  1. ANTI-ALIASING:  2x oversampling (linear-phase half-band FIR, done by the processor)
//     COMBINED with first-order antiderivative anti-aliasing (ADAA, Parker et al. 2016).
//     ADAA replaces y[n] = f(x[n]) with the average of f over the segment between x[n-1]
//     and x[n], i.e. (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) where F' = f.  That is
//     equivalent to a half-sample-wide rectangular pre-filter on the nonlinear output, and
//     cuts aliasing by roughly the same amount as doubling the oversampling ratio - at a
//     fraction of the cost and without the extra FIR latency of 4x.
//     All ADAA arithmetic is in double: the divided difference suffers catastrophic
//     cancellation in float when |dx| is small.
//
//  2. TWO-STAGE CASCADE:  a mild tanh "preamp" stage feeds the voicing's main clipper.
//     Real preamps spread gain over several stages; the first one compresses/rounds peaks so
//     the second sees a pre-conditioned signal.  The result is a drive control whose
//     harmonic content evolves smoothly (2nd/3rd first, then dense upper harmonics) instead
//     of the "switch-on" fizz of a single hard nonlinearity.
//
//  3. LEVEL COMPENSATION:  makeup gain is derived from the *actual* static transfer curve
//     (RMS in vs RMS out for a reference sine), so turning Drive up changes harmonic
//     content, not loudness.  The loudness-based auto-gain then only has to trim residuals.
// =============================================================================
#pragma once
#include "BassVoicings.h"
#include <cmath>
#include <algorithm>

namespace bd
{
    // log(cosh(x)) computed without overflow: |x| + log1p(exp(-2|x|)) - ln2
    inline double logCosh (double x) noexcept
    {
        x = std::abs (x);
        return x + std::log1p (std::exp (-2.0 * x)) - 0.69314718055994530942;
    }

    // ---- transfer functions f(x) and their antiderivatives F(x) ------------------------------
    // Tube: tanh with DC bias.  Asymmetric -> even harmonics.   F(x) = logcosh(x+b) - x*tanh(b)
    inline double fTube (double x, double b) noexcept { return std::tanh (x + b) - std::tanh (b); }
    inline double FTube (double x, double b) noexcept { return logCosh (x + b) - x * std::tanh (b); }

    // Poly: symmetric "solid-state" soft-clip, f = 1.5x - 0.5x^3 for |x|<1, +-1 beyond.
    // C1-continuous at the knee (f'(1)=0), harder knee than tanh, odd harmonics only.
    inline double fPoly (double x) noexcept
    {
        const double a = std::abs (x);
        return a >= 1.0 ? std::copysign (1.0, x) : 1.5 * x - 0.5 * x * x * x;
    }
    inline double FPoly (double x) noexcept
    {
        const double a = std::abs (x);
        if (a >= 1.0) return a - 0.375;
        const double a2 = a * a;
        return 0.75 * a2 - 0.125 * a2 * a2;
    }

    // Fuzz: lopsided - positive half hard (gain 2.5), negative half soft & smaller (0.8).
    inline double fFuzz (double x) noexcept { return x > 0.0 ? std::tanh (2.5 * x) : 0.8 * std::tanh (x); }
    inline double FFuzz (double x) noexcept { return x > 0.0 ? logCosh (2.5 * x) / 2.5 : 0.8 * logCosh (x); }

    inline double fSoft (double x) noexcept { return std::tanh (x); }
    inline double FSoft (double x) noexcept { return logCosh (x); }

    /** Memory of one ADAA stage (previous input and previous antiderivative value). */
    struct AdaaState
    {
        double xPrev = 0.0, FPrev = 0.0;
        void reset() noexcept { xPrev = FPrev = 0.0; }
    };

    template <typename Fn, typename FnInt>
    inline double adaa1 (AdaaState& s, double x, Fn&& f, FnInt&& F) noexcept
    {
        const double Fx = F (x);
        const double dx = x - s.xPrev;
        // Below ~1e-6 the divided difference is numerically meaningless; use the midpoint
        // value, which is the exact limit of the quotient as dx -> 0.
        const double y = std::abs (dx) < 1.0e-6 ? f (0.5 * (x + s.xPrev)) : (Fx - s.FPrev) / dx;
        s.xPrev = x;
        s.FPrev = Fx;
        return y;
    }

    /** Process one channel of an oversampled block through both stages.  The stage-B gain is
        ramped linearly across the block to avoid zipper noise from Drive automation. */
    inline void saturateBlock (float* p, int n, AdaaState& sA, AdaaState& sB,
                               const Voicing& v, double gB0, double gB1) noexcept
    {
        const double step = (gB1 - gB0) / (double) std::max (n, 1);
        double g = gB0;
        const double b = (double) v.bias;

        switch (v.shaper)
        {
            case ShaperType::Tube:
                for (int i = 0; i < n; ++i)
                {
                    const double a = adaa1 (sA, (double) p[i], fSoft, FSoft);
                    g += step;
                    p[i] = (float) adaa1 (sB, a * g, [b] (double x) { return fTube (x, b); },
                                                     [b] (double x) { return FTube (x, b); });
                }
                break;
            case ShaperType::Poly:
                for (int i = 0; i < n; ++i)
                {
                    const double a = adaa1 (sA, (double) p[i], fSoft, FSoft);
                    g += step;
                    p[i] = (float) adaa1 (sB, a * g, fPoly, FPoly);
                }
                break;
            case ShaperType::Fuzz:
                for (int i = 0; i < n; ++i)
                {
                    const double a = adaa1 (sA, (double) p[i], fSoft, FSoft);
                    g += step;
                    p[i] = (float) adaa1 (sB, a * g, fFuzz, FFuzz);
                }
                break;
        }
    }

    /** Static (non-ADAA) transfer through both stages - used only for makeup computation. */
    inline double staticTransfer (const Voicing& v, double x, double gA, double gB) noexcept
    {
        const double a = std::tanh (gA * x);
        switch (v.shaper)
        {
            case ShaperType::Tube: return fTube (gB * a, (double) v.bias);
            case ShaperType::Poly: return fPoly (gB * a);
            case ShaperType::Fuzz:
            default:               return fFuzz (gB * a);
        }
    }

    /** Drive law: total dB is split 35% / 65% between the preamp stage and the main clipper. */
    inline void driveGains (const Voicing& v, float drive01, double& gA, double& gB) noexcept
    {
        const double totalDb = (double) drive01 * (double) v.driveMaxDb;
        gA = std::pow (10.0, 0.35 * totalDb / 20.0);
        gB = std::pow (10.0, 0.65 * totalDb / 20.0);
    }

    /** Makeup so that a reference sine keeps its RMS through the (mean-removed) nonlinearity. */
    inline float computeMakeup (const Voicing& v, double gA, double gB, double refPeak = 0.25) noexcept
    {
        constexpr int N = 64;
        double s = 0.0, s2 = 0.0;
        for (int k = 0; k < N; ++k)
        {
            const double th = 2.0 * kPi * ((double) k + 0.5) / (double) N;
            const double y = staticTransfer (v, refPeak * std::sin (th), gA, gB);
            s += y; s2 += y * y;
        }
        const double mean = s / N;
        const double var = std::max (s2 / N - mean * mean, 1.0e-12);
        const double rmsIn = refPeak / std::sqrt (2.0);
        return (float) std::min (std::max (rmsIn / std::sqrt (var), 0.02), 50.0);
    }
}
