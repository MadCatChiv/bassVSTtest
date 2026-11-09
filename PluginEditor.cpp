#include "PluginEditor.h"

BassDriverEditor::BassDriverEditor (BassDriverAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addKnob (drive,    "drive",    "DRIVE");
    addKnob (blend,    "blend",    "BLEND");
    addKnob (bass,     "bass",     "BASS");
    addKnob (mid,      "mid",      "MID");
    addKnob (midFreq,  "midfreq",  "MID FREQ");
    addKnob (treble,   "treble",   "TREBLE");
    addKnob (presence, "presence", "PRESENCE");
    addKnob (level,    "level",    "OUTPUT");

    voice.addItemList (proc.apvts.getParameter ("voice")->getAllValueStrings(), 1);
    addAndMakeVisible (voice);
    voiceAtt = std::make_unique<ComboAtt> (proc.apvts, "voice", voice);
    voiceLabel.setText ("VOICE", juce::dontSendNotification);
    voiceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voiceLabel);

    for (auto* b : { &autoIn, &autoOut, &cab }) addAndMakeVisible (*b);
    autoInAtt  = std::make_unique<ButtonAtt> (proc.apvts, "autoin",  autoIn);
    autoOutAtt = std::make_unique<ButtonAtt> (proc.apvts, "autoout", autoOut);
    cabAtt     = std::make_unique<ButtonAtt> (proc.apvts, "cab",     cab);

    setSize (760, 300);
}

void BassDriverEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void BassDriverEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c1f));
    g.setColour (juce::Colour (0xffff7a1a));
    g.setFont (juce::FontOptions (22.f, juce::Font::bold));
    g.drawText ("BASS DRIVER", 20, 8, 300, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.f));
    g.drawText ("preamp / DI-style driver with auto gain matching", 20, 32, 400, 16, juce::Justification::centredLeft);
}

void BassDriverEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto top = area.removeFromTop (52);
    auto bottom = area.removeFromBottom (36);

    // bottom strip
    auto strip = bottom.reduced (4);
    voiceLabel.setBounds (strip.removeFromLeft (60));
    voice.setBounds (strip.removeFromLeft (130).reduced (4, 2));
    strip.removeFromLeft (20);
    autoIn.setBounds (strip.removeFromLeft (120));
    autoOut.setBounds (strip.removeFromLeft (130));
    cab.setBounds (strip.removeFromLeft (100));

    // knob row
    Knob* knobs[] = { &drive, &blend, &bass, &mid, &midFreq, &treble, &presence, &level };
    const int w = area.getWidth() / 8;
    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell.reduced (2));
    }
    juce::ignoreUnused (top);
}
