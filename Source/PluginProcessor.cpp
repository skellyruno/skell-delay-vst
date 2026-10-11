#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessor::DelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #ifndef JucePlugin_IsMidiEffect
                      #ifndef JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

DelayAudioProcessor::~DelayAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DelayAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "PAN", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "SMOOTH", "Smoothing", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    juce::NormalisableRange<float> timeRange (31.25f, 2000.0f, 0.1f);
    timeRange.setSkewForCentre (250.0f);

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DELAY_TIME", "Delay Time", timeRange, 250.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback", juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DUCKING", "Ducking", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DRY", "Dry Level", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "WET", "Wet Level", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "PINGPONG", "Ping Pong", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "MODE", "Mode", juce::StringArray { "Digital", "Analog", "Tape" }, 0));

    return { params.begin(), params.end() };
}

const juce::String DelayAudioProcessor::getName() const { return JucePlugin_Name; }
bool DelayAudioProcessor::acceptsMidi() const { return false; }
bool DelayAudioProcessor::producesMidi() const { return false; }
bool DelayAudioProcessor::isMidiEffect() const { return false; }
double DelayAudioProcessor::getTailLengthSeconds() const { return 2.0; }

int DelayAudioProcessor::getNumPrograms() { return 1; }
int DelayAudioProcessor::getCurrentProgram() { return 0; }
void DelayAudioProcessor::setCurrentProgram (int) {}
const juce::String DelayAudioProcessor::getProgramName (int) { return {}; }
void DelayAudioProcessor::changeProgramName (int, const juce::String&) {}

void DelayAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = sr;

    const int maxDelaySamples = static_cast<int>(sampleRate * 2.5);
    delayBuffer.setSize (2, maxDelaySamples);
    delayBuffer.clear();

    writePosition = 0;
    duckEnvelope = 0.0f;

    // Initialize smoother ONCE here
    smoothDelayTime.reset (sampleRate, 0.05);
    smoothDelayTime.setCurrentAndTargetValue ((250.0f / 1000.0f) * static_cast<float>(sampleRate));
}

void DelayAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
        return true;

    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}
#endif

void DelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return;

    const float panVal       = apvts.getRawParameterValue ("PAN")->load();
    const float targetTimeMs = apvts.getRawParameterValue ("DELAY_TIME")->load();
    const float feedbackVal  = apvts.getRawParameterValue ("FEEDBACK")->load();
    const float duckingVal   = apvts.getRawParameterValue ("DUCKING")->load();
    const float dryVal       = apvts.getRawParameterValue ("DRY")->load();
    const float wetVal       = apvts.getRawParameterValue ("WET")->load();
    const bool  isPingPong   = apvts.getRawParameterValue ("PINGPONG")->load() > 0.5f;
    const int   modeVal      = static_cast<int>(apvts.getRawParameterValue ("MODE")->load());

    // ONLY set target value here (DO NOT call .reset() inside processBlock!)
    smoothDelayTime.setTargetValue ((targetTimeMs / 1000.0f) * static_cast<float>(sampleRate));

    const int delayBufLen = delayBuffer.getNumSamples();
    float maxPeak = 0.0f;

    const float quarterPi = juce::MathConstants<float>::pi * 0.25f;
    float leftPan  = std::cos ((panVal + 1.0f) * quarterPi);
    float rightPan = std::sin ((panVal + 1.0f) * quarterPi);

    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.010f * static_cast<float>(sampleRate)));
    const float duckRelease = 1.0f - std::exp (-1.0f / (0.150f * static_cast<float>(sampleRate)));

    auto* leftIn  = buffer.getReadPointer (0);
    auto* rightIn = numChannels > 1 ? buffer.getReadPointer (1) : leftIn;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = numChannels > 1 ? buffer.getWritePointer (1) : leftOut;

    auto* delayLeftPtr  = delayBuffer.getWritePointer (0);
    auto* delayRightPtr = delayBuffer.getWritePointer (1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelaySamples = smoothDelayTime.getNextValue();

        float readPosition = static_cast<float>(writePosition) - currentDelaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(delayBufLen);

        int readIdx1 = static_cast<int>(readPosition) % delayBufLen;
        int readIdx2 = (readIdx1 + 1) % delayBufLen;
        float frac = readPosition - static_cast<float>(static_cast<int>(readPosition));

        float delayLeft  = (1.0f - frac) * delayLeftPtr[readIdx1]  + frac * delayLeftPtr[readIdx2];
        float delayRight = (1.0f - frac) * delayRightPtr[readIdx1] + frac * delayRightPtr[readIdx2];

        if (modeVal == 1)      { delayLeft = std::tanh (delayLeft * 1.15f) * 0.95f; delayRight = std::tanh (delayRight * 1.15f) * 0.95f; }
        else if (modeVal == 2) { delayLeft = std::tanh (delayLeft * 1.40f) * 0.85f; delayRight = std::tanh (delayRight * 1.40f) * 0.85f; }

        float inLeft  = leftIn[sample];
        float inRight = rightIn[sample];

        float inLevel = std::max (std::abs (inLeft), std::abs (inRight));
        if (inLevel > duckEnvelope)
            duckEnvelope += (inLevel - duckEnvelope) * duckAttack;
        else
            duckEnvelope += (inLevel - duckEnvelope) * duckRelease;

        float duckGain = juce::jlimit (0.0f, 1.0f, 1.0f - (duckEnvelope * duckingVal));

        if (isPingPong)
        {
            float monoIn = (inLeft + inRight) * 0.5f;
            delayLeftPtr[writePosition]  = monoIn + (delayRight * feedbackVal);
            delayRightPtr[writePosition] = delayLeft * feedbackVal;
        }
        else
        {
            delayLeftPtr[writePosition]  = inLeft  + (delayLeft  * feedbackVal);
            delayRightPtr[writePosition] = inRight + (delayRight * feedbackVal);
        }

        float wetOutLeft  = delayLeft  * duckGain * wetVal * leftPan;
        float wetOutRight = delayRight * duckGain * wetVal * rightPan;

        float outLeft  = (inLeft  * dryVal) + wetOutLeft;
        float outRight = (inRight * dryVal) + wetOutRight;

        leftOut[sample]  = outLeft;
        rightOut[sample] = outRight;

        maxPeak = std::max ({ maxPeak, std::abs (outLeft), std::abs (outRight) });

        writePosition = (writePosition + 1) % delayBufLen;
    }

    leftLevel.set (maxPeak);
    rightLevel.set (maxPeak);
}

float DelayAudioProcessor::getLeftLevel() const  { return leftLevel.get(); }
float DelayAudioProcessor::getRightLevel() const { return rightLevel.get(); }

bool DelayAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* DelayAudioProcessor::createEditor()
{
    return new DelayAudioProcessorEditor (*this);
}

void DelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DelayAudioProcessor();
}
