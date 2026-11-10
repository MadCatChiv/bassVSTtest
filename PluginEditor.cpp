#include "PluginEditor.h"

//==============================================================================
BassLookAndFeel::BassLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd8dadf));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, juce::Colour (0xffb9bcc4));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2c2f35));
    setColour (juce::ComboBox::textColourId, juce::Colour (0xffd8dadf));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3d4048));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff23262b));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c2f35));
    setColour (juce::TextButton::buttonOnColourId, accent());
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd8dadf));
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

void BassLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (7.f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff383b42));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroAngle = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    juce::Path val;
    val.addCentredArc (c.x, c.y, r, r, 0.f, std::min (zeroAngle, angle), std::max (zeroAngle, angle), true);
    g.setColour (s.isEnabled() ? accent() : juce::Colours::grey);
    g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = r - 7.f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e57), c.x, c.y - br, juce::Colour (0xff25272c), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
    g.setColour (juce::Colour (0xff15161a));
    g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 1.2f);

    juce::Path p;
    p.addRoundedRectangle (-1.5f, -br + 2.f, 3.f, br * 0.45f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void BassLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent() : juce::Colour (0xff2c2f35));
    g.fillRoundedRectangle (r, 5.f);
    g.setColour (hl ? juce::Colours::white.withAlpha (0.5f) : juce::Colour (0xff3d4048));
    g.drawRoundedRectangle (r, 5.f, 1.f);
    g.setColour (on ? juce::Colours::black : juce::Colour (0xffd8dadf));
    g.setFont (juce::FontOptions (13.f, juce::Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
BassDriverEditor::BassDriverEditor (BassDriverAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    const juce::String fine = "  (Shift-drag: fine adjust, double-click: reset)";
    addKnob (drive,    "drive",    "DRIVE",    "Amount of saturation. Level-compensated: changes grit, not volume." + fine);
    addKnob (blend,    "blend",    "BLEND",    "Clean to driven. The low end always stays clean and tight." + fine);
    addKnob (tight,    "tight",    "TIGHT",    "How much low end stays clean. Higher = tighter, more focused grind." + fine);
    addKnob (comp,     "comp",     "COMP",     "Slow opto-style leveller for sustain." + fine);
    addKnob (peak,     "peak",     "PEAK",     "Fast peak catcher for evenness before the drive." + fine);
    addKnob (bass,     "bass",     "BASS",     "Low shelf. Corner frequency follows the voicing." + fine);
    addKnob (mid,      "mid",      "MID",      "Parametric mid. Gets narrower as you push it, like analogue hardware." + fine);
    addKnob (midFreq,  "midfreq",  "MID FREQ", "Centre frequency of the mid control." + fine);
    addKnob (treble,   "treble",   "TREBLE",   "High shelf." + fine);
    addKnob (presence, "presence", "PRESENCE", "Top-end air and bite." + fine);
    addKnob (level,    "level",    "OUTPUT",   "Final trim, applied after automatic loudness matching." + fine);

    voicingBox.addItemList (proc.apvts.getParameter ("voicing")->getAllValueStrings(), 1);
    voicingBox.setTooltip ("Amp-inspired voicing: clipper, pre/post filtering, crossover and tone corners.");
    addAndMakeVisible (voicingBox);
    voicingAtt = std::make_unique<ComboAtt> (proc.apvts, "voicing", voicingBox);

    cabBox.addItemList (proc.apvts.getParameter ("cabmodel")->getAllValueStrings(), 1);
    addAndMakeVisible (cabBox);
    cabAtt = std::make_unique<ComboAtt> (proc.apvts, "cabmodel", cabBox);

    for (auto* b : { &cabOn, &autoIn, &autoOut }) addAndMakeVisible (*b);
    cabOn.setTooltip ("Enable cabinet simulation. Leave off if an amp sim or cab IR follows.");
    autoIn.setTooltip ("Slowly levels what you play so Drive and Comp feel the same for any bass or DI level.");
    autoOut.setTooltip ("Matches output loudness (ITU-R BS.1770 K-weighted) to the input loudness.");
    cabOnAtt   = std::make_unique<ButtonAtt> (proc.apvts, "cab", cabOn);
    autoInAtt  = std::make_unique<ButtonAtt> (proc.apvts, "autoin", autoIn);
    autoOutAtt = std::make_unique<ButtonAtt> (proc.apvts, "autoout", autoOut);

    for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveBtn, &abA, &abB, &copyBtn, &undoBtn, &redoBtn, &irBtn })
        addAndMakeVisible (*b);
    abA.setClickingTogglesState (false); abB.setClickingTogglesState (false);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (+1); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { promptSavePreset(); };
    abA.onClick = [this] { proc.switchToSlot (0); };
    abB.onClick = [this] { proc.switchToSlot (1); };
    copyBtn.onClick = [this] { proc.copyActiveToOtherSlot(); };
    copyBtn.setTooltip ("Copy the active slot's settings into the other slot.");
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    redoBtn.onClick = [this] { proc.undoManager.redo(); };
    irBtn.onClick = [this] { showIrMenu(); };
    irLabel.setJustificationType (juce::Justification::centredLeft);
    irLabel.setFont (juce::FontOptions (12.f));
    addAndMakeVisible (irLabel);

    setSize (900, 520);
    addMouseListener (this, true);
    startTimerHz (30);
    timerCallback();
}

