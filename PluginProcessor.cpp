#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kRefPeak = 0.4f;     // level the drive stage is calibrated around
    constexpr float kRefRms  = 0.125f;   // -18 dBFS RMS: target for auto input level
    constexpr float kGate    = 1.0e-6f;  // mean-square gate (~ -60 dBFS RMS)

    struct Preset { const char* name; float drive, blend, bass, mid, midF, treble, pres; int voice; bool cab; };
    const Preset kPresets[] = {
        { "Clean DI",       1.5f, 30.f,  2.f,  0.f,  700.f,  1.f, 0.f, 0, false },
        { "Tube Warmth",    4.0f, 55.f,  3.f, -1.f,  600.f,  2.f, 2.f, 0, false },
        { "Classic Scoop",  5.5f, 65.f,  5.f, -5.f,  500.f,  4.f, 3.f, 0, true  },
        { "Solid Rock",     6.5f, 75.f,  2.f,  3.f,  800.f,  1.f, 3.f, 1, true  },
        { "Fuzz Grind",     8.0f, 85.f,  1.f,  2.f, 1000.f, -2.f, 0.f, 2, true  },
    };

    inline float shaper (float x, int voice) noexcept
    {
        switch (voice)
        {
            case 0:  { constexpr float bias = 0.25f;                       // tube-ish, asymmetric (even harmonics)
                       return std::tanh (x + bias) - std::tanh (bias); }
            case 1:  { const float x2 = x * x;                              // solid-state: harder knee, symmetric
                       return x / std::sqrt (std::sqrt (1.0f + x2 * x2)); }
            default: return x > 0.f ? std::tanh (2.5f * x)                  // fuzz: lopsided + gritty
                                    : 0.8f * std::tanh (x);
        }
    }

    inline float softLimit (float x) noexcept
    {
        constexpr float t = 0.9f;
        const float a = std::abs (x);
        if (a <= t) return x;
        return std::copysign (t + (1.f - t) * std::tanh ((a - t) / (1.f - t)), x);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BassDriverAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto db = [] { return AudioParameterFloatAttributes().withLabel ("dB"); };

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 },    "Drive",    NormalisableRange<float> (0.f, 10.f, 0.01f), 4.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "blend", 1 },    "Blend",    NormalisableRange<float> (0.f, 100.f, 0.1f), 65.f,
                                                  AudioParameterFloatAttributes().withLabel ("% dirty")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bass", 1 },     "Bass",     NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db()));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mid", 1 },      "Mid",      NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db()));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "midfreq", 1 },  "Mid Freq", NormalisableRange<float> (200.f, 2500.f, 1.f, 0.45f), 700.f,
                                                  AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "treble", 1 },   "Treble",   NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db()));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "presence", 1 }, "Presence", NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db()));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 },    "Output",   NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f, db()));
    l.add (std::make_unique<AudioParameterChoice>(ParameterID { "voice", 1 },    "Voice",    StringArray { "Tube", "Solid State", "Fuzz" }, 0));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "autoin", 1 },   "Auto Input",  true));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "autoout", 1 },  "Auto Output", true));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "cab", 1 },      "Cab Sim",     false));
    return l;
}

BassDriverAudioProcessor::BassDriverAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      oversampling (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
{
    pDrive    = apvts.getRawParameterValue ("drive");
    pBlend    = apvts.getRawParameterValue ("blend");
    pLevel    = apvts.getRawParameterValue ("level");
    pBass     = apvts.getRawParameterValue ("bass");
    pMid      = apvts.getRawParameterValue ("mid");
    pMidFreq  = apvts.getRawParameterValue ("midfreq");
    pTreble   = apvts.getRawParameterValue ("treble");
    pPresence = apvts.getRawParameterValue ("presence");
    pVoice    = apvts.getRawParameterValue ("voice");
    pAutoIn   = apvts.getRawParameterValue ("autoin");
    pAutoOut  = apvts.getRawParameterValue ("autoout");
    pCab      = apvts.getRawParameterValue ("cab");
}

bool BassDriverAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

//==============================================================================
void BassDriverAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };

    for (auto* f : { &lowSplit, &dirtyHP, &dirtyLP, &lowShelf, &midPeak, &highShelf, &presShelf, &cabLP })
    {
        f->prepare (spec);
        f->reset();
    }

    *lowSplit.state = *Coeffs::makeFirstOrderLowPass (sr, 100.0);
    *dirtyHP.state  = *Coeffs::makeFirstOrderHighPass (sr, 30.0);
    *dirtyLP.state  = *Coeffs::makeLowPass (sr, juce::jmin (7500.0, sr * 0.42), 0.6f);

    oversampling.initProcessing ((size_t) maxBlock);
    oversampling.reset();
    latency = (int) std::lround (oversampling.getLatencyInSamples());
    setLatencySamples (latency);

    for (auto* d : { &delayX, &delayH })
    {
        d->setMaximumDelayInSamples (latency + 16);
        d->prepare (spec);
        d->reset();
        d->setDelay ((float) latency);
    }

    workX.setSize (2, maxBlock);
    workH.setSize (2, maxBlock);
    workD.setSize (2, maxBlock);

    cBass = cMid = cMidF = cTreble = cPres = 1e9f; cCab = -1;
    inEnv = outEnv = 0.f;
    gInDb = compDb = 0.f;
    prevGIn = prevDrive = prevMakeup = prevGOut = 1.f;
    prevBlend = pBlend->load() * 0.01f;
}

