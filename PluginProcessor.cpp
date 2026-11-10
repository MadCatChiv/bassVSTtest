// =============================================================================
//  Bass Driver - signal flow (per 64-sample control block)
//
//   in -> K-weighted loudness measure (raw input)
//      -> AUTO INPUT  (slow loudness levelling to -20 LUFS: Drive feels identical for any bass)
//      -> DUAL COMPRESSOR (opto leveller + peak catcher, sidechain high-passed)
//      -> LR4 CROSSOVER ---- lows (always clean, tight low end) ----------------------+
//                       \--- highs -> pre-emphasis (HP / focus / LP)                    |
//                                  -> 4x oversampling [ stage A tanh -> stage B clipper, both ADAA ]
//                                  -> DC block, fizz LP, level-compensation makeup      |
//                                  -> BLEND with the delay-aligned clean highs ---------+
//      -> voicing colour + air -> BASS / MID / TREBLE / PRESENCE (analogue-style EQ)
//      -> CABINET (parametric model or user IR convolution)
//      -> K-weighted loudness measure (processed)  -> AUTO OUTPUT (match loudness)
//      -> output trim -> click-free structural-change ducking -> soft safety limiter -> out
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kTargetLufs = -20.0f;     // level Auto Input aims for (drive/comp thresholds are tuned to it)

    inline float softLimit (float x) noexcept
    {
        // Transparent below 0.9, smooth tanh knee up to 1.0: protects converters/hosts from
        // a runaway setting without hard-clipping edges.
        constexpr float t = 0.9f;
        const float a = std::abs (x);
        if (a <= t) return x;
        return std::copysign (t + (1.f - t) * std::tanh ((a - t) / (1.f - t)), x);
    }

    inline bool differs (float a, float b) noexcept { return std::abs (a - b) > 1.0e-4f; }

    /** One-pole slew of a dB value toward a target with separate time constants for falling
        (gain decreasing) and rising gain.  Optional dead-zone: movements smaller than 0.3 dB
        are ignored, which stops the auto-gain from continuously micro-modulating the signal
        (that modulation is itself a form of distortion on a sustained bass note). */
    inline float slewDb (float cur, float target, int len, double fs, float tauDown, float tauUp, bool deadZone) noexcept
    {
        float err = target - cur;
        if (deadZone) err = err > 0.f ? std::max (0.f, err - 0.3f) : std::min (0.f, err + 0.3f);
        const float tau = err < 0.f ? tauDown : tauUp;
        return cur + err * (1.f - std::exp (-(float) len / ((float) fs * tau)));
    }

    inline void ramp (float* d, int n, float g0, float g1) noexcept
    {
        const float step = (g1 - g0) / (float) std::max (n, 1);
        float g = g0;
        for (int i = 0; i < n; ++i) { g += step; d[i] *= g; }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BassDriverAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto lbl = [] (const char* s) { return AudioParameterFloatAttributes().withLabel (s); };
    auto F = [] (const char* id, const char* name, float lo, float hi, float step, float def, AudioParameterFloatAttributes a, float skew = 1.f)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step, skew), def, a);
    };

    l.add (F ("drive",    "Drive",    0.f, 10.f, 0.01f, 4.5f, lbl ("")));
    l.add (F ("blend",    "Blend",    0.f, 100.f, 0.1f, 65.f, lbl ("% dirty")));
    l.add (F ("tight",    "Tight",    0.f, 100.f, 0.1f, 50.f, lbl ("%")));
    l.add (F ("comp",     "Comp",     0.f, 10.f, 0.01f, 0.f, lbl ("")));
    l.add (F ("peak",     "Peak",     0.f, 10.f, 0.01f, 0.f, lbl ("")));
    l.add (F ("bass",     "Bass",     -12.f, 12.f, 0.1f, 0.f, lbl ("dB")));
    l.add (F ("mid",      "Mid",      -12.f, 12.f, 0.1f, 0.f, lbl ("dB")));
    l.add (F ("midfreq",  "Mid Freq", 200.f, 2500.f, 1.f, 700.f, lbl ("Hz"), 0.45f));
    l.add (F ("treble",   "Treble",   -12.f, 12.f, 0.1f, 0.f, lbl ("dB")));
    l.add (F ("presence", "Presence", -12.f, 12.f, 0.1f, 0.f, lbl ("dB")));
    l.add (F ("level",    "Output",   -24.f, 12.f, 0.1f, 0.f, lbl ("dB")));

    StringArray voicings; for (auto& v : bd::kVoicings) voicings.add (v.name);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voicing", 1 }, "Voicing", voicings, 0));

    StringArray cabs; for (auto& c : bd::kCabs) cabs.add (c.name);
    cabs.add ("User IR");
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "cab", 1 },      "Cab Sim",     false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cabmodel", 1 }, "Cab Model",   cabs, 1));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autoin", 1 },   "Auto Input",  true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autoout", 1 },  "Auto Output", true));
    return l;
}

