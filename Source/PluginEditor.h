#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SkellLookAndFeel.h"
#include "SkellComponents.h"

class DelayAudioProcessorEditor  : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    DelayAudioProcessorEditor (DelayAudioProcessor&);
    ~DelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;
    juce::Image bgImage;

    // Meters & Display
    SkellMeter leftMeter, rightMeter;
    SkellDisplay delayDisplay;

    // Sliders
    juce::Slider panSlider, volSlider;
    juce::Slider timeSlider, feedbackSlider;
    juce::Slider mixerSlider, dryWetSlider, hiCutSlider;
    juce::ToggleButton pingPongButton { "" };

    // Mode Selector Buttons
    juce::TextButton digitalBtn { "DIGITAL" }, analogBtn { "ANALOG" }, tapeBtn { "TAPE" };

    // Labels
    juce::Label panLabel, volLabel, timeLabel, feedbackLabel;
    juce::Label mixerLabel, dryWetLabel, hiCutLabel, pingPongLabel;
    juce::Label inputHeader, delayHeader, outputHeader;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> panAttach, volAttach, timeAttach, feedbackAttach;
    std::unique_ptr<SliderAttachment> mixerAttach, dryWetAttach, hiCutAttach;
    std::unique_ptr<ButtonAttachment> pingPongAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