void BassDriverAudioProcessor::updateToneFilters()
{
    const float bass = pBass->load(), mid = pMid->load(), midF = pMidFreq->load(),
                treble = pTreble->load(), pres = pPresence->load();
    const int cab = pCab->load() > 0.5f ? 1 : 0;
    using juce::Decibels;

    if (bass != cBass)     { *lowShelf.state  = *Coeffs::makeLowShelf  (sr, 100.0, 0.7f, Decibels::decibelsToGain (bass));   cBass = bass; }
    if (mid != cMid || midF != cMidF)
                           { *midPeak.state   = *Coeffs::makePeakFilter (sr, midF, 0.8f, Decibels::decibelsToGain (mid));    cMid = mid; cMidF = midF; }
    if (treble != cTreble) { *highShelf.state = *Coeffs::makeHighShelf (sr, 2500.0, 0.7f, Decibels::decibelsToGain (treble)); cTreble = treble; }
    if (pres != cPres)     { *presShelf.state = *Coeffs::makeHighShelf (sr, 6000.0, 0.7f, Decibels::decibelsToGain (pres));  cPres = pres; }
    if (cab != cCab)       { *cabLP.state     = *Coeffs::makeLowPass   (sr, cab ? juce::jmin (5000.0, sr * 0.42) : juce::jmin (18000.0, sr * 0.45), 0.6f);
                             cCab = cab; }
}

//==============================================================================
void BassDriverAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0) return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, total);

    updateToneFilters();

    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int len = juce::jmin (maxBlock, total - pos);
        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numCh, pos, len);
        processChunk (sub, len);
    }
}