BassDriverAudioProcessor::BassDriverAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "PARAMS", createLayout())
{
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pDrive = P ("drive");  pBlend = P ("blend");  pTight = P ("tight");  pComp = P ("comp");  pPeak = P ("peak");
    pBass = P ("bass");    pMid = P ("mid");      pMidFreq = P ("midfreq");
    pTreble = P ("treble"); pPresence = P ("presence"); pLevel = P ("level");
    pVoicing = P ("voicing"); pCab = P ("cab"); pCabModel = P ("cabmodel");
    pAutoIn = P ("autoin"); pAutoOut = P ("autoout");
    undoManager.setMaxNumberOfStoredUnits (30000, 100);
}

bool BassDriverAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

//==============================================================================
void BassDriverAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    numChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());

    // 4x oversampling (two cascaded linear-phase half-band FIR stages) + first-order ADAA in the
    // saturator.  Measured on a worst-case 2.9 kHz tone at heavy drive, 4x+ADAA brings aliasing of the
    // hard-clipping voicings from about -39 dB (2x+ADAA) to about -61 dB relative to the fundamental,
    // for roughly +1.5% of one core.  Linear phase + integer latency => the clean bands can be
    // delay-matched exactly, so blending clean and driven paths never comb-filters.
    // Built with the real bus channel count: mono tracks cost half the CPU, and the oversampler's
    // integer-latency delay line requires block channel count == configured channel count.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) kSub);
    oversampling->reset();
    latency = (int) std::lround (oversampling->getLatencyInSamples());
    setLatencySamples (latency);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kSub, 2 };
    convolution.prepare (spec);
    convolution.reset();

    compressor.prepare (sr);
    cabSim.reset();
    for (auto* d : { &delayLow, &delayHigh, &delayBypass }) { d->reset(); d->setDelay (latency); }
    for (auto* b : { &xoLpA, &xoLpB, &xoHpA, &xoHpB, &preHp, &preMid, &preLp, &postHp, &postLp,
                     &colour, &air, &lowShelf, &midPeak, &highShelf, &presShelf })
        b->reset();
    kIn.prepare (sr);
    kOut.prepare (sr);
    inWin.prepare ((int) std::lround (0.6 * sr / kSub), (int) std::lround (3.0 * sr / kSub));
    outWin.prepare ((int) std::lround (0.6 * sr / kSub), (int) std::lround (0.6 * sr / kSub));
    for (auto& s : stA) s.reset();
    for (auto& s : stB) s.reset();

    for (auto* b : { &xBuf, &lowBuf, &highBuf, &dirtyBuf }) { b->setSize (2, kSub); b->clear(); }

    auto initSm = [this] (auto& sm, float v, double secs) { sm.reset (sr, secs); sm.setCurrentAndTargetValue (v); };
    initSm (smDrive, pDrive->load() * 0.1f, 0.03);   initSm (smBlend, pBlend->load() * 0.01f, 0.03);
    initSm (smTight, pTight->load() * 0.01f, 0.05);  initSm (smComp, pComp->load() * 0.1f, 0.03);
    initSm (smPeak, pPeak->load() * 0.1f, 0.03);     initSm (smBass, pBass->load(), 0.03);
    initSm (smMid, pMid->load(), 0.03);              initSm (smMidF, pMidFreq->load(), 0.05);
    initSm (smTreble, pTreble->load(), 0.03);        initSm (smPres, pPresence->load(), 0.03);
    initSm (smLevel, pLevel->load(), 0.03);

    appliedVoicing = -1; appliedCabKey = -2; duckState = 0;
    cDrive = cXover = -1.f;
    gInDb = compDb = 0.f;
    prevGIn = prevGOut = 1.f; prevGA = prevMakeup = prevGB = 1.f;
    prevBlend = pBlend->load() * 0.01f;
}

