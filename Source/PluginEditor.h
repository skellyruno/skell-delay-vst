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
    void updateModeButtons (int selectedIndex);

    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;
    juce::Image bgImage;

    SkellMeter leftMeter, rightMeter;
    SkellDisplay delayDisplay;

    juce::Slider panSlider, smoothSlider;
    juce::Slider timeSlider, feedbackSlider;
    juce::Slider drySlider, wetSlider, duckingSlider;
    juce::ToggleButton pingPongButton { "" };

    juce::TextButton digitalBtn { "DIGITAL" }, analogBtn { "ANALOG" }, tapeBtn { "TAPE" };

    juce::Label panLabel, smoothLabel, timeLabel, feedbackLabel;
    juce::Label dryLabel, wetLabel, duckingLabel, pingPongLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> panAttach, smoothAttach, timeAttach, feedbackAttach;
    std::unique_ptr<SliderAttachment> dryAttach, wetAttach, duckingAttach;
    std::unique_ptr<ButtonAttachment> pingPongAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
