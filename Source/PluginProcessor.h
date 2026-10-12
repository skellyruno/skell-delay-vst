#pragma once

#include <JuceHeader.h>

// Tempo-synced note values, ordered by length (beats = quarter notes)
struct SyncDivision { const char* name; float beats; };
inline constexpr SyncDivision kSyncDivisions[] = {
    { "1/64",  0.0625f }, { "1/32T", 1.0f / 12.0f }, { "1/32",  0.125f },
    { "1/16T", 1.0f / 6.0f }, { "1/16", 0.25f },     { "1/8T",  1.0f / 3.0f },
    { "1/16.", 0.375f },  { "1/8",   0.5f },         { "1/4T",  2.0f / 3.0f },
    { "1/8.",  0.75f },   { "1/4",   1.0f },         { "1/2T",  4.0f / 3.0f },
    { "1/4.",  1.5f },    { "1/2",   2.0f },         { "1/2.",  3.0f },
    { "1/1",   4.0f }
};
inline constexpr int kNumSyncDivisions = static_cast<int> (sizeof (kSyncDivisions) / sizeof (kSyncDivisions[0]));
inline constexpr int kDefaultSyncDivision = 7;   // 1/8

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

    // Actual delay time in use (ms), whether free or tempo-synced - for the GUI
    float getEffectiveDelayMs() const { return effectiveDelayMs.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float computeDelayMs() const;   // free ms, or note value at the host tempo

    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;
    double sampleRate = 44100.0;

    juce::LinearSmoothedValue<float> smoothDelayTime;
    float duckEnvelope = 0.0f;
    float lastSmoothParam = 50.0f;

    // Feedback-loop tone shaping (per channel) and tape modulation state
    float lpState[2] { 0.0f, 0.0f };
    float hpState[2] { 0.0f, 0.0f };
    float lp2State[2] { 0.0f, 0.0f };   // second low-pass pole (Analog/BBD)
    double hostBpm = 120.0;
    std::atomic<float> effectiveDelayMs { 250.0f };
    double lfoPhaseSlow = 0.0, lfoPhaseFast = 0.0;
    float wowDepth = 0.0f, flutterDepth = 0.0f;   // smoothed modulation depths (samples)

    std::atomic<float> leftLevel { 0.0f };
    std::atomic<float> rightLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessor)
};
