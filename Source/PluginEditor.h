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
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };

    void timerCallback() override;

    void setupKnob (Knob& k, const juce::String& paramId, const juce::String& text,
                    std::function<juce::String (double)> textFn);
    void placeKnob (Knob& k, int centreX, int topY, int size);

    void setChoice (const juce::String& paramId, int index);
    void setParamValue (const juce::String& paramId, float value);
    void refreshButtons();

    void showPresetMenu();
    void stepPreset (int delta);

    void setSync (bool shouldSync);
    void applyTimeMode (bool sync);   // re-points the TIME knob at ms (free) or note value (sync)

    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;
    juce::Image bgImage;

    SkellMeter leftMeter { false }, rightMeter { true };
    SkellDisplay delayDisplay;

    // Main controls. Everything else (lo-cut, attack/sustain, swirl, sauce, wider, richer,
    // mono, destiny, cheap, smooth ...) is still a working, automatable parameter at a neutral default.
    Knob time, feedback;
    Knob hiCut, tape;
    Knob ducking;

    // One MIX knob drives DRY and WET together: first half brings the echoes in (dry stays full),
    // second half takes the dry signal out. The individual DRY / WET parameters stay automatable.
    juce::Slider mixSlider;
    juce::Label mixLabel;
    void syncMixFromParams();
    void applyMix (double mix);
    bool mixDragging = false;

    juce::TextButton syncBtn { "FREE" };
    juce::TextButton pingBtn { "PING PONG" };
    juce::TextButton feelStraightBtn { "STR" }, feelDottedBtn { "DOT" }, feelTripletBtn { "TRI" };

    juce::TextButton revBtn { "REV" };
    juce::TextButton shimBtn { "SHIM" };
    juce::TextButton abA { "A" }, abB { "B" };
    juce::TextButton presetPrev { "<" }, presetBtn { "Init" }, presetNext { ">" };

    bool timeIsSync = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
