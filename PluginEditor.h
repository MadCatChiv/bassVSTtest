#pragma once
#include "PluginProcessor.h"

class BassDriverEditor : public juce::AudioProcessorEditor
{
public:
    explicit BassDriverEditor (BassDriverAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);

    BassDriverAudioProcessor& proc;
    Knob drive, blend, bass, mid, midFreq, treble, presence, level;
    juce::ComboBox voice;
    juce::Label voiceLabel;
    juce::ToggleButton autoIn { "Auto Input" }, autoOut { "Auto Output" }, cab { "Cab Sim" };
    std::unique_ptr<ComboAtt> voiceAtt;
    std::unique_ptr<ButtonAtt> autoInAtt, autoOutAtt, cabAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassDriverEditor)
};
