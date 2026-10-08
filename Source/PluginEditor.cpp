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
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (11.5f, juce::Font::bold | juce::Font::italic));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    // Knobs & Labels
    setupKnob (panSlider, panLabel, "PAN");
    setupKnob (smoothSlider, smoothLabel, "SMOOTH");

    setupKnob (timeSlider, timeLabel, "TIME");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");

    setupKnob (duckingSlider, duckingLabel, "DUCKING");
    setupKnob (drySlider, dryLabel, "DRY");
    setupKnob (wetSlider, wetLabel, "WET");

    addAndMakeVisible (pingPongButton);
    pingPongLabel.setText ("PING PONG", juce::dontSendNotification);
    pingPongLabel.setFont (juce::Font (10.0f, juce::Font::bold | juce::Font::italic));
    pingPongLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
    pingPongLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pingPongLabel);

    // Mode Buttons
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
    duckingAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    dryAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRY", drySlider);
    wetAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "WET", wetSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    timeSlider.onValueChange = [this]() {
        float ms = static_cast<float>(timeSlider.getValue());
        delayDisplay.setText (getTimeFractionString (ms));
    };

    int currentMode = static_cast<int>(audioProcessor.apvts.getRawParameterValue ("MODE")->load());
    updateModeButtons (currentMode);

    startTimerHz (30);
    setSize (750, 240);
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
    // --- 1. LED METERS ---
    leftMeter.setBounds (9, 45, 11, 150);
    rightMeter.setBounds (730, 45, 11, 150);

    // --- 2. LEFT PANEL (PAN, SMOOTH) ---
    panSlider.setBounds (48, 88, 56, 56);
    panLabel.setBounds (38, 148, 76, 16);

    smoothSlider.setBounds (120, 88, 56, 56);
    smoothLabel.setBounds (110, 148, 76, 16);

    // --- 3. CENTER PANEL ---
    delayDisplay.setBounds (326, 75, 98, 26);

    digitalBtn.setBounds (315, 107, 38, 15);
    analogBtn.setBounds (356, 107, 38, 15);
    tapeBtn.setBounds (397, 107, 38, 15);

    pingPongButton.setBounds (366, 127, 18, 18);
    pingPongLabel.setBounds (326, 147, 98, 15);

    timeSlider.setBounds (214, 80, 86, 86);
    timeLabel.setBounds (219, 169, 76, 16);

    feedbackSlider.setBounds (448, 80, 86, 86);
    feedbackLabel.setBounds (453, 169, 76, 16);

    // --- 4. RIGHT PANEL (DUCKING, DRY, WET) ---
    duckingSlider.setBounds (551, 90, 52, 52);
    duckingLabel.setBounds (540, 148, 75, 16);

    drySlider.setBounds (611, 90, 52, 52);
    dryLabel.setBounds (600, 148, 75, 16);

    wetSlider.setBounds (671, 90, 52, 52);
    wetLabel.setBounds (660, 148, 75, 16);
}