BassDriverEditor::~BassDriverEditor() { stopTimer(); setLookAndFeel (nullptr); }

void BassDriverEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name, const juce::String& tip)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
    if (auto* par = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, par->convertFrom0to1 (par->getDefaultValue()));
}

void BassDriverEditor::mouseDown (const juce::MouseEvent&) { proc.undoManager.beginNewTransaction(); }

//==============================================================================
void BassDriverEditor::timerCallback()
{
    presetBtn.setButtonText (proc.getCurrentPresetName());
    abA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    abA.setColour (juce::TextButton::buttonColourId, proc.getActiveSlot() == 0 ? BassLookAndFeel::accent() : juce::Colour (0xff2c2f35));
    abB.setColour (juce::TextButton::buttonColourId, proc.getActiveSlot() == 1 ? BassLookAndFeel::accent() : juce::Colour (0xff2c2f35));
    abA.setColour (juce::TextButton::textColourOffId, proc.getActiveSlot() == 0 ? juce::Colours::black : juce::Colour (0xffd8dadf));
    abB.setColour (juce::TextButton::textColourOffId, proc.getActiveSlot() == 1 ? juce::Colours::black : juce::Colour (0xffd8dadf));
    undoBtn.setEnabled (proc.undoManager.canUndo());
    redoBtn.setEnabled (proc.undoManager.canRedo());
    const auto ir = proc.getImpulseResponseName();
    irLabel.setText (ir.isEmpty() ? "no IR loaded" : ir, juce::dontSendNotification);

    // Smooth meter ballistics on the UI side (fast attack, slow fall).
    auto follow = [] (float& d, float t) { d = t > d ? t : d + 0.2f * (t - d); };
    follow (dispIn, proc.meterInLufs.load());
    follow (dispOut, proc.meterOutLufs.load());
    follow (dispGr, proc.meterGrDb.load());
    repaint (meterArea);
}

void BassDriverEditor::showPresetMenu()
{
    juce::PopupMenu m, factory, user;
    const auto cur = proc.getCurrentPresetName();
    for (int i = 0; i < bd::kNumFactoryPresets; ++i)
        factory.addItem (1000 + i, bd::kFactoryPresets[i].name, true, cur == bd::kFactoryPresets[i].name);
    m.addSubMenu ("Factory", factory);

    const auto files = proc.listUserPresets();
    for (int i = 0; i < files.size(); ++i)
        user.addItem (2000 + i, files[i].getFileNameWithoutExtension(), true, cur == files[i].getFileNameWithoutExtension());
    if (files.isEmpty()) user.addItem (-1, "(no user presets yet)", false);
    m.addSubMenu ("User", user);
    m.addSeparator();
    m.addItem (1, "Save preset...");
    m.addItem (2, "Show preset folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this, files] (int r)
    {
        if (r == 1) promptSavePreset();
        else if (r == 2) { auto f = BassDriverAudioProcessor::userPresetFolder(); f.createDirectory(); f.startAsProcess(); }
        else if (r >= 2000) { if (r - 2000 < files.size()) proc.loadUserPreset (files[r - 2000]); }
        else if (r >= 1000) proc.setCurrentProgram (r - 1000);
    });
}

void BassDriverEditor::stepPreset (int dir)
{
    // Walk the combined list: factory presets first, then user presets.
    juce::StringArray names; juce::Array<juce::File> files = proc.listUserPresets();
    for (int i = 0; i < bd::kNumFactoryPresets; ++i) names.add (bd::kFactoryPresets[i].name);
    for (auto& f : files) names.add (f.getFileNameWithoutExtension());
    int idx = names.indexOf (proc.getCurrentPresetName());
    idx = (idx < 0 ? (dir > 0 ? 0 : names.size() - 1) : (idx + dir + names.size()) % names.size());
    if (idx < bd::kNumFactoryPresets) proc.setCurrentProgram (idx);
    else proc.loadUserPreset (files[idx - bd::kNumFactoryPresets]);
}

void BassDriverEditor::promptSavePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getCurrentPresetName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r == 1) proc.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

void BassDriverEditor::showIrMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Load cabinet IR (.wav / .aif / .flac)...");
    m.addItem (2, "Clear IR", proc.getImpulseResponseName().isNotEmpty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (irBtn), [this] (int r)
    {
        if (r == 2) proc.clearImpulseResponse();
        else if (r == 1)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load cabinet impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f.existsAsFile())
                                      {
                                          proc.loadImpulseResponse (f);
                                          if (auto* p = proc.apvts.getParameter ("cabmodel")) p->setValueNotifyingHost (p->convertTo0to1 ((float) bd::kCabUserIR));
                                          if (auto* p = proc.apvts.getParameter ("cab"))      p->setValueNotifyingHost (1.f);
                                      }
                                  });
        }
    });
}

