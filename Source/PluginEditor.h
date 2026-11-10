#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SkellLookAndFeel.h"
#include "SkellComponents.h"
#include "SkellUi.h"
#include "PresetBrowser.h"

class DelayAudioProcessorEditor  : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    DelayAudioProcessorEditor (DelayAudioProcessor&);
    ~DelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        SkellSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };

    void timerCallback() override;

    void setupKnob (Knob& k, const juce::String& paramId, const juce::String& text,
                    std::function<juce::String (double)> textFn, juce::Component* parent = nullptr);
    void placeKnob (Knob& k, int centreX, int topY, int size);

    void setChoice (const juce::String& paramId, int index);
    void setParamValue (const juce::String& paramId, float value);
    void momentary (juce::TextButton& b, const char* paramId);   // parameter is on while the button is held
    void refreshButtons();

    void toggleBrowser();
    void setAdvPage (int page);
    void applyUiSettings();    // contrast, text size
    void updateTextSizes();
    void doTap();
    void doLearn();
    void stepPreset (int delta);
    void showSaveDialog();

    void setSync (bool shouldSync);
    void applyTimeMode (bool sync);   // re-points the TIME knob at ms (free) or note value (sync)

    DelayAudioProcessor& audioProcessor;
    SkellLookAndFeel skellLookAndFeel;
    juce::Image bgImage;

    juce::Component content;   // everything lives here at 720x248 and is scaled to the window
    juce::TooltipWindow tooltipWindow { this, 700 };

    SkellMeter leftMeter { false }, rightMeter { true };
    SkellDisplay delayDisplay;

    // Main controls. Everything else (lo-cut, attack/sustain, swirl, sauce, wider, richer,
    // mono, destiny, cheap, smooth ...) is still a working, automatable parameter at a neutral default.
    Knob time, feedback;
    Knob pan, hiCut, wider;
    Knob ducking, dry, wet;

    juce::TextButton syncBtn { "FREE" };
    juce::TextButton pingBtn { "PING" };

    juce::TextButton bbdBtn { "BBD" }, killBtn { "KILL" }, bypassBtn { "BYP" }, apanBtn { "APAN" };
    juce::TextButton revBtn { "REV" };
    juce::TextButton presetPrev { "<" }, presetBtn { "Init" }, presetNext { ">" };
    juce::TextButton abBtn { "A" };      // visible A/B compare
    PresetBrowser browser { audioProcessor };
    bool browserOpen = false;
    SkellModControl modCtl { audioProcessor.apvts };

    // Advanced panel: every parameter that has no place on the front panel, in one opaque sheet
    struct AdvPanel : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (juce::Colour (0xff050c03));
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (juce::Colour (0xff39ff14).withAlpha (0.28f));
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
        }
    };
    AdvPanel advPanel, advPanel2;
    int advPage = 0;      // 0 closed, 1 sound, 2 play
    juce::Label playNote[5];
    std::vector<juce::Label*> allLabels;
    Knob advGrain, advSpectral;
    juce::TextButton stutterBtn { "STUTTER" }, tapeStopBtn { "TAPE STOP" }, revThrowBtn { "REV THROW" }, tapBtn { "TAP" }, learnBtn { "LEARN" };
    juce::Label playInfo, hintLabel;
    juce::TextButton hcBtn { "HIGH CONTRAST" }, bigTextBtn { "LARGE TEXT" };
    double tapTimes[6] {};  int tapCount = 0;
    double lastUndoStepMs = 0.0;
    juce::String tapInfo;
    juce::TextButton advBtn { "ADV" };
    Knob advLoCut, advAttack, advSustain, advSauce, advRicher, advShimmer,
         advSmooth, advDuckRel, advGateDepth, advPanDepth, advModRate, advRepeats, advFbMode;
    juce::TextButton advCheap { "CHEAP" }, advMono { "MONO" }, advDestiny { "DESTINY" };

    bool timeIsSync = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessorEditor)
};
