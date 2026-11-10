#pragma once
#include "PluginProcessor.h"

/** Knob look: arc track, value arc (centre-origin for bipolar params), body + pointer. */
class BassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BassLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    static juce::Colour accent() { return juce::Colour (0xffff7a1a); }
};

/** Slider with professional fine control: hold Shift while dragging for 6x finer resolution;
    double-click resets to the parameter default (set by the editor). */
class FineSlider : public juce::Slider
{
public:
    FineSlider() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1500 : 250);
        juce::Slider::mouseDown (e);
    }
};

class BassDriverEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassDriverEditor (BassDriverAudioProcessor&);
    ~BassDriverEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // starts a new undo transaction for every gesture

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        FineSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const juce::String& id, const juce::String& name, const juce::String& tip);
    void timerCallback() override;
    void showPresetMenu();
    void stepPreset (int dir);
    void promptSavePreset();
    void showIrMenu();

    BassDriverAudioProcessor& proc;
    BassLookAndFeel lnf;

    Knob drive, blend, tight, comp, peak, bass, mid, midFreq, treble, presence, level;
    juce::ComboBox voicingBox, cabBox;
    juce::ToggleButton cabOn { "Cab" }, autoIn { "Auto In" }, autoOut { "Auto Out" };
    std::unique_ptr<ComboAtt> voicingAtt, cabAtt;
    std::unique_ptr<ButtonAtt> cabOnAtt, autoInAtt, autoOutAtt;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn, saveBtn { "Save" },
                     abA { "A" }, abB { "B" }, copyBtn { "A>B" }, undoBtn { "Undo" }, redoBtn { "Redo" }, irBtn { "IR..." };
    juce::Label irLabel;
    juce::TooltipWindow tooltip { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> secDrive, secComp, secOut, secTone, secCab, meterArea;
    float dispIn = -70.f, dispOut = -70.f, dispGr = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassDriverEditor)
};
