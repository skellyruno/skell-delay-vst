#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String getTimeFractionString (float timeMs)
{
    struct Division { float ms; const char* name; };
    const Division divisions[] = {
        { 31.25f,  "1/64" },  { 41.67f,  "1/32T" }, { 62.50f,  "1/32" },
        { 83.33f,  "1/16T" }, { 125.0f,  "1/16" },  { 166.67f, "1/8T" },
        { 250.0f,  "1/8" },   { 333.33f, "1/4T" },  { 500.0f,  "1/4" },
        { 666.67f, "1/2T" },  { 1000.0f, "1/1" }
    };

    int closestIdx = 0;
    float minDiff = std::abs (timeMs - divisions[0].ms);

    for (int i = 1; i < 11; ++i)
    {
        float diff = std::abs (timeMs - divisions[i].ms);
        if (diff < minDiff)
        {
            minDiff = diff;
            closestIdx = i;
        }
    }
    return divisions[closestIdx].name;
}

DelayAudioProcessorEditor::DelayAudioProcessorEditor (DelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    bgImage = juce::ImageFileFormat::loadFrom (
        BinaryData::skell_bg_png, BinaryData::skell_bg_pngSize);

    setLookAndFeel (&skellLookAndFeel);

    addAndMakeVisible (leftMeter);
    addAndMakeVisible (rightMeter);
    addAndMakeVisible (delayDisplay);

    // Preset Menu Configuration
    presetSelector.addItem ("SKELL Default", 1);
    presetSelector.addItem ("Slapback Echo", 2);
    presetSelector.addItem ("Vocal Ducking", 3);
    presetSelector.addItem ("Tape Wash",    4);
    presetSelector.addItem ("Ping-Pong Lead", 5);
    presetSelector.setSelectedId (1, juce::dontSendNotification);

    presetSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff081404));
    presetSelector.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff39ff14));
    presetSelector.setColour (juce::ComboBox::textColourId, juce::Colour (0xff39ff14));
    presetSelector.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff39ff14));

    presetSelector.onChange = [this]() { applyPreset (presetSelector.getSelectedId()); };
    addAndMakeVisible (presetSelector);

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (10.0f).withStyle ("Bold Italic"));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff39ff14));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    setupKnob (panSlider, panLabel, "PAN");
    setupKnob (smoothSlider, smoothLabel, "SMOOTH");
    setupKnob (timeSlider, timeLabel, "TIME");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");
    setupKnob (drySlider, dryLabel, "DRY");
    setupKnob (wetSlider, wetLabel, "WET");
    setupKnob (duckingSlider, duckingLabel, "DUCKING");

    addAndMakeVisible (pingPongButton);
    pingPongLabel.setText ("PING PONG", juce::dontSendNotification);
    pingPongLabel.setFont (juce::FontOptions (8.5f).withStyle ("Bold Italic"));
    pingPongLabel.setColour (juce::Label::textColourId, juce::Colour (0xff39ff14));
    pingPongLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pingPongLabel);

    digitalBtn.setRadioGroupId (1001);
    analogBtn.setRadioGroupId (1001);
    tapeBtn.setRadioGroupId (1001);

    digitalBtn.setClickingTogglesState (true);
    analogBtn.setClickingTogglesState (true);
    tapeBtn.setClickingTogglesState (true);

    addAndMakeVisible (digitalBtn);
    addAndMakeVisible (analogBtn);
    addAndMakeVisible (tapeBtn);

    digitalBtn.onClick = [this]() { updateModeButtons (0); };
    analogBtn.onClick  = [this]() { updateModeButtons (1); };
    tapeBtn.onClick    = [this]() { updateModeButtons (2); };

    // Attachments
    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    smoothAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTH", smoothSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    dryAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRY", drySlider);
    wetAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "WET", wetSlider);
    duckingAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    // Text Formatters
    panSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt ((v + 1.0) * 50.0)); };
    smoothSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v)); };
    timeSlider.textFromValueFunction = [](double v) { return getTimeFractionString (static_cast<float>(v)); };
    feedbackSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    drySlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    wetSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    duckingSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };

    int currentMode = static_cast<int>(audioProcessor.apvts.getRawParameterValue ("MODE")->load());
    updateModeButtons (currentMode);

    startTimerHz (30);
    setSize (638, 204);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DelayAudioProcessorEditor::applyPreset (int presetIndex)
{
    auto setParam = [this](const juce::String& paramID, float normVal) {
        if (auto* param = audioProcessor.apvts.getParameter (paramID))
            param->setValueNotifyingHost (normVal);
    };

    switch (presetIndex)
    {
        case 1: // Default
            setParam ("PAN", 0.5f); setParam ("SMOOTH", 0.5f); setParam ("DELAY_TIME", 0.11f);
            setParam ("FEEDBACK", 0.5f); setParam ("DRY", 1.0f); setParam ("WET", 1.0f); setParam ("DUCKING", 0.0f);
            updateModeButtons (0);
            break;

        case 2: // Slapback
            setParam ("PAN", 0.5f); setParam ("DELAY_TIME", 0.02f); setParam ("FEEDBACK", 0.15f);
            setParam ("DRY", 1.0f); setParam ("WET", 0.8f); setParam ("DUCKING", 0.0f);
            updateModeButtons (1);
            break;

        case 3: // Vocal Ducking
            setParam ("PAN", 0.5f); setParam ("DELAY_TIME", 0.18f); setParam ("FEEDBACK", 0.60f);
            setParam ("DRY", 1.0f); setParam ("WET", 0.9f); setParam ("DUCKING", 0.65f);
            updateModeButtons (0);
            break;

        case 4: // Tape Wash
            setParam ("PAN", 0.5f); setParam ("DELAY_TIME", 0.25f); setParam ("FEEDBACK", 0.75f);
            setParam ("DRY", 1.0f); setParam ("WET", 0.7f); setParam ("DUCKING", 0.0f);
            updateModeButtons (2);
            break;

        case 5: // Ping-Pong Lead
            setParam ("PAN", 0.5f); setParam ("DELAY_TIME", 0.11f); setParam ("FEEDBACK", 0.50f);
            setParam ("DRY", 1.0f); setParam ("WET", 1.0f); setParam ("DUCKING", 0.20f);
            if (auto* pp = audioProcessor.apvts.getParameter ("PINGPONG"))
                pp->setValueNotifyingHost (1.0f);
            updateModeButtons (0);
            break;
    }
}

