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

    // Meters & Display
    SkellMeter leftMeter, rightMeter;
    SkellDisplay delayDisplay;

    // Sliders
    juce::Slider panSlider, smoothSlider;
    juce::Slider timeSlider, feedbackSlider;
    juce::Slider duckingSlider, drySlider, wetSlider;
    juce::ToggleButton pingPongButton { "" };

    // Mode Buttons
    juce::TextButton digitalBtn { "DIGITAL" }, analogBtn { "ANALOG" }, tapeBtn { "TAPE" };

    // Neon Green Labels
    juce::Label panLabel, smoothLabel, timeLabel, feedbackLabel;
    juce::Label duckingLabel, dryLabel, wetLabel, pingPongLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> panAttach, smoothAttach, timeAttach, feedbackAttach;
    std::unique_ptr<SliderAttachment> duckingAttach, dryAttach, wetAttach;
    std::unique_ptr<ButtonAttachment> pingPongAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
