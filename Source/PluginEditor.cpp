#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String getTimeFractionString (float timeMs)
{
    struct Division { float ms; const char* name; };
    const Division divisions[] = {
        { 31.25f,  "1/64" },  { 41.67f,  "1/32T" }, { 62.50f,  "1/32" },
        { 83.33f,  "1/16T" }, { 125.0f,  "1/16" },  { 166.67f, "1/8T" },
        { 250.0f,  "1/8" },   { 333.33f, "1/4T" },  { 500.0f,  "1/4" },
        { 666.67f, "1/2T" },  { 1000.0f, "1/2" },   { 2000.0f, "1/1" }
    };

    int closestIdx = 0;
    float minDiff = std::abs (timeMs - divisions[0].ms);

    for (int i = 1; i < 12; ++i)
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

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.onValueChange = [&s]() { s.repaint(); };
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

    // 1. Create attachments FIRST
    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    smoothAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTH", smoothSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    dryAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRY", drySlider);
    wetAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "WET", wetSlider);
    duckingAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    // 2. Define textFromValueFunction AFTER attachments so they don't get overridden
    panSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt ((v + 1.0) * 50.0));
    };

    smoothSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v));
    };

    // Inside TIME knob now displays the fraction string (e.g. 1/8) matching center screen
    timeSlider.textFromValueFunction = [](double v) {
        return getTimeFractionString (static_cast<float>(v));
    };

    feedbackSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0));
    };

    duckingSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0));
    };

    drySlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0));
    };

    wetSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0));
    };

    timeSlider.onValueChange = [this]() {
        float ms = static_cast<float>(timeSlider.getValue());
        delayDisplay.setText (getTimeFractionString (ms));
        timeSlider.repaint();
    };

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
    leftMeter.setLevel (audioProcessor.getLeftLevel());
    rightMeter.setLevel (audioProcessor.getRightLevel());
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
    // Outer Meters
    leftMeter.setBounds (10, 42, 8, 128);
    rightMeter.setBounds (620, 42, 8, 128);

    // Left Small Knobs (Shifted down for clearance)
    panSlider.setBounds (40, 80, 48, 48);
    panLabel.setBounds (32, 130, 64, 14);

    smoothSlider.setBounds (104, 80, 48, 48);
    smoothLabel.setBounds (96, 130, 64, 14);

    // Large TIME Knob (1/8 centered at 12 o'clock)
    timeSlider.setBounds (174, 72, 72, 72);
    timeLabel.setBounds (178, 146, 64, 14);

    // Center Display & Mode Selection Buttons
    delayDisplay.setBounds (269, 68, 100, 24);

    digitalBtn.setBounds (260, 96, 38, 15);
    analogBtn.setBounds (300, 96, 38, 15);
    tapeBtn.setBounds (340, 96, 38, 15);

    pingPongButton.setBounds (311, 116, 16, 16);
    pingPongLabel.setBounds (269, 133, 100, 14);

    // Large FEEDBACK Knob
    feedbackSlider.setBounds (390, 72, 72, 72);
    feedbackLabel.setBounds (394, 146, 64, 14);

    // Right Knobs: DRY, WET, DUCKING (Shifted down and brought inward to clear top/right border artwork)
    drySlider.setBounds (478, 64, 44, 44);
    dryLabel.setBounds (468, 110, 64, 14);

    wetSlider.setBounds (536, 64, 44, 44);
    wetLabel.setBounds (526, 110, 64, 14);

    duckingSlider.setBounds (507, 122, 44, 44);
    duckingLabel.setBounds (492, 168, 74, 14);
}
