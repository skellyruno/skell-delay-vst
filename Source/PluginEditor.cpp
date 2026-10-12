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
    pingPongLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pingPongLabel);

    digitalBtn.setRadioGroupId (1001);
    analogBtn.setRadioGroupId (1001);
    tapeBtn.setRadioGroupId (1001);

    digitalBtn.setClickingTogglesState (true);
    analogBtn.setClickingTogglesState (true);
    tapeBtn.setClickingTogglesState (true);

    digitalBtn.setConnectedEdges (juce::Button::ConnectedOnRight);
    analogBtn.setConnectedEdges  (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    tapeBtn.setConnectedEdges    (juce::Button::ConnectedOnLeft);

    addAndMakeVisible (digitalBtn);
    addAndMakeVisible (analogBtn);
    addAndMakeVisible (tapeBtn);

    digitalBtn.onClick = [this]() { setMode (0); };
    analogBtn.onClick  = [this]() { setMode (1); };
    tapeBtn.onClick    = [this]() { setMode (2); };

    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    smoothAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTH", smoothSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    dryAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRY", drySlider);
    wetAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "WET", wetSlider);
    duckingAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    panSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt ((v + 1.0) * 50.0)); };
    smoothSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v)); };
    timeSlider.textFromValueFunction = [](double v) { return getTimeFractionString (static_cast<float>(v)); };
    feedbackSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    wetSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    drySlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    duckingSlider.textFromValueFunction = [](double v) { return juce::String (juce::roundToInt (v * 100.0)); };

    updateModeButtons (static_cast<int>(audioProcessor.apvts.getRawParameterValue ("MODE")->load()));

    startTimerHz (30);
    setSize (638, 204);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DelayAudioProcessorEditor::setMode (int index)
{
    if (auto* modeParam = audioProcessor.apvts.getParameter ("MODE"))
    {
        modeParam->beginChangeGesture();
        modeParam->setValueNotifyingHost (modeParam->convertTo0to1 (static_cast<float>(index)));
        modeParam->endChangeGesture();
    }
    updateModeButtons (index);
}

// UI only - never writes the parameter, so host automation / preset recall stays in sync
void DelayAudioProcessorEditor::updateModeButtons (int selectedIndex)
{
    digitalBtn.setToggleState (selectedIndex == 0, juce::dontSendNotification);
    analogBtn.setToggleState  (selectedIndex == 1, juce::dontSendNotification);
    tapeBtn.setToggleState    (selectedIndex == 2, juce::dontSendNotification);
}

void DelayAudioProcessorEditor::timerCallback()
{
    updateModeButtons (static_cast<int>(audioProcessor.apvts.getRawParameterValue ("MODE")->load()));

    leftMeter.setLevel (audioProcessor.getLeftLevel());
    rightMeter.setLevel (audioProcessor.getRightLevel());

    float ms = static_cast<float>(timeSlider.getValue());
    float fb = static_cast<float>(feedbackSlider.getValue());
    float duck = static_cast<float>(duckingSlider.getValue());

    delayDisplay.setText (getTimeFractionString (ms));
    delayDisplay.setParams (ms, fb, duck);
}

void DelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The artwork PNG is ~75% transparent, so always paint an opaque base first;
    // otherwise the host's window colour shows through the middle.
    g.fillAll (juce::Colour (0xff050a04));

    if (bgImage.isValid())
        g.drawImage (bgImage, getLocalBounds().toFloat());
}

void DelayAudioProcessorEditor::resized()
{
    // Meters (original placement)
    leftMeter.setBounds (10, 42, 8, 128);
    rightMeter.setBounds (620, 42, 8, 128);

    // Left group - knobs share a centre line, labels share one baseline
    panSlider.setBounds (44, 84, 48, 48);
    panLabel.setBounds (36, 148, 64, 14);

    smoothSlider.setBounds (108, 84, 48, 48);
    smoothLabel.setBounds (100, 148, 64, 14);

    timeSlider.setBounds (174, 72, 72, 72);
    timeLabel.setBounds (178, 148, 64, 14);

    // Centre: tap display, mode buttons, ping-pong
    delayDisplay.setBounds (259, 50, 120, 46);

    digitalBtn.setBounds (259, 102, 40, 16);
    analogBtn.setBounds  (299, 102, 40, 16);
    tapeBtn.setBounds    (339, 102, 40, 16);

    pingPongButton.setBounds (283, 126, 14, 14);
    pingPongLabel.setBounds  (303, 126, 60, 14);

    // Right group
    feedbackSlider.setBounds (390, 72, 72, 72);
    feedbackLabel.setBounds (394, 148, 64, 14);

    drySlider.setBounds (480, 62, 40, 40);
    dryLabel.setBounds (468, 102, 64, 14);

    wetSlider.setBounds (536, 62, 40, 40);
    wetLabel.setBounds (524, 102, 64, 14);

    // Kept clear of the bottom ornament
    duckingSlider.setBounds (508, 116, 40, 40);
    duckingLabel.setBounds (493, 156, 70, 14);
}
