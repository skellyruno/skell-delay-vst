#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessorEditor::DelayAudioProcessorEditor (DelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    bgImage = juce::ImageFileFormat::loadFrom (
        BinaryData::skell_bg_png, BinaryData::skell_bg_pngSize);

    setLookAndFeel (&skellLookAndFeel);

    // Level Meters & Display Screen
    addAndMakeVisible (leftMeter);
    addAndMakeVisible (rightMeter);
    addAndMakeVisible (delayDisplay);

    // Setup helper for Sliders and Labels
    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (15.0f, juce::Font::bold | juce::Font::italic));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    auto setupHeader = [this](juce::Label& l, const juce::String& text) {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (20.0f, juce::Font::bold | juce::Font::italic));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    // Headers
    setupHeader (inputHeader, "INPUT");
    setupHeader (delayHeader, "DELAY");
    setupHeader (outputHeader, "OUTPUT");

    // Input Group
    setupKnob (panSlider, panLabel, "PAN");
    setupKnob (volSlider, volLabel, "VOL");

    // Center Group
    setupKnob (timeSlider, timeLabel, "TIME");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");

    // Output Group
    setupKnob (mixerSlider, mixerLabel, "MIXER");
    setupKnob (dryWetSlider, dryWetLabel, "DRY/WET");
    setupKnob (hiCutSlider, hiCutLabel, "HI-CUT");

    // Ping Pong Toggle & Label
    addAndMakeVisible (pingPongButton);
    pingPongLabel.setText ("PING PONG", juce::dontSendNotification);
    pingPongLabel.setFont (juce::Font (13.0f, juce::Font::bold | juce::Font::italic));
    pingPongLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
    pingPongLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pingPongLabel);

    // Mode Selector Buttons
    addAndMakeVisible (digitalBtn);
    addAndMakeVisible (analogBtn);
    addAndMakeVisible (tapeBtn);

    // Attachments
    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    volAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "VOL", volSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    mixerAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIXER", mixerSlider);
    dryWetAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX", dryWetSlider);
    hiCutAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "HICUT", hiCutSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    // Update display when time changes
    timeSlider.onValueChange = [this]() {
        float val = static_cast<float>(timeSlider.getValue());
        delayDisplay.setText (juce::String (val, 0) + " ms");
    };

    startTimerHz (30); // 30 FPS meter update
    setSize (1000, 320);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
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
    leftMeter.setBounds (12, 65, 14, 200);
    rightMeter.setBounds (974, 65, 14, 200);

    // --- 2. INPUT SECTION ---
    inputHeader.setBounds (60, 68, 180, 25);
    
    panSlider.setBounds (65, 120, 75, 75);
    panLabel.setBounds (52, 200, 100, 20);

    volSlider.setBounds (160, 120, 75, 75);
    volLabel.setBounds (147, 200, 100, 20);

    // --- 3. CENTER DELAY SECTION ---
    delayHeader.setBounds (410, 68, 180, 25);

    // Time Display Box
    delayDisplay.setBounds (435, 105, 130, 34);

    // Digital / Analog / Tape Buttons
    digitalBtn.setBounds (420, 147, 50, 18);
    analogBtn.setBounds (475, 147, 50, 18);
    tapeBtn.setBounds (530, 147, 50, 18);

    // Ping Pong Switch
    pingPongButton.setBounds (488, 172, 24, 24);
    pingPongLabel.setBounds (435, 198, 130, 20);

    // Large Center Knobs
    timeSlider.setBounds (285, 110, 115, 115);
    timeLabel.setBounds (292, 228, 100, 20);

    feedbackSlider.setBounds (598, 110, 115, 115);
    feedbackLabel.setBounds (605, 228, 100, 20);

    // --- 4. OUTPUT SECTION ---
    outputHeader.setBounds (730, 68, 230, 25);

    mixerSlider.setBounds (735, 125, 70, 70);
    mixerLabel.setBounds (720, 200, 100, 20);

    dryWetSlider.setBounds (815, 125, 70, 70);
    dryWetLabel.setBounds (800, 200, 100, 20);

    hiCutSlider.setBounds (895, 125, 70, 70);
    hiCutLabel.setBounds (880, 200, 100, 20);
}
