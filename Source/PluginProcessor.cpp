#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessor::DelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

DelayAudioProcessor::~DelayAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DelayAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Delay Time (ms)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DELAY_TIME", 1 }, "Delay Time",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 250.0f));

    // Feedback
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "FEEDBACK", 1 }, "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.4f));

    // Mix (Dry/Wet)
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "MIX", 1 }, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    // Pan & Volume
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "PAN", 1 }, "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "VOL", 1 }, "Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

    // Mixer & Hi-Cut
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "MIXER", 1 }, "Mixer",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "HICUT", 1 }, "Hi-Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 10.0f, 0.3f), 8000.0f));

    // Ping Pong Switch
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "PINGPONG", 1 }, "Ping Pong Mode", false));

    // Delay Mode: 0 = Digital, 1 = Analog, 2 = Tape
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "MODE", 1 }, "Delay Mode",
        juce::StringArray { "Digital", "Analog", "Tape" }, 0));

    return { params.begin(), params.end() };
}

void DelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int maxDelaySamples = static_cast<int>(sampleRate * 3.0);
    delayBuffer.setSize (2, maxDelaySamples);
    delayBuffer.clear();
    writePosition = 0;

    smoothedDelaySamples.reset (sampleRate, 0.05);
    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 };

    hpFilterL.prepare (spec); hpFilterR.prepare (spec);
    hpFilterL.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterR.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterL.setCutoffFrequency (120.0f); hpFilterR.setCutoffFrequency (120.0f);

    lpFilterL.prepare (spec); lpFilterR.prepare (spec);
    lpFilterL.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lpFilterR.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void DelayAudioProcessor::releaseResources() {}

bool DelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void DelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();
    const double sr = getSampleRate();

    float timeMs = apvts.getRawParameterValue ("DELAY_TIME")->load();
    smoothedDelaySamples.setTargetValue ((timeMs / 1000.0f) * static_cast<float>(sr));
    smoothedFeedback.setTargetValue (apvts.getRawParameterValue ("FEEDBACK")->load());
    smoothedMix.setTargetValue (apvts.getRawParameterValue ("MIX")->load());

    const bool isPingPong = apvts.getRawParameterValue ("PINGPONG")->load() > 0.5f;
    const int mode = static_cast<int>(apvts.getRawParameterValue ("MODE")->load());
    const float hiCutFreq = apvts.getRawParameterValue ("HICUT")->load();
    const float vol = apvts.getRawParameterValue ("VOL")->load();

    // Mode-specific character adjustments
    float cutoff = hiCutFreq;
    float drive = 1.0f;

    if (mode == 1) // Analog Mode: Warmer, rolled-off high end, mild drive
    {
        cutoff = juce::jmin (hiCutFreq, 5500.0f);
        drive = 1.4f;
    }
    else if (mode == 2) // Tape Mode: Darker, rich saturation
    {
        cutoff = juce::jmin (hiCutFreq, 3800.0f);
        drive = 2.0f;
    }

    lpFilterL.setCutoffFrequency (cutoff);
    lpFilterR.setCutoffFrequency (cutoff);

    auto* mainLeft  = buffer.getWritePointer (0);
    auto* mainRight = buffer.getWritePointer (1);

    auto* delayLeft  = delayBuffer.getWritePointer (0);
    auto* delayRight = delayBuffer.getWritePointer (1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelaySamples = smoothedDelaySamples.getNextValue();
        const float currentFeedback     = smoothedFeedback.getNextValue();
        const float currentMix          = smoothedMix.getNextValue();

        float readPosL = static_cast<float>(writePosition) - currentDelaySamples;
        if (readPosL < 0.0f) readPosL += static_cast<float>(bufferLength);

        float wetL = getInterpolatedSample (delayLeft, bufferLength, readPosL);
        float wetR = getInterpolatedSample (delayRight, bufferLength, readPosL);

        wetL = hpFilterL.processSample (0, wetL);
        wetL = lpFilterL.processSample (0, wetL);
        wetR = hpFilterR.processSample (0, wetR);
        wetR = lpFilterR.processSample (0, wetR);

        float feedL = isPingPong ? wetR : wetL;
        float feedR = isPingPong ? wetL : wetR;

        // Apply saturation curve based on Mode
        feedL = std::tanh (feedL * drive) * currentFeedback;
        feedR = std::tanh (feedR * drive) * currentFeedback;

        delayLeft[writePosition]  = mainLeft[sample] + feedL;
        delayRight[writePosition] = mainRight[sample] + feedR;

        mainLeft[sample]  = ((mainLeft[sample]  * (1.0f - currentMix)) + (wetL * currentMix)) * vol;
        mainRight[sample] = ((mainRight[sample] * (1.0f - currentMix)) + (wetR * currentMix)) * vol;

        if (++writePosition >= bufferLength)
            writePosition = 0;
    }

    leftLevel.set  (buffer.getMagnitude (0, 0, numSamples));
    rightLevel.set (buffer.getMagnitude (1, 0, numSamples));
}

float DelayAudioProcessor::getInterpolatedSample (const float* buffer, int bufferLength, float readPosition)
{
    int index1 = static_cast<int>(readPosition);
    int index2 = index1 + 1;
    if (index2 >= bufferLength) index2 = 0;

    float frac = readPosition - static_cast<float>(index1);
    return buffer[index1] + frac * (buffer[index2] - buffer[index1]);
}

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