//==============================================================================
void BassDriverAudioProcessor::applyVoicing (int v)
{
    appliedVoicing = v;
    const auto& V = bd::kVoicings[v];
    const double nyq = 0.45 * sr;

    preHp.set  (bd::makeHighPass (sr, V.preHpHz, 0.7071));
    preMid.set (bd::makePeak (sr, V.preMidHz, V.preMidQ, V.preMidDb));
    preLp.set  (bd::makeLowPass (sr, std::min ((double) V.preLpHz, nyq), 0.6));
    postHp.set (bd::makeHighPass (sr, 25.0, 0.7071));                       // removes bias DC from the Tube shaper
    postLp.set (bd::makeLowPass (sr, std::min ((double) V.postLpHz, nyq), 0.6));
    colour.set (bd::makePeak (sr, V.colourHz, V.colourQ, V.colourDb));
    air.set    (bd::makeHighShelf (sr, 4500.0, 0.6, V.airDb));

    cDrive = cXover = -1.f;                                                 // force recompute
    cBass = cMid = cMidF = cTreble = cPres = 1e9f;
    for (auto& s : stA) s.reset();
    for (auto& s : stB) s.reset();
}

void BassDriverAudioProcessor::applyCab (int key)
{
    appliedCabKey = key;
    if (key > 0 && key - 1 < bd::kNumBuiltInCabs)
    {
        cabSim.configure (key - 1, sr);
        cabSim.reset();
    }
}

void BassDriverAudioProcessor::updateControlFilters (float bass, float mid, float midF, float treble, float pres)
{
    const auto& V = bd::kVoicings[appliedVoicing];
    // "Analogue" EQ behaviour:
    //  * shelves use a low Q (0.5-0.6) = broad, gentle ~6 dB/oct slopes like a Baxandall/passive stack;
    //  * the mid band is PROPORTIONAL-Q: narrow-ish only when pushed hard, broad when nudged -
    //    this is how most hardware parametric/graphic stages behave and why they sound "musical";
    //  * corner frequencies come from the voicing (an SVT-style bass pivots lower than a DI-style one).
    if (differs (bass, cBass))     { lowShelf.set  (bd::makeLowShelf  (sr, V.bassHz, 0.55, bass));         cBass = bass; }
    if (differs (mid, cMid) || differs (midF, cMidF))
                                   { midPeak.set   (bd::makePeak (sr, midF, 0.55 + 0.07 * std::abs (mid), mid)); cMid = mid; cMidF = midF; }
    if (differs (treble, cTreble)) { highShelf.set (bd::makeHighShelf (sr, V.trebleHz, 0.5, treble));      cTreble = treble; }
    if (differs (pres, cPres))     { presShelf.set (bd::makeHighShelf (sr, V.presenceHz, 0.6, pres));      cPres = pres; }
}

