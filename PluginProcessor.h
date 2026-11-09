#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class BassDriverAudioProcessor : public juce::AudioProcessor
{
public:
    BassDriverAudioProcessor();
    ~BassDriverAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bass Driver"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using StereoFilter = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (juce::AudioBuffer<float>& io, int len);
    void updateToneFilters();
    void setParam (const juce::String& id, float plainValue);

    // parameters
    std::atomic<float> *pDrive, *pBlend, *pLevel, *pBass, *pMid, *pMidFreq,
                       *pTreble, *pPresence, *pVoice, *pAutoIn, *pAutoOut, *pCab;

    // DSP
    double sr = 44100.0;
    int maxBlock = 512;
    int latency = 0;

    juce::dsp::Oversampling<float> oversampling;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delayX, delayH;
    StereoFilter lowSplit, dirtyHP, dirtyLP, lowShelf, midPeak, highShelf, presShelf, cabLP;
    juce::AudioBuffer<float> workX, workH, workD;

    // tone cache
    float cBass = 1e9f, cMid = 1e9f, cMidF = 1e9f, cTreble = 1e9f, cPres = 1e9f;
    int cCab = -1;

    // auto-gain state
    float inEnv = 0.f, outEnv = 0.f;
    float gInDb = 0.f, compDb = 0.f;
    float prevGIn = 1.f, prevDrive = 1.f, prevMakeup = 1.f, prevBlend = 0.f, prevGOut = 1.f;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassDriverAudioProcessor)
};
