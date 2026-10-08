#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessor::DelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // Cache raw atomic parameter pointers once for real-time safety
    panParam      = apvts.getRawParameterValue ("PAN");
    smoothParam   = apvts.getRawParameterValue ("SMOOTH");
    timeParam     = apvts.getRawParameterValue ("DELAY_TIME");
    feedbackParam = apvts.getRawParameterValue ("FEEDBACK");
    duckingParam  = apvts.getRawParameterValue ("DUCKING");
    dryParam      = apvts.getRawParameterValue ("DRY");
    wetParam      = apvts.getRawParameterValue ("WET");
    pingPongParam = apvts.getRawParameterValue ("PINGPONG");
    modeParam     = apvts.getRawParameterValue ("MODE");
}

DelayAudioProcessor::~DelayAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DelayAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "PAN", 1 }, "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "SMOOTH", 1 }, "Time Smooth",
        juce::NormalisableRange<float>(5.0f, 200.0f, 1.0f, 0.5f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DELAY_TIME", 1 }, "Delay Time",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 250.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "FEEDBACK", 1 }, "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.4f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DUCKING", 1 }, "Ducking",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DRY", 1 }, "Dry",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "WET", 1 }, "Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "PINGPONG", 1 }, "Ping Pong Mode", false));

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
    duckingEnv = 0.0f;

    smoothedDelaySamples.reset (sampleRate, 0.05);
    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedDry.reset (sampleRate, 0.02);
    smoothedWet.reset (sampleRate, 0.02);

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 };

    hpFilterL.prepare (spec); hpFilterR.prepare (spec);
    hpFilterL.reset();        hpFilterR.reset();
    hpFilterL.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterR.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hpFilterL.setCutoffFrequency (120.0f); hpFilterR.setCutoffFrequency (120.0f);

    lpFilterL.prepare (spec); lpFilterR.prepare (spec);
    lpFilterL.reset();        lpFilterR.reset();
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

    const float smoothMs = smoothParam->load();
    smoothedDelaySamples.reset (sr, smoothMs / 1000.0f);

    const float timeMs = timeParam->load();
    smoothedDelaySamples.setTargetValue ((timeMs / 1000.0f) * static_cast<float>(sr));

    smoothedFeedback.setTargetValue (feedbackParam->load());
    smoothedDry.setTargetValue (dryParam->load());
    smoothedWet.setTargetValue (wetParam->load());

    const float pan = panParam->load();
    const float duckingAmount = duckingParam->load();
    const bool isPingPong = pingPongParam->load() > 0.5f;
    const int mode = static_cast<int>(modeParam->load());

    float cutoff = 12000.0f;
    float drive = 1.0f;

    if (mode == 1)      { cutoff = 5500.0f; drive = 1.4f; }
    else if (mode == 2) { cutoff = 3800.0f; drive = 2.0f; }

    lpFilterL.setCutoffFrequency (cutoff);
    lpFilterR.setCutoffFrequency (cutoff);

    constexpr float quarterPi = juce::MathConstants<float>::pi * 0.25f;
    const float panL = std::cos ((pan + 1.0f) * quarterPi);
    const float panR = std::sin ((pan + 1.0f) * quarterPi);

    auto* mainLeft  = buffer.getWritePointer (0);
    auto* mainRight = buffer.getWritePointer (1);

    auto* delayLeft  = delayBuffer.getWritePointer (0);
    auto* delayRight = delayBuffer.getWritePointer (1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelaySamples = smoothedDelaySamples.getNextValue();
        const float currentFeedback     = smoothedFeedback.getNextValue();
        const float currentDry          = smoothedDry.getNextValue();
        const float currentWet          = smoothedWet.getNextValue();

        const float inPeak = (std::abs (mainLeft[sample]) + std::abs (mainRight[sample])) * 0.5f;
        duckingEnv += (inPeak - duckingEnv) * (inPeak > duckingEnv ? 0.01f : 0.0005f);
        float duckGain = juce::jlimit (0.0f, 1.0f, 1.0f - (duckingEnv * duckingAmount));

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

        feedL = std::tanh (feedL * drive) * currentFeedback;
        feedR = std::tanh (feedR * drive) * currentFeedback;

        delayLeft[writePosition]  = (mainLeft[sample]  * panL) + feedL;
        delayRight[writePosition] = (mainRight[sample] * panR) + feedR;

        const float finalWetL = wetL * duckGain * currentWet;
        const float finalWetR = wetR * duckGain * currentWet;

        mainLeft[sample]  = (mainLeft[sample]  * currentDry) + finalWetL;
        mainRight[sample] = (mainRight[sample] * currentDry) + finalWetR;

        if (++writePosition >= bufferLength)
            writePosition = 0;
    }

    leftLevel.set  (buffer.getMagnitude (0, 0, numSamples));
    rightLevel.set (buffer.getMagnitude (1, 0, numSamples));
}

float DelayAudioProcessor::getInterpolatedSample (const float* buffer, int bufferLength, float readPosition)
{
    const int index1 = static_cast<int>(readPosition);
    int index2 = index1 + 1;
    if (index2 >= bufferLength) index2 = 0;

    const float frac = readPosition - static_cast<float>(index1);
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
