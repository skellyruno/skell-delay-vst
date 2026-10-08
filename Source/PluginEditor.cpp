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
        l.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    // Format centered text readouts inside knob centers
    panSlider.textFromValueFunction = [](double v) {
        if (std::abs (v) < 0.05) return juce::String ("C");
        if (v < 0) return "L" + juce::String (juce::roundToInt (std::abs (v) * 100));
        return "R" + juce::String (juce::roundToInt (v * 100));
    };

    smoothSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v)) + "ms";
    };

    timeSlider.textFromValueFunction = [](double v) {
        return getTimeFractionString (static_cast<float>(v));
    };

    feedbackSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    };

    duckingSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    };

    drySlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    };

    wetSlider.textFromValueFunction = [](double v) {
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    };

    setupKnob (panSlider, panLabel, "PAN");
    setupKnob (smoothSlider, smoothLabel, "SMOOTH");
    setupKnob (timeSlider, timeLabel, "TIME");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");
    setupKnob (duckingSlider, duckingLabel, "DUCKING");
    setupKnob (drySlider, dryLabel, "DRY");
    setupKnob (wetSlider, wetLabel, "WET");

    addAndMakeVisible (pingPongButton);
    pingPongLabel.setText ("PING PONG", juce::dontSendNotification);
    pingPongLabel.setFont (juce::FontOptions (8.5f).withStyle ("Bold Italic"));
    pingPongLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
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

    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    smoothAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTH", smoothSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    duckingAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    dryAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRY", drySlider);
    wetAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "WET", wetSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

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
        g.fillAll (juce::Colour (0xff070a07));
}

void DelayAudioProcessorEditor::resized()
{
    leftMeter.setBounds (8, 38, 9, 128);
    rightMeter.setBounds (621, 38, 9, 128);

    panSlider.setBounds (41, 75, 48, 48);
    panLabel.setBounds (32, 126, 65, 14);

    smoothSlider.setBounds (102, 75, 48, 48);
    smoothLabel.setBounds (94, 126, 65, 14);

    delayDisplay.setBounds (277, 64, 84, 22);

    digitalBtn.setBounds (268, 91, 32, 13);
    analogBtn.setBounds (303, 91, 32, 13);
    tapeBtn.setBounds (338, 91, 32, 13);

    pingPongButton.setBounds (311, 108, 15, 15);
    pingPongLabel.setBounds (277, 125, 84, 13);

    timeSlider.setBounds (182, 68, 73, 73);
    timeLabel.setBounds (186, 144, 65, 14);

    feedbackSlider.setBounds (381, 68, 73, 73);
    feedbackLabel.setBounds (385, 144, 65, 14);

    duckingSlider.setBounds (468, 77, 44, 44);
    duckingLabel.setBounds (459, 126, 64, 14);

    drySlider.setBounds (519, 77, 44, 44);
    dryLabel.setBounds (510, 126, 64, 14);

    wetSlider.setBounds (570, 77, 44, 44);
    wetLabel.setBounds (561, 126, 64, 14);
}
