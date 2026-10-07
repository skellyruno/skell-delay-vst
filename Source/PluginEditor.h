#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DelayAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    DelayAudioProcessorEditor (DelayAudioProcessor&);
    ~DelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DelayAudioProcessor& audioProcessor;

    juce::Slider delayTimeSlider, feedbackSlider, mixSlider, duckingSlider, driveSlider;
    juce::ToggleButton pingPongButton { "Ping-Pong" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> delayTimeAttach, feedbackAttach, mixAttach, duckingAttach, driveAttach;
    std::unique_ptr<ButtonAttachment> pingPongAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};