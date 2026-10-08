#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessorEditor::DelayAudioProcessorEditor (DelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Load embedded background image
    bgImage = juce::ImageCache::getFromHashCode (
        juce::String ("skell_bg_png").hashCode()); 
    
    // Fallback if hash lookup isn't used directly:
    if (bgImage.isNull())
        bgImage = juce::ImageFileFormat::loadFrom (
            BinaryData::skell_bg_png, BinaryData::skell_bg_pngSize);

    // Apply custom gothic neon LookAndFeel
    setLookAndFeel (&skellLookAndFeel);

    auto setupKnob = [this](juce::Slider& s) {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); // Text printed on BG
        addAndMakeVisible (s);
    };

    // Input Group
    setupKnob (panSlider);
    setupKnob (volSlider);

    // Delay Core Group (Large Knobs)
    setupKnob (timeSlider);
    setupKnob (feedbackSlider);

    // Output Group
    setupKnob (mixerSlider);
    setupKnob (dryWetSlider);
    setupKnob (hiCutSlider);

    // Ping Pong Toggle
    addAndMakeVisible (pingPongButton);

    // Connect to Parameters
    panAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "PAN", panSlider);
    volAttach      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "VOL", volSlider);
    timeAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DELAY_TIME", timeSlider);
    feedbackAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    mixerAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIXER", mixerSlider);
    dryWetAttach   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX", dryWetSlider);
    hiCutAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "HICUT", hiCutSlider);
    pingPongAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "PINGPONG", pingPongButton);

    // Match aspect ratio of background image (e.g. 1000x320)
    setSize (1000, 320);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void DelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (bgImage.isValid())
    {
        g.drawImage (bgImage, getLocalBounds().toFloat());
    }
    else
    {
        g.fillAll (juce::Colour (0xff0a0d0a)); // Dark fallback background
    }
}

void DelayAudioProcessorEditor::resized()
{
    // Exact pixel placement matching SKELL-DELAY panels:
    
    // 1. INPUT PANEL (Left)
    panSlider.setBounds (70, 150, 70, 70);
    volSlider.setBounds (165, 150, 70, 70);

    // 2. DELAY PANEL (Center - Large Knobs)
    timeSlider.setBounds (315, 125, 105, 105);
    feedbackSlider.setBounds (575, 125, 105, 105);
    pingPongButton.setBounds (485, 200, 30, 30);

    // 3. OUTPUT PANEL (Right)
    mixerSlider.setBounds (735, 150, 65, 65);
    dryWetSlider.setBounds (810, 150, 65, 65);
    hiCutSlider.setBounds (885, 150, 65, 65);
}