//==============================================================================
void BassDriverAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), numChannels);   // never more than the oversampler was built for
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0 || oversampling == nullptr) return;

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, total);

    smDrive.setTargetValue (pDrive->load() * 0.1f);   smBlend.setTargetValue (pBlend->load() * 0.01f);
    smTight.setTargetValue (pTight->load() * 0.01f);  smComp.setTargetValue (pComp->load() * 0.1f);
    smPeak.setTargetValue (pPeak->load() * 0.1f);     smBass.setTargetValue (pBass->load());
    smMid.setTargetValue (pMid->load());              smMidF.setTargetValue (pMidFreq->load());
    smTreble.setTargetValue (pTreble->load());        smPres.setTargetValue (pPresence->load());
    smLevel.setTargetValue (pLevel->load());

    for (int pos = 0; pos < total; pos += kSub)
    {
        const int len = juce::jmin (kSub, total - pos);
        float* ch[2] { buffer.getWritePointer (0) + pos, numCh > 1 ? buffer.getWritePointer (1) + pos : nullptr };
        processSub (ch, numCh, len);
    }
}

void BassDriverAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Latency-compensated bypass: the dry signal is delayed by exactly our reported latency so
    // toggling bypass never shifts timing against other tracks.
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    for (int ch = 0; ch < numCh; ++ch) delayBypass.process (ch, buffer.getWritePointer (ch), buffer.getNumSamples());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
}

