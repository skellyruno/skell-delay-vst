#pragma once
#include <JuceHeader.h>

class DelayAudioProcessor : public juce::AudioProcessor
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

    float getLeftLevel() const { return leftLevel.get(); }
    float getRightLevel() const { return rightLevel.get(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float getInterpolatedSample (const float* buffer, int bufferLength, float readPosition);

    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;

    // Fast atomic parameter caches
    std::atomic<float>* panParam      = nullptr;
    std::atomic<float>* smoothParam   = nullptr;
    std::atomic<float>* timeParam     = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* duckingParam  = nullptr;
    std::atomic<float>* dryParam      = nullptr;
    std::atomic<float>* wetParam      = nullptr;
    std::atomic<float>* pingPongParam = nullptr;
    std::atomic<float>* modeParam     = nullptr;

    juce::LinearSmoothedValue<float> smoothedDelaySamples;
    juce::LinearSmoothedValue<float> smoothedFeedback;
    juce::LinearSmoothedValue<float> smoothedDry;
    juce::LinearSmoothedValue<float> smoothedWet;

    juce::dsp::StateVariableTPTFilter<float> hpFilterL, hpFilterR;
    juce::dsp::StateVariableTPTFilter<float> lpFilterL, lpFilterR;

    float duckingEnv = 0.0f;
    juce::Atomic<float> leftLevel { 0.0f };
    juce::Atomic<float> rightLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessor)
};
