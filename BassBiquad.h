// =============================================================================
//  BassBiquad.h  -  allocation-free biquad (RBJ cookbook) with double-precision state.
//
//  WHY NOT juce::dsp::IIR?  JUCE's coefficient factories return heap-allocated
//  ref-counted objects.  We retune filters while parameters are being smoothed
//  (every 64 samples), so we want zero heap traffic on the audio thread.
//
//  WHY DOUBLE STATE?  Low-frequency filters (80 Hz shelf @ 48 kHz, 38 Hz K-weighting
//  high-pass, 55 Hz detector high-pass) have poles very close to z = 1.  In 32-bit float
//  the quantisation noise of a transposed direct form II section grows as 1/(1-|p|), which
//  is audible as a noise floor and "zipper" under modulation.  Doubles make this a
//  non-issue and cost almost nothing at our channel count.
// =============================================================================
#pragma once
#include <algorithm>
#include <iterator>
#include <cmath>

namespace bd
{
    constexpr double kPi = 3.14159265358979323846;

    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // a0 normalised to 1
    };

    namespace detail
    {
        inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
        {
            const double inv = 1.0 / a0;
            return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
        }
        inline double clampFreq (double fs, double f) noexcept { return std::min (std::max (f, 5.0), 0.49 * fs); }
    }

    inline BiquadCoeffs makeLowPass (double fs, double f, double q) noexcept
    {
        f = detail::clampFreq (fs, f);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return detail::normalise ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }

    inline BiquadCoeffs makeHighPass (double fs, double f, double q) noexcept
    {
        f = detail::clampFreq (fs, f);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return detail::normalise ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }

    inline BiquadCoeffs makePeak (double fs, double f, double q, double gainDb) noexcept
    {
        f = detail::clampFreq (fs, f);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return detail::normalise (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    inline BiquadCoeffs makeLowShelf (double fs, double f, double q, double gainDb) noexcept
    {
        f = detail::clampFreq (fs, f);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double beta = 2.0 * std::sqrt (A) * al;
        return detail::normalise (A * ((A + 1) - (A - 1) * c + beta),
                                  2 * A * ((A - 1) - (A + 1) * c),
                                  A * ((A + 1) - (A - 1) * c - beta),
                                  (A + 1) + (A - 1) * c + beta,
                                  -2 * ((A - 1) + (A + 1) * c),
                                  (A + 1) + (A - 1) * c - beta);
    }

    inline BiquadCoeffs makeHighShelf (double fs, double f, double q, double gainDb) noexcept
    {
        f = detail::clampFreq (fs, f);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double beta = 2.0 * std::sqrt (A) * al;
        return detail::normalise (A * ((A + 1) + (A - 1) * c + beta),
                                  -2 * A * ((A - 1) + (A + 1) * c),
                                  A * ((A + 1) + (A - 1) * c - beta),
                                  (A + 1) - (A - 1) * c + beta,
                                  2 * ((A - 1) - (A + 1) * c),
                                  (A + 1) - (A - 1) * c - beta);
    }

    /** Two-channel transposed direct-form-II biquad.  TDF-II tolerates coefficient changes
        between blocks far better than DF-I (state is a "charge", not a signal history). */
    struct Biquad
    {
        BiquadCoeffs c;
        double z1[2] { 0, 0 }, z2[2] { 0, 0 };

        void set (const BiquadCoeffs& n) noexcept { c = n; }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

        inline float process (int ch, float x) noexcept
        {
            const double in  = (double) x;
            const double out = c.b0 * in + z1[ch];
            z1[ch] = c.b1 * in - c.a1 * out + z2[ch];
            z2[ch] = c.b2 * in - c.a2 * out;
            return (float) out;
        }
    };

    /** Integer delay line used to latency-align the clean paths with the oversampled path. */
    struct IntDelay
    {
        static constexpr int kSize = 512;               // power of two, > max oversampler latency
        float buf[2][kSize] {};
        int pos[2] { 0, 0 };
        int delay = 0;

        void reset() noexcept { for (auto& b : buf) std::fill (std::begin (b), std::end (b), 0.f); pos[0] = pos[1] = 0; }
        void setDelay (int d) noexcept { delay = std::min (std::max (d, 0), kSize - 1); }

        inline void process (int ch, float* data, int n) noexcept
        {
            int p = pos[ch];
            for (int i = 0; i < n; ++i)
            {
                buf[ch][p] = data[i];
                data[i] = buf[ch][(p - delay) & (kSize - 1)];
                p = (p + 1) & (kSize - 1);
            }
            pos[ch] = p;
        }
    };
}