void BassDriverAudioProcessor::processSub (float* const* io, int numCh, int len)
{
    using juce::Decibels;

    // ---- advance parameter smoothers one control step ---------------------------------------------
    const float drive01 = smDrive.skip (len),  blend = smBlend.skip (len),  tight01 = smTight.skip (len);
    const float comp01  = smComp.skip (len),   peak01 = smPeak.skip (len);
    const float bass = smBass.skip (len), mid = smMid.skip (len), midF = smMidF.skip (len);
    const float treble = smTreble.skip (len), pres = smPres.skip (len), levelDb = smLevel.skip (len);

    // ---- structural changes (voicing / cab) are applied between a fade-out and a fade-in ----------
    //      Switching coefficient sets and clipper curves mid-signal would click; a ~1.3 ms duck
    //      on each side makes it inaudible at negligible cost.
    const int wantVoicing = juce::jlimit (0, bd::kNumVoicings - 1, (int) std::lround (pVoicing->load()));
    const int wantCab = pCab->load() > 0.5f ? 1 + juce::jlimit (0, bd::kCabUserIR, (int) std::lround (pCabModel->load())) : 0;
    float duckStart = 1.f, duckEnd = 1.f;
    if (appliedVoicing < 0)
    {
        applyVoicing (wantVoicing); applyCab (wantCab);
    }
    else if (duckState == 1)
    {
        applyVoicing (wantVoicing); applyCab (wantCab);
        duckState = 0; duckStart = 0.f; duckEnd = 1.f;
    }
    else if (wantVoicing != appliedVoicing || wantCab != appliedCabKey)
    {
        duckState = 1; duckStart = 1.f; duckEnd = 0.f;
    }
    const auto& V = bd::kVoicings[appliedVoicing];

    float* x[2]  { xBuf.getWritePointer (0),    xBuf.getWritePointer (1) };
    float* lo[2] { lowBuf.getWritePointer (0),  lowBuf.getWritePointer (1) };
    float* hi[2] { highBuf.getWritePointer (0), highBuf.getWritePointer (1) };
    float* dt[2] { dirtyBuf.getWritePointer (0), dirtyBuf.getWritePointer (1) };

    // ---- 1. measure RAW input loudness (K-weighted) ------------------------------------------------
    double msIn = 0.0;
    for (int c = 0; c < numCh; ++c)
    {
        double s = 0.0;
        for (int i = 0; i < len; ++i) { const float k = kIn.process (c, io[c][i]); s += (double) k * k; }
        msIn += s / len;
    }
    inWin.push (msIn);
    const float inShort = inWin.lufsShort(), inLong = inWin.lufsLong();
    const bool gateOpen = inShort > -62.f;           // BS.1770 absolute gate-ish: hold gains during silence
    const bool autoIn = pAutoIn->load() > 0.5f, autoOut = pAutoOut->load() > 0.5f;

    // ---- 2. AUTO INPUT: very slow (1.5 s down / 3 s up) leveller on 3 s loudness -----------------------
    //      Slow enough that it can never act as a compressor on note dynamics.
    {
        const float target = autoIn ? (gateOpen ? juce::jlimit (-18.f, 18.f, kTargetLufs - inLong) : gInDb) : 0.f;
        gInDb = slewDb (gInDb, target, len, sr, autoIn ? 1.5f : 0.1f, autoIn ? 3.0f : 0.1f, autoIn);
    }
    const float gIn = Decibels::decibelsToGain (gInDb);
    for (int c = 0; c < numCh; ++c)
    {
        for (int i = 0; i < len; ++i) x[c][i] = io[c][i];
        ramp (x[c], len, prevGIn, gIn);
    }
    prevGIn = gIn;

    // ---- 3. dual compressor -----------------------------------------------------------------------
    const float grDb = compressor.process (x, numCh, len, comp01, peak01);

    // ---- 4. LR4 crossover: flat magnitude sum, phase-coherent bands --------------------------------
    //      4th-order Linkwitz-Riley (two cascaded Butterworth 2nd-order sections per band).  Unlike
    //      the earlier 1st-order split (-6 dB/oct skirts), the highs are cleanly isolated, so low notes
    //      are not intermodulated; the sum of the bands is an all-pass, so blend=0 is transparent.
    const double xo = (double) V.xoverHz * std::pow (2.0, ((double) tight01 - 0.5) * 2.0);   // Tight: x0.5 .. x2
    if (std::abs ((float) xo - cXover) > 0.05f)
    {
        const auto lp = bd::makeLowPass (sr, xo, 0.70710678), hp = bd::makeHighPass (sr, xo, 0.70710678);
        xoLpA.set (lp); xoLpB.set (lp); xoHpA.set (hp); xoHpB.set (hp);
        cXover = (float) xo;
    }
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < len; ++i)
        {
            const float s = x[c][i];
            lo[c][i] = xoLpB.process (c, xoLpA.process (c, s));
            hi[c][i] = xoHpB.process (c, xoHpA.process (c, s));
        }

    // ---- 5. saturation of the high band --------------------------------------------------------------
    const bool dirtyActive = prevBlend > 1.0e-4f || blend > 1.0e-4f;     // skip all heavy work at blend = 0
    if (dirtyActive)
    {
        if (differs (drive01, cDrive))
        {
            bd::driveGains (V, drive01, gA, gB);
            makeup = bd::computeMakeup (V, gA, gB);
            cDrive = drive01;
        }

        for (int c = 0; c < numCh; ++c)
        {
            for (int i = 0; i < len; ++i)
            {
                float s = hi[c][i];
                s = preHp.process (c, s);          // tighten: keep low-mid mud out of the clipper
                s = preMid.process (c, s);         // focus: where the grit lives
                s = preLp.process (c, s);          // band-limit: less energy above Nyquist/2 to alias
                dt[c][i] = s;
            }
            ramp (dt[c], len, prevGA, (float) gA);
        }
        prevGA = (float) gA;

        if (numCh < numChannels) std::fill (dt[1], dt[1] + len, 0.f);      // defensive: host gave fewer channels than the bus
        juce::dsp::AudioBlock<float> blk (dt, (size_t) numChannels, (size_t) len);
        auto up = oversampling->processSamplesUp (blk);
        for (int c = 0; c < numCh; ++c)
            bd::saturateBlock (up.getChannelPointer ((size_t) c), (int) up.getNumSamples(), stA[c], stB[c], V, (double) prevGB, gB);
        prevGB = (float) gB;
        oversampling->processSamplesDown (blk);

        for (int c = 0; c < numCh; ++c)
        {
            for (int i = 0; i < len; ++i) dt[c][i] = postLp.process (c, postHp.process (c, dt[c][i]));
            ramp (dt[c], len, prevMakeup, makeup);
        }
        prevMakeup = makeup;
    }

    // ---- 6. latency-align clean bands with the oversampled path, then BLEND ---------------------------
    for (int c = 0; c < numCh; ++c) { delayLow.process (c, lo[c], len); delayHigh.process (c, hi[c], len); }

    //   out = lows + (1-b)*highs + b*dirty      (lows are never blended: low end stays tight and mono-compatible)
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < len; ++i)
        {
            const float b = prevBlend + (blend - prevBlend) * (float) (i + 1) / (float) len;
            const float h = hi[c][i];
            x[c][i] = lo[c][i] + h + b * ((dirtyActive ? dt[c][i] : h) - h);
        }
    prevBlend = blend;

    // ---- 7. voicing colour + tone stack ----------------------------------------------------------------
    updateControlFilters (bass, mid, midF, treble, pres);
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < len; ++i)
        {
            float s = x[c][i];
            s = colour.process (c, s);  s = air.process (c, s);
            s = lowShelf.process (c, s); s = midPeak.process (c, s);
            s = highShelf.process (c, s); s = presShelf.process (c, s);
            x[c][i] = s;
        }

    // ---- 8. cabinet -----------------------------------------------------------------------------------------
    if (appliedCabKey > 0)
    {
        if (appliedCabKey - 1 == bd::kCabUserIR)
        {
            if (irLoaded.load())
            {
                juce::dsp::AudioBlock<float> blk (x, (size_t) numCh, (size_t) len);
                juce::dsp::ProcessContextReplacing<float> ctx (blk);
                convolution.process (ctx);
            }
        }
        else cabSim.process (x, numCh, len);
    }

    // ---- 9. measure PROCESSED loudness, AUTO OUTPUT ----------------------------------------------------
    //      Open-loop: the output is measured BEFORE its own compensation gain, so there is no feedback
    //      loop to oscillate.  The saturator's makeup already removes most of the level change
    //      instantly; this stage only trims what remains (EQ, compressor, cab) over ~0.3-0.6 s.
    double msOut = 0.0;
    for (int c = 0; c < numCh; ++c)
    {
        double s = 0.0;
        for (int i = 0; i < len; ++i) { const float k = kOut.process (c, x[c][i]); s += (double) k * k; }
        msOut += s / len;
    }
    outWin.push (msOut);
    const float outShort = outWin.lufsShort();
    {
        const float target = autoOut ? (gateOpen ? juce::jlimit (-24.f, 24.f, inShort - outShort) : compDb) : 0.f;
        compDb = slewDb (compDb, target, len, sr, autoOut ? 0.25f : 0.1f, autoOut ? 0.6f : 0.1f, autoOut);
    }

    // ---- 10. output gain, ducking, safety limiter, write back ---------------------------------------------
    const float gOut = Decibels::decibelsToGain (compDb + levelDb);
    bool bad = false;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) (i + 1) / (float) len;
            const float g = (prevGOut + (gOut - prevGOut) * t) * (duckStart + (duckEnd - duckStart) * t);
            const float y = softLimit (x[c][i] * g);
            if (! std::isfinite (y)) { bad = true; io[c][i] = 0.f; }
            else io[c][i] = y;
        }
    prevGOut = gOut;

    if (bad)   // last line of defence: never let a NaN live in an IIR state
    {
        for (auto* b : { &xoLpA, &xoLpB, &xoHpA, &xoHpB, &preHp, &preMid, &preLp, &postHp, &postLp,
                         &colour, &air, &lowShelf, &midPeak, &highShelf, &presShelf })
            b->reset();
        compressor.reset(); cabSim.reset(); kIn.prepare (sr); kOut.prepare (sr);
        for (auto& s : stA) s.reset();
        for (auto& s : stB) s.reset();
        oversampling->reset();
        gInDb = compDb = 0.f;
    }

    meterInLufs.store (inShort);
    meterOutLufs.store (outShort + compDb);
    meterGrDb.store (grDb);
}