void BassDriverAudioProcessor::processChunk (juce::AudioBuffer<float>& io, int len)
{
    using juce::Decibels;
    const int numCh = io.getNumChannels();
    auto blockOf = [len] (juce::AudioBuffer<float>& b) { return juce::dsp::AudioBlock<float> (b).getSubBlock (0, (size_t) len); };
    auto meanSquare = [&] (const juce::AudioBuffer<float>& b)
    {
        float s = 0.f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* p = b.getReadPointer (ch);
            for (int i = 0; i < len; ++i) s += p[i] * p[i];
        }
        return s / (float) (numCh * len);
    };
    auto envStep = [&] (float& env, float ms)
    {
        const float tau = ms > env ? 0.03f : 0.6f;       // fast attack, slow release
        env += (1.f - std::exp (-(float) len / ((float) sr * tau))) * (ms - env);
    };
    auto smoothCoef = [&] (float tau) { return 1.f - std::exp (-(float) len / ((float) sr * tau)); };

    // --- copy input (mono is duplicated so every stage can run as stereo)
    for (int ch = 0; ch < 2; ++ch)
        workX.copyFrom (ch, 0, io, juce::jmin (ch, numCh - 1), 0, len);

    // --- measure raw input level
    envStep (inEnv, meanSquare (io));
    const bool gateOpen = inEnv > kGate;
    const bool autoIn  = pAutoIn->load()  > 0.5f;
    const bool autoOut = pAutoOut->load() > 0.5f;

    // --- AUTO INPUT: slowly level the incoming signal to a reference so Drive always feels the same
    {
        float target = 0.f;
        if (autoIn && gateOpen)
            target = juce::jlimit (-18.f, 18.f, Decibels::gainToDecibels (kRefRms / std::sqrt (inEnv)));
        else if (autoIn)
            target = gInDb;                                      // hold during silence
        gInDb += (target - gInDb) * smoothCoef (autoIn ? 0.6f : 0.1f);
        const float g = Decibels::decibelsToGain (gInDb);
        workX.applyGainRamp (0, len, prevGIn, g);
        prevGIn = g;
    }

    // --- split: lows (kept clean & tight) / highs (get saturated)
    for (int ch = 0; ch < 2; ++ch) workH.copyFrom (ch, 0, workX, ch, 0, len);
    { auto b = blockOf (workH); lowSplit.process (juce::dsp::ProcessContextReplacing<float> (b)); }   // workH = lows
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* h = workH.getWritePointer (ch);
        auto* x = workX.getReadPointer (ch);
        for (int i = 0; i < len; ++i) h[i] = x[i] - h[i];                                              // workH = highs
    }
    for (int ch = 0; ch < 2; ++ch) workD.copyFrom (ch, 0, workH, ch, 0, len);

    // --- DRIVE stage (4x oversampled, gain-compensated so Drive changes tone, not volume)
    const int voice = juce::jlimit (0, 2, (int) std::lround (pVoice->load()));
    const float gDrive = Decibels::decibelsToGain (pDrive->load() * 4.0f);       // 0..40 dB
    const float a = gDrive * kRefPeak;
    const float m = 0.5f * (std::abs (shaper (a, voice)) + std::abs (shaper (-a, voice)));
    const float makeup = kRefPeak / juce::jmax (m, 1.0e-4f);

    workD.applyGainRamp (0, len, prevDrive, gDrive);
    prevDrive = gDrive;
    {
        auto blk = blockOf (workD);
        auto up = oversampling.processSamplesUp (blk);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* p = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i) p[i] = shaper (p[i], voice);
        }
        oversampling.processSamplesDown (blk);
        dirtyHP.process (juce::dsp::ProcessContextReplacing<float> (blk));   // remove bias DC
        dirtyLP.process (juce::dsp::ProcessContextReplacing<float> (blk));   // tame fizz
    }
    workD.applyGainRamp (0, len, prevMakeup, makeup);
    prevMakeup = makeup;

    // --- latency-align the un-processed paths with the oversampled one
    { auto b = blockOf (workX); delayX.process (juce::dsp::ProcessContextReplacing<float> (b)); }
    { auto b = blockOf (workH); delayH.process (juce::dsp::ProcessContextReplacing<float> (b)); }

    // --- BLEND:  out = lows + (1-b)*highs + b*dirty   ==   x + b*(dirty - highs)
    const float b0 = prevBlend, b1 = pBlend->load() * 0.01f;
    prevBlend = b1;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* x = workX.getWritePointer (ch);
        auto* h = workH.getReadPointer (ch);
        auto* d = workD.getReadPointer (ch);
        for (int i = 0; i < len; ++i)
        {
            const float b = b0 + (b1 - b0) * ((float) i / (float) len);
            x[i] += b * (d[i] - h[i]);
        }
    }

    // --- TONE (post-drive, like a SansAmp) + optional cab voicing
    {
        auto b = blockOf (workX);
        juce::dsp::ProcessContextReplacing<float> ctx (b);
        lowShelf.process (ctx);
        midPeak.process (ctx);
        highShelf.process (ctx);
        presShelf.process (ctx);
        cabLP.process (ctx);
    }

    // --- AUTO OUTPUT: match processed loudness to the loudness that came in
    envStep (outEnv, meanSquare (workX));
    {
        float target = 0.f;
        if (autoOut && gateOpen && outEnv > 1.0e-10f)
            target = juce::jlimit (-24.f, 24.f, 10.f * std::log10 (inEnv / outEnv));
        else if (autoOut)
            target = compDb;                                     // hold during silence
        compDb += (target - compDb) * smoothCoef (autoOut ? 0.3f : 0.1f);
    }
    const float gOut = Decibels::decibelsToGain (compDb + pLevel->load());
    workX.applyGainRamp (0, len, prevGOut, gOut);
    prevGOut = gOut;

    // --- safety soft-limit and write back
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* dst = io.getWritePointer (ch);
        auto* src = workX.getReadPointer (ch);
        for (int i = 0; i < len; ++i) dst[i] = softLimit (src[i]);
    }
}

//==============================================================================
void BassDriverAudioProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

int BassDriverAudioProcessor::getNumPrograms() { return (int) std::size (kPresets); }
const juce::String BassDriverAudioProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, getNumPrograms()) ? kPresets[i].name : "";
}
void BassDriverAudioProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms())) return;
    currentProgram = i;
    const auto& p = kPresets[i];
    setParam ("drive", p.drive);  setParam ("blend", p.blend);
    setParam ("bass", p.bass);    setParam ("mid", p.mid);   setParam ("midfreq", p.midF);
    setParam ("treble", p.treble); setParam ("presence", p.pres);
    setParam ("voice", (float) p.voice);
    setParam ("cab", p.cab ? 1.f : 0.f);
    setParam ("autoin", 1.f);  setParam ("autoout", 1.f);  setParam ("level", 0.f);
}

void BassDriverAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BassDriverAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BassDriverAudioProcessor::createEditor() { return new BassDriverEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BassDriverAudioProcessor(); }
