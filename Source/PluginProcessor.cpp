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

DelayAudioProcessor::~DelayAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DelayAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "PAN", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "SMOOTH", "Smoothing", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    // TIME range with 250ms (1/8 note) centered at 12 o'clock
    juce::NormalisableRange<float> timeRange (31.25f, 2000.0f, 0.1f);
    timeRange.setSkewForCentre (250.0f);

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DELAY_TIME", "Delay Time", timeRange, 250.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

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

    const int maxDelaySamples = static_cast<int>(sampleRate * 2.0);
    delayBuffer.setSize (2, maxDelaySamples);
    delayBuffer.clear();

    writePosition = 0;
    duckEnvelope = 0.0f;

    smoothDelayTime.reset (sampleRate, 0.05);
}

void DelayAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void DelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    const float panVal       = apvts.getRawParameterValue ("PAN")->load();
    const float smoothVal    = apvts.getRawParameterValue ("SMOOTH")->load();
    const float targetTimeMs = apvts.getRawParameterValue ("DELAY_TIME")->load();
    const float feedbackVal  = juce::jmin (0.98f, apvts.getRawParameterValue ("FEEDBACK")->load());
    const float duckingVal   = apvts.getRawParameterValue ("DUCKING")->load();
    const float dryVal       = apvts.getRawParameterValue ("DRY")->load();
    const float wetVal       = apvts.getRawParameterValue ("WET")->load();
    const bool  isPingPong   = apvts.getRawParameterValue ("PINGPONG")->load() > 0.5f;
    const int   modeVal      = static_cast<int>(apvts.getRawParameterValue ("MODE")->load());

    float rampTime = juce::jmap (smoothVal, 0.0f, 100.0f, 0.005f, 0.200f);
    smoothDelayTime.reset (sampleRate, rampTime);
    smoothDelayTime.setTargetValue ((targetTimeMs / 1000.0f) * static_cast<float>(sampleRate));

    const int delayBufLen = delayBuffer.getNumSamples();
    float maxLeftPeak = 0.0f;
    float maxRightPeak = 0.0f;

    const float quarterPi = juce::MathConstants<float>::pi * 0.25f;
    float leftPan  = std::cos ((panVal + 1.0f) * quarterPi);
    float rightPan = std::sin ((panVal + 1.0f) * quarterPi);

    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.010f * static_cast<float>(sampleRate)));
    const float duckRelease = 1.0f - std::exp (-1.0f / (0.150f * static_cast<float>(sampleRate)));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelaySamples = smoothDelayTime.getNextValue();

        float readPosition = static_cast<float>(writePosition) - currentDelaySamples;
        if (readPosition < 0.0f)
            readPosition += static_cast<float>(delayBufLen);

        int readIdx1 = static_cast<int>(readPosition);
        int readIdx2 = (readIdx1 + 1) % delayBufLen;
        float frac = readPosition - static_cast<float>(readIdx1);

        float delayLeft = (1.0f - frac) * delayBuffer.getSample (0, readIdx1) + frac * delayBuffer.getSample (0, readIdx2);
        float delayRight = (1.0f - frac) * delayBuffer.getSample (1, readIdx1) + frac * delayBuffer.getSample (1, readIdx2);

        if (modeVal == 1) // Analog
        {
            delayLeft  = std::tanh (delayLeft * 1.1f) * 0.95f;
            delayRight = std::tanh (delayRight * 1.1f) * 0.95f;
        }
        else if (modeVal == 2) // Tape
        {
            delayLeft  = std::tanh (delayLeft * 1.35f) * 0.85f;
            delayRight = std::tanh (delayRight * 1.35f) * 0.85f;
        }

        float inLeft  = totalNumInputChannels > 0 ? buffer.getSample (0, sample) : 0.0f;
        float inRight = totalNumInputChannels > 1 ? buffer.getSample (1, sample) : inLeft;

        float inLevel = std::max (std::abs (inLeft), std::abs (inRight));
        if (inLevel > duckEnvelope)
            duckEnvelope += (inLevel - duckEnvelope) * duckAttack;
        else
            duckEnvelope += (inLevel - duckEnvelope) * duckRelease;

        float duckGain = juce::jlimit (0.0f, 1.0f, 1.0f - (duckEnvelope * duckingVal));

        if (isPingPong)
        {
            float monoIn = (inLeft + inRight) * 0.5f;
            delayBuffer.setSample (0, writePosition, monoIn + (delayRight * feedbackVal));
            delayBuffer.setSample (1, writePosition, delayLeft * feedbackVal);
        }
        else
        {
            delayBuffer.setSample (0, writePosition, inLeft  + (delayLeft  * feedbackVal));
            delayBuffer.setSample (1, writePosition, inRight + (delayRight * feedbackVal));
        }

        float wetOutLeft  = delayLeft  * duckGain * wetVal * leftPan;
        float wetOutRight = delayRight * duckGain * wetVal * rightPan;

        float outLeft  = (inLeft  * dryVal) + wetOutLeft;
        float outRight = (inRight * dryVal) + wetOutRight;

        if (totalNumInputChannels > 0) buffer.setSample (0, sample, outLeft);
        if (totalNumInputChannels > 1) buffer.setSample (1, sample, outRight);

        maxLeftPeak  = std::max (maxLeftPeak,  std::abs (outLeft));
        maxRightPeak = std::max (maxRightPeak, std::abs (outRight));

        writePosition = (writePosition + 1) % delayBufLen;
    }

    leftLevel.set (maxLeftPeak);
    rightLevel.set (maxRightPeak);
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