//==============================================================================
//  Presets, A/B, state
//==============================================================================
void BassDriverAudioProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

int BassDriverAudioProcessor::getNumPrograms() { return bd::kNumFactoryPresets; }
const juce::String BassDriverAudioProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, bd::kNumFactoryPresets) ? bd::kFactoryPresets[i].name : "";
}
void BassDriverAudioProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, bd::kNumFactoryPresets)) return;
    currentFactory = i;
    undoManager.beginNewTransaction();
    applyFactory (bd::kFactoryPresets[i]);
}

void BassDriverAudioProcessor::applyFactory (const bd::FactoryPreset& p)
{
    setParam ("drive", p.drive);   setParam ("blend", p.blend);   setParam ("tight", p.tight);
    setParam ("comp", p.comp);     setParam ("peak", p.peak);
    setParam ("bass", p.bass);     setParam ("mid", p.mid);       setParam ("midfreq", p.midFreq);
    setParam ("treble", p.treble); setParam ("presence", p.presence);
    setParam ("voicing", (float) p.voicing);
    setParam ("cab", p.cab ? 1.f : 0.f);  setParam ("cabmodel", (float) p.cabModel);
    setParam ("autoin", 1.f);      setParam ("autoout", 1.f);     setParam ("level", 0.f);
    apvts.state.setProperty ("presetName", p.name, nullptr);
}

