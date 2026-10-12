#include "PluginProcessor.h"
#include "PluginEditor.h"

DelayAudioProcessor::DelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
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
    lpState[0] = lpState[1] = hpState[0] = hpState[1] = 0.0f;
    lfoPhaseSlow = lfoPhaseFast = 0.0;
    modAmount = 0.0f;

    // Initialise smoother from the current parameter values (not hardcoded defaults)
    lastSmoothParam = apvts.getRawParameterValue ("SMOOTH")->load();
    smoothDelayTime.reset (sampleRate, juce::jmax (0.001, lastSmoothParam * 0.001));
    smoothDelayTime.setCurrentAndTargetValue (
        (apvts.getRawParameterValue ("DELAY_TIME")->load() / 1000.0f) * static_cast<float>(sampleRate));
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

    // SMOOTH (0-100) = glide time in ms. Only re-time the ramp when the knob moves,
    // preserving the current value so there is no jump/click.
    const float smoothParam = apvts.getRawParameterValue ("SMOOTH")->load();
    if (smoothParam != lastSmoothParam)
    {
        lastSmoothParam = smoothParam;
        const float current = smoothDelayTime.getCurrentValue();
        smoothDelayTime.reset (sampleRate, juce::jmax (0.001, smoothParam * 0.001));
        smoothDelayTime.setCurrentAndTargetValue (current);
    }

    smoothDelayTime.setTargetValue ((targetTimeMs / 1000.0f) * static_cast<float>(sampleRate));

    const int delayBufLen = delayBuffer.getNumSamples();
    float peakL = 0.0f, peakR = 0.0f;

    // Balance-style pan: centre = unity on both sides (no -3 dB dip on the wet signal)
    const float leftPan  = panVal > 0.0f ? 1.0f - panVal : 1.0f;
    const float rightPan = panVal < 0.0f ? 1.0f + panVal : 1.0f;

    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.010f * static_cast<float>(sampleRate)));
    const float duckRelease = 1.0f - std::exp (-1.0f / (0.150f * static_cast<float>(sampleRate)));

    // Per-mode character of the feedback loop: each repeat gets darker / thinner / more saturated
    //   Digital: clean, near-transparent.  Analog: BBD-ish dark repeats + soft drive.
    //   Tape: darker still, heavier drive, plus wow & flutter on the delay time.
    float lpHz = 16000.0f, hpHz = 20.0f, drive = 0.0f, modTarget = 0.0f;
    if (modeVal == 1)      { lpHz = 5500.0f; hpHz = 120.0f; drive = 1.2f; }
    else if (modeVal == 2) { lpHz = 3600.0f; hpHz = 170.0f; drive = 1.5f; modTarget = 1.0f; }

    const float sr = static_cast<float>(sampleRate);
    lpHz = juce::jmin (lpHz, sr * 0.45f);
    const float lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * lpHz / sr);
    const float hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hpHz / sr);

    const double slowInc = juce::MathConstants<double>::twoPi * 0.55 / sampleRate;   // wow
    const double fastInc = juce::MathConstants<double>::twoPi * 6.3  / sampleRate;   // flutter
    const float wowDepth     = 0.0007f * sr;   // +-0.7 ms
    const float flutterDepth = 0.00012f * sr;  // +-0.12 ms

    auto* leftIn  = buffer.getReadPointer (0);
    auto* rightIn = (numChannels > 1 && getTotalNumInputChannels() > 1) ? buffer.getReadPointer (1) : leftIn;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = numChannels > 1 ? buffer.getWritePointer (1) : leftOut;

    auto* delayLeftPtr  = delayBuffer.getWritePointer (0);
    auto* delayRightPtr = delayBuffer.getWritePointer (1);

    // 4-point cubic Hermite read: much less high-frequency loss than linear interpolation
    auto readCubic = [delayBufLen] (const float* buf, int i1, float f)
    {
        const float y0 = buf[(i1 - 1 + delayBufLen) % delayBufLen];
        const float y1 = buf[i1];
        const float y2 = buf[(i1 + 1) % delayBufLen];
        const float y3 = buf[(i1 + 2) % delayBufLen];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };

    // High-pass then low-pass then soft saturation (unity small-signal gain)
    auto shapeLoop = [&] (float x, int ch)
    {
        lpState[ch] += lpCoef * (x - lpState[ch]);
        float y = lpState[ch];
        hpState[ch] += hpCoef * (y - hpState[ch]);
        y -= hpState[ch];
        if (drive > 0.0f)
            y = std::tanh (y * drive) / drive;
        return y;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        modAmount += 0.0005f * (modTarget - modAmount);   // fade tape wobble in/out when switching modes
        const float mod = modAmount * (wowDepth * static_cast<float>(std::sin (lfoPhaseSlow))
                                     + flutterDepth * static_cast<float>(std::sin (lfoPhaseFast)));
        lfoPhaseSlow += slowInc;  if (lfoPhaseSlow >= juce::MathConstants<double>::twoPi) lfoPhaseSlow -= juce::MathConstants<double>::twoPi;
        lfoPhaseFast += fastInc;  if (lfoPhaseFast >= juce::MathConstants<double>::twoPi) lfoPhaseFast -= juce::MathConstants<double>::twoPi;

        const float currentDelaySamples = smoothDelayTime.getNextValue() + mod;

        float readPosition = static_cast<float>(writePosition) - currentDelaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(delayBufLen);

        const int   readIdx = static_cast<int>(readPosition) % delayBufLen;
        const float frac    = readPosition - std::floor (readPosition);

        // Tapped, filtered, saturated: used both for the output and for the feedback path
        const float delayLeft  = shapeLoop (readCubic (delayLeftPtr,  readIdx, frac), 0);
        const float delayRight = shapeLoop (readCubic (delayRightPtr, readIdx, frac), 1);

        const float inLeft  = leftIn[sample];
        const float inRight = rightIn[sample];

        const float inLevel = std::max (std::abs (inLeft), std::abs (inRight));
        if (inLevel > duckEnvelope)
            duckEnvelope += (inLevel - duckEnvelope) * duckAttack;
        else
            duckEnvelope += (inLevel - duckEnvelope) * duckRelease;

        const float duckGain = juce::jlimit (0.0f, 1.0f, 1.0f - (duckEnvelope * duckingVal));

        if (isPingPong)
        {
            const float monoIn = (inLeft + inRight) * 0.5f;
            delayLeftPtr[writePosition]  = monoIn + (delayRight * feedbackVal);
            delayRightPtr[writePosition] = delayLeft * feedbackVal;
        }
        else
        {
            delayLeftPtr[writePosition]  = inLeft  + (delayLeft  * feedbackVal);
            delayRightPtr[writePosition] = inRight + (delayRight * feedbackVal);
        }

        const float outLeft  = (inLeft  * dryVal) + delayLeft  * duckGain * wetVal * leftPan;
        const float outRight = (inRight * dryVal) + delayRight * duckGain * wetVal * rightPan;

        leftOut[sample]  = outLeft;
        rightOut[sample] = outRight;

        peakL = std::max (peakL, std::abs (outLeft));
        peakR = std::max (peakR, std::abs (outRight));

        writePosition = (writePosition + 1) % delayBufLen;
    }

    // Accumulate until the GUI reads (GUI resets via exchange), so short blocks don't hide peaks
    leftLevel.store  (std::max (leftLevel.load(),  peakL));
    rightLevel.store (std::max (rightLevel.load(), peakR));
}

float DelayAudioProcessor::getLeftLevel()  { return leftLevel.exchange (0.0f); }
float DelayAudioProcessor::getRightLevel() { return rightLevel.exchange (0.0f); }

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