void DelayAudioProcessorEditor::updateModeButtons (int selectedIndex)
{
    auto* modeParam = audioProcessor.apvts.getParameter ("MODE");
    if (modeParam != nullptr)
        modeParam->setValueNotifyingHost (modeParam->convertTo0to1 (static_cast<float>(selectedIndex)));

    digitalBtn.setToggleState (selectedIndex == 0, juce::dontSendNotification);
    analogBtn.setToggleState  (selectedIndex == 1, juce::dontSendNotification);
    tapeBtn.setToggleState    (selectedIndex == 2, juce::dontSendNotification);
}

void DelayAudioProcessorEditor::timerCallback()
{
    // 30 FPS update keeps SliderAttachments working without breaking callbacks
    leftMeter.setLevel (audioProcessor.getLeftLevel());
    rightMeter.setLevel (audioProcessor.getRightLevel());

    float ms = static_cast<float>(timeSlider.getValue());
    delayDisplay.setText (getTimeFractionString (ms));
}

void DelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (bgImage.isValid())
        g.drawImage (bgImage, getLocalBounds().toFloat());
    else
        g.fillAll (juce::Colour (0xff050a04));
}

void DelayAudioProcessorEditor::resized()
{
    leftMeter.setBounds (10, 42, 8, 128);
    rightMeter.setBounds (620, 42, 8, 128);

    panSlider.setBounds (40, 80, 48, 48);
    panLabel.setBounds (32, 130, 64, 14);

    smoothSlider.setBounds (104, 80, 48, 48);
    smoothLabel.setBounds (96, 130, 64, 14);

    timeSlider.setBounds (174, 72, 72, 72);
    timeLabel.setBounds (178, 146, 64, 14);

    // Preset Manager Bar atop Center LCD
    presetSelector.setBounds (269, 44, 100, 18);
    delayDisplay.setBounds (269, 66, 100, 24);

    digitalBtn.setBounds (260, 94, 38, 15);
    analogBtn.setBounds (300, 94, 38, 15);
    tapeBtn.setBounds (340, 94, 38, 15);

    pingPongButton.setBounds (311, 114, 16, 16);
    pingPongLabel.setBounds (269, 131, 100, 14);

    feedbackSlider.setBounds (390, 72, 72, 72);
    feedbackLabel.setBounds (394, 146, 64, 14);

    drySlider.setBounds (470, 68, 44, 44);
    dryLabel.setBounds (460, 114, 64, 14);

    wetSlider.setBounds (525, 68, 44, 44);
    wetLabel.setBounds (515, 114, 64, 14);

    duckingSlider.setBounds (498, 126, 44, 44);
    duckingLabel.setBounds (483, 170, 74, 14);
}