juce::String BassDriverAudioProcessor::getCurrentPresetName() const
{
    return apvts.state.getProperty ("presetName", "Init").toString();
}

juce::File BassDriverAudioProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("BassTools").getChildFile ("Bass Driver").getChildFile ("Presets");
}

juce::Array<juce::File> BassDriverAudioProcessor::listUserPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.bdpreset");
    files.sort();
    return files;
}

bool BassDriverAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory().wasOk()) return false;
    apvts.state.setProperty ("presetName", clean, nullptr);
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (folder.getChildFile (clean + ".bdpreset"));
    return false;
}

/** Applies parameter values from a state XML by setting the parameters (so every change is
    undoable and reaches the host as proper automation-safe parameter changes). */
static void applyParamsFromXml (BassDriverAudioProcessor& proc, const juce::XmlElement& e)
{
    bool hasNewVoicing = false;
    for (auto* c : e.getChildIterator()) if (c->getStringAttribute ("id") == "voicing") hasNewVoicing = true;

    for (auto* c : e.getChildIterator())
    {
        const auto id = c->getStringAttribute ("id");
        const float v = (float) c->getDoubleAttribute ("value");
        if (id == "voice" && ! hasNewVoicing)           // legacy v1 state: Tube / Solid State / Fuzz
        {
            static const int legacyMap[3] { 1, 3, 4 };  // -> Tube Stack / Grind / Fuzz
            if (auto* p = proc.apvts.getParameter ("voicing"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) legacyMap[juce::jlimit (0, 2, (int) std::lround (v))]));
        }
        else if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
}

bool BassDriverAudioProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    undoManager.beginNewTransaction();
    applyParamsFromXml (*this, *xml);
    apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    const auto ir = xml->getStringAttribute ("irPath");
    if (ir.isNotEmpty() && juce::File (ir).existsAsFile()) loadImpulseResponse (juce::File (ir));
    return true;
}

void BassDriverAudioProcessor::captureSlot (int slot) { slotState[slot] = apvts.copyState(); }

