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
    void setMode (int index);
    void updateModeButtons (int selectedIndex);
    void setFbMode (int index);
    void updateFbButtons (int selectedIndex);

    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;
    juce::Image bgImage;

    SkellMeter leftMeter, rightMeter;
    SkellDisplay delayDisplay;

    juce::Slider panSlider, smoothSlider;
    juce::Slider timeSlider, feedbackSlider;
    juce::Slider drySlider, wetSlider, duckingSlider;

    juce::TextButton digitalBtn { "DIGITAL" }, analogBtn { "ANALOG" }, tapeBtn { "TAPE" };
    juce::TextButton fbNormalBtn { "NORM" }, fbInvertBtn { "INVERT" }, fbPingBtn { "PING" };

    juce::Label panLabel, smoothLabel, timeLabel, feedbackLabel;
    juce::Label dryLabel, wetLabel, duckingLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> panAttach, smoothAttach, timeAttach, feedbackAttach;
    std::unique_ptr<SliderAttachment> dryAttach, wetAttach, duckingAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