//==============================================================================
void BassDriverEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141518));
    g.setColour (BassLookAndFeel::accent());
    g.setFont (juce::FontOptions (22.f, juce::Font::bold));
    g.drawText ("BASS DRIVER", 18, 10, 200, 28, juce::Justification::centredLeft);

    auto panel = [&g] (juce::Rectangle<int> r, const char* title)
    {
        g.setColour (juce::Colour (0xff202328));
        g.fillRoundedRectangle (r.toFloat(), 8.f);
        g.setColour (juce::Colour (0xff30343b));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.f, 1.f);
        g.setColour (juce::Colour (0xff8a8f99));
        g.setFont (juce::FontOptions (11.f, juce::Font::bold));
        g.drawText (title, r.getX() + 12, r.getY() + 6, 200, 14, juce::Justification::centredLeft);
    };
    panel (secDrive, "DRIVE");  panel (secComp, "COMPRESSOR");  panel (secOut, "OUTPUT");
    panel (secTone, "TONE");    panel (secCab, "CABINET");

    // meters
    auto m = meterArea;
    auto bar = [&g] (juce::Rectangle<int> r, const char* name, float norm, juce::Colour col, const juce::String& txt)
    {
        g.setColour (juce::Colour (0xff15161a)); g.fillRoundedRectangle (r.toFloat(), 3.f);
        g.setColour (col);
        g.fillRoundedRectangle (r.toFloat().withWidth (std::max (0.f, std::min (1.f, norm)) * (float) r.getWidth()), 3.f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (name, r.withTrimmedLeft (5), juce::Justification::centredLeft);
        g.drawText (txt, r.withTrimmedRight (5), juce::Justification::centredRight);
    };
    const int rowH = m.getHeight() / 3;
    bar (m.removeFromTop (rowH).reduced (0, 2), "IN",  (dispIn + 50.f) / 45.f,  juce::Colour (0xff3b8f5a), juce::String (dispIn, 1) + " LUFS");
    bar (m.removeFromTop (rowH).reduced (0, 2), "OUT", (dispOut + 50.f) / 45.f, juce::Colour (0xff3b8f5a), juce::String (dispOut, 1) + " LUFS");
    bar (m.removeFromTop (rowH).reduced (0, 2), "GR",  dispGr / 12.f,           juce::Colour (0xffc9463d), juce::String (dispGr, 1) + " dB");
}

void BassDriverEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    // header
    auto head = area.removeFromTop (40);
    auto right = head;
    undoBtn.setBounds (right.removeFromRight (56).reduced (2));  redoBtn.setBounds (right.removeFromRight (56).reduced (2));
    right.removeFromRight (10);
    copyBtn.setBounds (right.removeFromRight (46).reduced (2));
    abB.setBounds (right.removeFromRight (32).reduced (2));      abA.setBounds (right.removeFromRight (32).reduced (2));
    right.removeFromRight (10);
    saveBtn.setBounds (right.removeFromRight (56).reduced (2));
    nextBtn.setBounds (right.removeFromRight (30).reduced (2));
    presetBtn.setBounds (right.removeFromRight (190).reduced (2));
    prevBtn.setBounds (right.removeFromRight (30).reduced (2));

    area.removeFromTop (6);
    const int gap = 8;
    auto row1 = area.removeFromTop (205);
    area.removeFromTop (gap);
    auto row2 = area;

    secDrive = row1.removeFromLeft (370); row1.removeFromLeft (gap);
    secComp  = row1.removeFromLeft (200); row1.removeFromLeft (gap);
    secOut   = row1;
    secTone  = row2.removeFromLeft (520); row2.removeFromLeft (gap);
    secCab   = row2;

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    };
    auto knobRow = [&placeKnob] (juce::Rectangle<int> r, std::initializer_list<Knob*> ks)
    {
        const int w = r.getWidth() / (int) ks.size();
        for (auto* k : ks) placeKnob (*k, r.removeFromLeft (w));
    };

    // DRIVE: voicing selector + three knobs
    auto d = secDrive.reduced (10, 22);
    voicingBox.setBounds (d.removeFromTop (26)); d.removeFromTop (6);
    knobRow (d, { &drive, &blend, &tight });

    knobRow (secComp.reduced (10, 22).withTrimmedTop (32), { &comp, &peak });

    // OUTPUT: level knob + toggles + meters
    auto o = secOut.reduced (10, 22);
    auto top = o.removeFromTop (28);
    autoIn.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (2, 0));
    autoOut.setBounds (top.reduced (2, 0));
    o.removeFromTop (4);
    meterArea = o.removeFromBottom (66);
    placeKnob (level, o.withSizeKeepingCentre (110, o.getHeight()));

    knobRow (secTone.reduced (10, 22), { &bass, &mid, &midFreq, &treble, &presence });

    // CABINET
    auto c = secCab.reduced (10, 22);
    cabOn.setBounds (c.removeFromTop (26).removeFromLeft (70)); c.removeFromTop (8);
    cabBox.setBounds (c.removeFromTop (26)); c.removeFromTop (8);
    auto irRow = c.removeFromTop (26);
    irBtn.setBounds (irRow.removeFromLeft (64)); irRow.removeFromLeft (6);
    irLabel.setBounds (irRow);
}
