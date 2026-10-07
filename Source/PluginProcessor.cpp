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

    // Delay Time Parameter
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DELAY_TIME", 1 }, "Delay Time",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 250.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float val, int) { return juce::String(val, 0) + " ms"; })));

    // Feedback Parameter
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "FEEDBACK", 1 }, "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.4f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float val, int) { return juce::String (static_cast<int>(val * 100)) + " %"; })));

    // Mix Parameter
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "MIX", 1 }, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float val, int) { return juce::String (static_cast<int>(val * 100)) + " %"; })));

    // Ping Pong Switch
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "PINGPONG", 1 }, "Ping Pong Mode", false));

    // Ducking Parameter
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DUCKING", 1 }, "Ducking",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float val, int) { return juce::String (static_cast<int>(val * 100)) + " %"; })));

    // Color Drive Parameter
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DRIVE", 1 }, "Color Drive",
        juce::NormalisableRange<float>(1.0f, 5.0f, 0.05f), 1.0f));

    return { params.begin(), params.end() };
}

void DelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Allocate 3 seconds max delay buffer length
    const int maxDelaySamples = static_cast<int>(sampleRate * 3.0);
    delayBuffer.setSize(2, maxDelaySamples);
    delayBuffer.clear();
    writePosition = 0;

    // Reset smoothers
    smoothedDelaySamples.reset(sampleRate, 0.05); // 50ms ramp
    smoothedFeedback.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);

    // Setup TPT filters (1 channel per filter instance)
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 };

    hpFilterL.prepare(spec);
    hpFilterR.prepare(spec);
    hpFilterL.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterR.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterL.setCutoffFrequency(150.0f);
    hpFilterR.setCutoffFrequency(150.0f);

    lpFilterL.prepare(spec);
    lpFilterR.prepare(spec);
    lpFilterL.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lpFilterR.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lpFilterL.setCutoffFrequency(5000.0f);
    lpFilterR.setCutoffFrequency(5000.0f);
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

    // Set parameter smooth target values
    float timeMs = apvts.getRawParameterValue("DELAY_TIME")->load();
    smoothedDelaySamples.setTargetValue((timeMs / 1000.0f) * static_cast<float>(sr));
    smoothedFeedback.setTargetValue(apvts.getRawParameterValue("FEEDBACK")->load());
    smoothedMix.setTargetValue(apvts.getRawParameterValue("MIX")->load());

    const bool isPingPong = apvts.getRawParameterValue("PINGPONG")->load() > 0.5f;
    const float duckingAmount = apvts.getRawParameterValue("DUCKING")->load();
    const float drive = apvts.getRawParameterValue("DRIVE")->load();

    auto* mainLeft = buffer.getWritePointer(0);
    auto* mainRight = buffer.getWritePointer(1);

    auto* delayLeft = delayBuffer.getWritePointer(0);
    auto* delayRight = delayBuffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelaySamples = smoothedDelaySamples.getNextValue();
        const float currentFeedback = smoothedFeedback.getNextValue();
        const float currentMix = smoothedMix.getNextValue();

        // Calculate fractional delay read indices
        float readPosL = static_cast<float>(writePosition) - currentDelaySamples;
        if (readPosL < 0.0f) readPosL += static_cast<float>(bufferLength);

        float readPosR = readPosL;

        // Fetch interpolated wet samples
        float wetL = getInterpolatedSample(delayLeft, bufferLength, readPosL);
        float wetR = getInterpolatedSample(delayRight, bufferLength, readPosR);

        // Process TPT filters on feedback loop
        wetL = hpFilterL.processSample(0, wetL);
        wetL = lpFilterL.processSample(0, wetL);

        wetR = hpFilterR.processSample(0, wetR);
        wetR = lpFilterR.processSample(0, wetR);

        // Auto-Ducking Logic
        float dryEnvelope = (std::abs(mainLeft[sample]) + std::abs(mainRight[sample])) * 0.5f;
        duckingEnv += (dryEnvelope - duckingEnv) * 0.002f;
        float duckGain = 1.0f - (duckingEnv * duckingAmount);
        
        float finalWetL = wetL * duckGain;
        float finalWetR = wetR * duckGain;

        // Routing / Ping-Pong Configuration
        float feedL = isPingPong ? wetR : wetL;
        float feedR = isPingPong ? wetL : wetR;

        // Color/Saturation on Feedback Loop
        feedL = std::tanh(feedL * drive) * currentFeedback;
        feedR = std::tanh(feedR * drive) * currentFeedback;

        // Write back into delay buffer
        delayLeft[writePosition]  = mainLeft[sample] + feedL;
        delayRight[writePosition] = mainRight[sample] + feedR;

        // Mix dry/wet into output
        mainLeft[sample]  = (mainLeft[sample] * (1.0f - currentMix)) + (finalWetL * currentMix);
        mainRight[sample] = (mainRight[sample] * (1.0f - currentMix)) + (finalWetR * currentMix);

        if (++writePosition >= bufferLength)
            writePosition = 0;
    }
}

float DelayAudioProcessor::getInterpolatedSample(const float* buffer, int bufferLength, float readPosition)
{
    int index1 = static_cast<int>(readPosition);
    int index2 = index1 + 1;
    if (index2 >= bufferLength) index2 = 0;

    float frac = readPosition - static_cast<float>(index1);
    return buffer[index1] + frac * (buffer[index2] - buffer[index1]);
}

// THIS METHOD RESOLVES THE LINKER ERROR:
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