void BassDriverAudioProcessor::recallSlot (int slot)
{
    if (! slotState[slot].isValid()) return;
    if (auto xml = slotState[slot].createXml())
    {
        applyParamsFromXml (*this, *xml);
        apvts.state.setProperty ("presetName", xml->getStringAttribute ("presetName", "Init"), nullptr);
    }
}

void BassDriverAudioProcessor::switchToSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot) return;
    captureSlot (activeSlot);
    if (! slotState[slot].isValid()) slotState[slot] = slotState[activeSlot].createCopy();
    activeSlot = slot;
    recallSlot (slot);
}

void BassDriverAudioProcessor::copyActiveToOtherSlot()
{
    slotState[1 - activeSlot] = apvts.copyState();
}

void BassDriverAudioProcessor::loadImpulseResponse (const juce::File& f)
{
    convolution.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes,
                                     (size_t) (sr * 0.5),            // cap at 500 ms: bounds CPU, cabs decay well before that
                                     juce::dsp::Convolution::Normalise::yes);
    irLoaded = true;
    apvts.state.setProperty ("irPath", f.getFullPathName(), nullptr);
}

void BassDriverAudioProcessor::clearImpulseResponse()
{
    irLoaded = false;
    apvts.state.removeProperty ("irPath", nullptr);
}

juce::String BassDriverAudioProcessor::getImpulseResponseName() const
{
    const auto p = apvts.state.getProperty ("irPath").toString();
    return p.isEmpty() || ! irLoaded.load() ? juce::String() : juce::File (p).getFileNameWithoutExtension();
}

void BassDriverAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto root = std::make_unique<juce::XmlElement> ("BassDriverState");
    root->setAttribute ("version", 2);
    root->setAttribute ("activeSlot", activeSlot);
    if (auto cur = apvts.copyState().createXml()) root->addChildElement (cur.release());
    for (int s = 0; s < 2; ++s)
        if (slotState[s].isValid())
            if (auto xml = slotState[s].createXml())
                root->createNewChildElement (s == 0 ? "SlotA" : "SlotB")->addChildElement (xml.release());
    copyXmlToBinary (*root, dest);
}

void BassDriverAudioProcessor::restoreFromXml (const juce::XmlElement& e)
{
    // Full project recall (not an undoable edit).  Handles v1 sessions saved by the first
    // release, where "voice" held 0/1/2 and "voicing"/"comp"/"tight"/... did not exist.
    int legacyVoice = -1;
    if (e.getChildByAttribute ("id", "voicing") == nullptr)
        if (auto* c = e.getChildByAttribute ("id", "voice")) legacyVoice = (int) std::lround (c->getDoubleAttribute ("value"));

    apvts.replaceState (juce::ValueTree::fromXml (e));

    if (legacyVoice >= 0)
    {
        static const int legacyMap[3] { 1, 3, 4 };
        setParam ("voicing", (float) legacyMap[juce::jlimit (0, 2, legacyVoice)]);
    }
    const auto ir = apvts.state.getProperty ("irPath").toString();
    if (ir.isNotEmpty() && juce::File (ir).existsAsFile()) loadImpulseResponse (juce::File (ir));
}

void BassDriverAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;

    if (xml->hasTagName ("BassDriverState"))
    {
        activeSlot = juce::jlimit (0, 1, xml->getIntAttribute ("activeSlot", 0));
        for (int s = 0; s < 2; ++s)
            if (auto* se = xml->getChildByName (s == 0 ? "SlotA" : "SlotB"))
                if (auto* c = se->getFirstChildElement()) slotState[s] = juce::ValueTree::fromXml (*c);
        if (auto* params = xml->getChildByName (apvts.state.getType())) restoreFromXml (*params);
    }
    else if (xml->hasTagName (apvts.state.getType()))
        restoreFromXml (*xml);                                            // v1 sessions
}

juce::AudioProcessorEditor* BassDriverAudioProcessor::createEditor() { return new BassDriverEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BassDriverAudioProcessor(); }
