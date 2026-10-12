#pragma once

#include <JuceHeader.h>

class DelayAudioProcessor  : public juce::AudioProcessor
{
public:
    DelayAudioProcessor();
    ~DelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getLeftLevel();   // returns peak since last call
    float getRightLevel();

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;
    double sampleRate = 44100.0;

    juce::LinearSmoothedValue<float> smoothDelayTime;
    float duckEnvelope = 0.0f;
    float lastSmoothParam = 50.0f;

    // Feedback-loop tone shaping (per channel) and tape modulation state
    float lpState[2] { 0.0f, 0.0f };
    float hpState[2] { 0.0f, 0.0f };
    double lfoPhaseSlow = 0.0, lfoPhaseFast = 0.0;
    float modAmount = 0.0f;

    std::atomic<float> leftLevel { 0.0f };
    std::atomic<float> rightLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessor)
};
