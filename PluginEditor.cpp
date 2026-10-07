#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessorEditor::DelayAudioProcessorEditor (DelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    auto setupSlider = [this](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        addAndMakeVisible(s);
    };

    setupSlider(delayTimeSlider);
    setupSlider(feedbackSlider);
    setupSlider(mixSlider);
    setupSlider(duckingSlider);
    setupSlider(driveSlider);
    addAndMakeVisible(pingPongButton);

    delayTimeAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", delayTimeSlider);
    feedbackAttach  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    mixAttach       = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);
    duckingAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DUCKING", duckingSlider);
    driveAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRIVE", driveSlider);
    pingPongAttach  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    setSize (500, 300);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor() {}

void DelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24)); // Dark matte background
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawFittedText ("CUSTOM VST3 DELAY", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);
}

void DelayAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(30); // Leave space for header

    auto row = area.removeFromTop(120);
    delayTimeSlider.setBounds(row.removeFromLeft(90));
    feedbackSlider.setBounds(row.removeFromLeft(90));
    mixSlider.setBounds(row.removeFromLeft(90));
    duckingSlider.setBounds(row.removeFromLeft(90));
    driveSlider.setBounds(row.removeFromLeft(90));

    pingPongButton.setBounds(area.removeFromTop(40).removeFromLeft(120));
}