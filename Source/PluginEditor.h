#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SkellLookAndFeel.h"

class DelayAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    DelayAudioProcessorEditor (DelayAudioProcessor&);
    ~DelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;

    // Background Image
    juce::Image bgImage;

    // Controls matching SKELL-DELAY UI
    juce::Slider panSlider, volSlider;
    juce::Slider timeSlider, feedbackSlider;
    juce::Slider mixerSlider, dryWetSlider, hiCutSlider;
    juce::ToggleButton pingPongButton { "" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> panAttach, volAttach, timeAttach, feedbackAttach;
    std::unique_ptr<SliderAttachment> mixerAttach, dryWetAttach, hiCutAttach;
    std::unique_ptr<ButtonAttachment> pingPongAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
