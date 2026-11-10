#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "BassBiquad.h"
#include "BassVoicings.h"
#include "BassSaturation.h"
#include "BassCompressor.h"
#include "BassLoudness.h"
#include "BassCabSim.h"
#include "BassPresets.h"

class BassDriverAudioProcessor : public juce::AudioProcessor
{
public:
    BassDriverAudioProcessor();
    ~BassDriverAudioProcessor() override = default;

    // --- AudioProcessor ------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Bass Driver"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentFactory; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- workflow: presets, A/B, undo, IR ---------------------------------------------------
    static juce::File userPresetFolder();
    juce::Array<juce::File> listUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const;

    int  getActiveSlot() const noexcept { return activeSlot; }        // 0 = A, 1 = B
    void switchToSlot (int slot);
    void copyActiveToOtherSlot();

    void loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();
    juce::String getImpulseResponseName() const;

    // --- meters (written by audio thread, read by UI) ---------------------------------------
    std::atomic<float> meterInLufs { -70.f }, meterOutLufs { -70.f }, meterGrDb { 0.f };

    juce::UndoManager undoManager;                  // must be declared before apvts
    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kSub = 64;                 // internal control-rate block (~1.3 ms @ 48 kHz)

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processSub (float* const* io, int numCh, int len);
    void applyVoicing (int v);
    void applyCab (int cabKey);
    void updateControlFilters (float bass, float mid, float midF, float treble, float pres);
    void setParam (const juce::String& id, float plainValue);
    void applyFactory (const bd::FactoryPreset& p);
    void restoreFromXml (const juce::XmlElement& e);
    void captureSlot (int slot);
    void recallSlot (int slot);

    // --- parameters ----------------------------------------------------------------------
    std::atomic<float> *pDrive, *pBlend, *pTight, *pComp, *pPeak, *pLevel, *pBass, *pMid, *pMidFreq,
                       *pTreble, *pPresence, *pVoicing, *pAutoIn, *pAutoOut, *pCab, *pCabModel;

    juce::SmoothedValue<float> smDrive, smBlend, smTight, smComp, smPeak, smBass, smMid, smTreble, smPres, smLevel;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smMidF;

    // --- DSP -----------------------------------------------------------------------------
    double sr = 44100.0;
    int latency = 0;
    int numChannels = 2;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::Convolution convolution;
    bd::DualCompressor compressor;
    bd::CabSim cabSim;
    bd::IntDelay delayLow, delayHigh, delayBypass;

    bd::Biquad xoLpA, xoLpB, xoHpA, xoHpB;                       // LR4 crossover (2 cascaded Butterworth)
    bd::Biquad preHp, preMid, preLp, postHp, postLp;             // around the saturator
    bd::Biquad colour, air, lowShelf, midPeak, highShelf, presShelf;
    bd::KWeighting kIn, kOut;
    bd::LoudnessWindow inWin, outWin;
    bd::AdaaState stA[2], stB[2];

    juce::AudioBuffer<float> xBuf, lowBuf, highBuf, dirtyBuf;

    // applied (cached) state
    int appliedVoicing = -1, appliedCabKey = -2;
    int duckState = 0;                                           // 0 idle, 1 fading down, 2 fading up
    float cDrive = -1.f, cXover = -1.f;
    float cBass = 1e9f, cMid = 1e9f, cMidF = 1e9f, cTreble = 1e9f, cPres = 1e9f;
    double gA = 1.0, gB = 1.0;
    float makeup = 1.f;
    float prevGA = 1.f, prevMakeup = 1.f, prevGB = 1.f;
    float gInDb = 0.f, compDb = 0.f;
    float prevGIn = 1.f, prevBlend = 0.f, prevGOut = 1.f;
    std::atomic<bool> irLoaded { false };

    // --- workflow state ------------------------------------------------------------------
    int currentFactory = 0;
    int activeSlot = 0;
    juce::ValueTree slotState[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassDriverAudioProcessor)
};
