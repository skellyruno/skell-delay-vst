#include "PluginProcessor.h"
#include "PluginEditor.h"

// Helper function converting delay time in ms to musical fraction string
static juce::String getTimeFractionString (float timeMs)
{
    struct Division { float ms; const char* name; };
    const Division divisions[] = {
        { 31.25f,  "1/64" },
        { 41.67f,  "1/32T" },
        { 62.50f,  "1/32" },
        { 83.33f,  "1/16T" },
        { 125.0f,  "1/16" },
        { 166.67f, "1/8T" },
        { 250.0f,  "1/8" },
        { 333.33f, "1/4T" },
        { 500.0f,  "1/4" },
        { 666.67f, "1/2T" },
        { 1000.0f, "1/2" },
        { 2000.0f, "1/1" }
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

    auto setupHeader = [this](juce::Label& l, const juce::String& text) {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (15.0f, juce::Font::bold | juce::Font::italic));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    setupHeader (inputHeader, "INPUT");
    setupHeader (delayHeader, "DELAY");
    setupHeader (outputHeader, "OUTPUT");

    setupKnob (panSlider, panLabel, "PAN");
    setupKnob (volSlider, volLabel, "VOL");

    setupKnob (timeSlider, timeLabel, "TIME");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");

    setupKnob (mixerSlider, mixerLabel, "MIXER");
    setupKnob (dryWetSlider, dryWetLabel, "DRY/WET");
    setupKnob (hiCutSlider, hiCutLabel, "HI-CUT");

    addAndMakeVisible (pingPongButton);
    pingPongLabel.setText ("PING PONG", juce::dontSendNotification);
    pingPongLabel.setFont (juce::Font (10.0f, juce::Font::bold | juce::Font::italic));
    pingPongLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
    pingPongLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pingPongLabel);

    // Mode Selector Radio Buttons
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
    volAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "VOL", volSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    mixerAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIXER", mixerSlider);
    dryWetAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX", dryWetSlider);
    hiCutAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "HICUT", hiCutSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    // Update time fraction display when time knob moves
    timeSlider.onValueChange = [this]() {
        float ms = static_cast<float>(timeSlider.getValue());
        delayDisplay.setText (getTimeFractionString (ms));
    };

    // Initialize Mode selection state from APVTS
    int currentMode = static_cast<int>(audioProcessor.apvts.getRawParameterValue ("MODE")->load());
    updateModeButtons (currentMode);

    startTimerHz (30);

    // 25% Reduction Size (750 x 240)
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
    // --- 1. FAR LEFT & FAR RIGHT LED METERS ---
    leftMeter.setBounds (9, 48, 11, 150);
    rightMeter.setBounds (730, 48, 11, 150);

    // --- 2. INPUT SECTION ---
    inputHeader.setBounds (45, 50, 135, 20);

    panSlider.setBounds (48, 90, 56, 56);
    panLabel.setBounds (38, 150, 76, 16);

    volSlider.setBounds (120, 90, 56, 56);
    volLabel.setBounds (110, 150, 76, 16);

    // --- 3. CENTER DELAY SECTION ---
    delayHeader.setBounds (308, 50, 135, 20);

    // Time Display Box
    delayDisplay.setBounds (326, 78, 98, 26);

    // Mode Buttons
    digitalBtn.setBounds (315, 110, 38, 15);
    analogBtn.setBounds (356, 110, 38, 15);
    tapeBtn.setBounds (397, 110, 38, 15);

    // Ping Pong Switch
    pingPongButton.setBounds (366, 130, 18, 18);
    pingPongLabel.setBounds (326, 150, 98, 15);

    // Large Center Knobs
    timeSlider.setBounds (214, 82, 86, 86);
    timeLabel.setBounds (219, 171, 76, 16);

    feedbackSlider.setBounds (448, 82, 86, 86);
    feedbackLabel.setBounds (453, 171, 76, 16);

    // --- 4. OUTPUT SECTION ---
    outputHeader.setBounds (548, 50, 172, 20);

    mixerSlider.setBounds (551, 94, 52, 52);
    mixerLabel.setBounds (540, 150, 75, 16);

    dryWetSlider.setBounds (611, 94, 52, 52);
    dryWetLabel.setBounds (600, 150, 75, 16);

    hiCutSlider.setBounds (671, 94, 52, 52);
    hiCutLabel.setBounds (660, 150, 75, 16);
}
