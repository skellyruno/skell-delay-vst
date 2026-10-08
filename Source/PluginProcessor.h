#pragma once

#include <JuceHeader.h>

class DelayAudioProcessor  : public juce::AudioProcessor
{
public:
    DelayAudioProcessor();
    ~DelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Peak level meters read by PluginEditor timer
    float getLeftLevel()  const { return leftLevel.get(); }
    float getRightLevel() const { return rightLevel.get(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Audio Buffer & State
    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;

    // Parameter Smoothers
    juce::LinearSmoothedValue<float> smoothedDelaySamples;
    juce::LinearSmoothedValue<float> smoothedFeedback;
    juce::LinearSmoothedValue<float> smoothedMix;

    // Feedback Loop Filters
    juce::dsp::StateVariableTPTFilter<float> hpFilterL, hpFilterR;
    juce::dsp::StateVariableTPTFilter<float> lpFilterL, lpFilterR;

    // Ducking State
    float duckingEnv = 0.0f;

    // Thread-safe level tracking for GUI meters
    juce::Atomic<float> leftLevel { 0.0f };
    juce::Atomic<float> rightLevel { 0.0f };

    float getInterpolatedSample (const float* buffer, int bufferLength, float readPosition);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessor)
};
