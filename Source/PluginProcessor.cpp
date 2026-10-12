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

    // Free (ms) / tempo-synced time
    params.push_back (std::make_unique<juce::AudioParameterBool>("SYNC", "Tempo Sync", false));

    juce::StringArray divNames;
    for (const auto& d : kSyncDivisions)
        divNames.add (d.name);
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "SYNC_DIV", "Sync Note", divNames, kDefaultSyncDivision));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DUCKING", "Ducking", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "DRY", "Dry Level", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "WET", "Wet Level", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Same three feedback routings as Fruity Delay 2
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "FB_MODE", "Feedback Mode", juce::StringArray { "Normal", "Inverted", "Ping Pong" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "MODE", "Mode", juce::StringArray { "Digital", "Analog", "Tape" }, 0));

    return { params.begin(), params.end() };
}

const juce::String DelayAudioProcessor::getName() const { return JucePlugin_Name; }
bool DelayAudioProcessor::acceptsMidi() const { return false; }
bool DelayAudioProcessor::producesMidi() const { return false; }
bool DelayAudioProcessor::isMidiEffect() const { return false; }
double DelayAudioProcessor::getTailLengthSeconds() const { return 6.0; }

int DelayAudioProcessor::getNumPrograms() { return 1; }
int DelayAudioProcessor::getCurrentProgram() { return 0; }
void DelayAudioProcessor::setCurrentProgram (int) {}
const juce::String DelayAudioProcessor::getProgramName (int) { return {}; }
void DelayAudioProcessor::changeProgramName (int, const juce::String&) {}

float DelayAudioProcessor::computeDelayMs() const
{
    float ms;
    if (apvts.getRawParameterValue ("SYNC")->load() > 0.5f)
    {
        const int idx = juce::jlimit (0, kNumSyncDivisions - 1,
                                      static_cast<int>(apvts.getRawParameterValue ("SYNC_DIV")->load() + 0.5f));
        ms = kSyncDivisions[idx].beats * 60000.0f / static_cast<float>(juce::jmax (20.0, hostBpm));
    }
    else
    {
        ms = apvts.getRawParameterValue ("DELAY_TIME")->load();
    }

    // never read past the end of the delay line
    const float maxMs = 1000.0f * static_cast<float>(delayBuffer.getNumSamples() - 8) / static_cast<float>(sampleRate);
    return juce::jlimit (1.0f, juce::jmax (1.0f, maxMs), ms);
}

void DelayAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = sr;

    const int maxDelaySamples = static_cast<int>(sampleRate * 6.0);
    delayBuffer.setSize (2, maxDelaySamples);
    delayBuffer.clear();

    writePosition = 0;
    duckEnvelope = 0.0f;
    lpState[0] = lpState[1] = hpState[0] = hpState[1] = lp2State[0] = lp2State[1] = 0.0f;
    lfoPhaseSlow = lfoPhaseFast = 0.0;
    wowDepth = flutterDepth = 0.0f;

    // Initialise smoother from the current parameter values (not hardcoded defaults)
    lastSmoothParam = apvts.getRawParameterValue ("SMOOTH")->load();
    smoothDelayTime.reset (sampleRate, juce::jmax (0.001, lastSmoothParam * 0.001));
    smoothDelayTime.setCurrentAndTargetValue ((computeDelayMs() / 1000.0f) * static_cast<float>(sampleRate));
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
    const float feedbackVal  = apvts.getRawParameterValue ("FEEDBACK")->load();
    const float duckingVal   = apvts.getRawParameterValue ("DUCKING")->load();
    const float dryVal       = apvts.getRawParameterValue ("DRY")->load();
    const float wetVal       = apvts.getRawParameterValue ("WET")->load();
    const int   fbMode       = static_cast<int>(apvts.getRawParameterValue ("FB_MODE")->load());
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

    // Host tempo for sync mode (falls back to the last known / 120 bpm)
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    const float delayMs = computeDelayMs();
    effectiveDelayMs.store (delayMs);
    smoothDelayTime.setTargetValue ((delayMs / 1000.0f) * static_cast<float>(sampleRate));

    const int delayBufLen = delayBuffer.getNumSamples();
    float peakL = 0.0f, peakR = 0.0f;

    // PAN is an INPUT pan (as in Fruity Delay 2): it positions the signal feeding the delay,
    // the dry signal is untouched. Balance law: centre = unity on both sides.
    const float inGainL = panVal > 0.0f ? 1.0f - panVal : 1.0f;
    const float inGainR = panVal < 0.0f ? 1.0f + panVal : 1.0f;
    const bool  pingStartsLeft = panVal <= 0.0f;

    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.004f * static_cast<float>(sampleRate)));
    const float duckRelease = 1.0f - std::exp (-1.0f / (0.250f * static_cast<float>(sampleRate)));

    // Character of the repeats (every pass through the loop gets this treatment):
    //   Digital: clean, only a gentle top-end roll-off.
    //   Analog : bucket-brigade echo - dark 2-pole low-pass, soft drive, slow clock-drift
    //            chorus on the repeats.
    //   Tape   : tape echo - softer roll-off, heavier saturation, wow & flutter.
    float lpHz = 12000.0f, hpHz = 20.0f, drive = 0.0f;
    float wowTargetMs = 0.0f, flutterTargetMs = 0.0f, wowHz = 0.55f;
    bool twoPole = false;
    if (modeVal == 1)      { lpHz = 3800.0f; hpHz = 80.0f;  drive = 1.3f;  twoPole = true;
                             wowTargetMs = 0.35f; flutterTargetMs = 0.0f;  wowHz = 0.9f; }
    else if (modeVal == 2) { lpHz = 5200.0f; hpHz = 110.0f; drive = 1.6f;
                             wowTargetMs = 0.55f; flutterTargetMs = 0.08f; wowHz = 0.55f; }

    const float sr = static_cast<float>(sampleRate);
    lpHz = juce::jmin (lpHz, sr * 0.45f);
    const float lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * lpHz / sr);
    const float hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hpHz / sr);

    const double slowInc = juce::MathConstants<double>::twoPi * wowHz / sampleRate;   // wow / clock drift
    const double fastInc = juce::MathConstants<double>::twoPi * 6.3  / sampleRate;   // flutter
    const float wowDepthTarget     = 0.001f * wowTargetMs * sr;
    const float flutterDepthTarget = 0.001f * flutterTargetMs * sr;

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
        if (twoPole)
        {
            lp2State[ch] += lpCoef * (y - lp2State[ch]);
            y = lp2State[ch];
        }
        hpState[ch] += hpCoef * (y - hpState[ch]);
        y -= hpState[ch];
        if (drive > 0.0f)
            y = std::tanh (y * drive) / drive;
        else if (std::abs (y) > 1.0f)   // Digital: only tames runaway at 100% feedback
            y = std::copysign (1.0f + 0.5f * std::tanh (std::abs (y) - 1.0f), y);
        return y;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // glide the modulation depths so switching modes never clicks
        wowDepth     += 0.0005f * (wowDepthTarget     - wowDepth);
        flutterDepth += 0.0005f * (flutterDepthTarget - flutterDepth);
        const float mod = wowDepth * static_cast<float>(std::sin (lfoPhaseSlow))
                        + flutterDepth * static_cast<float>(std::sin (lfoPhaseFast));
        lfoPhaseSlow += slowInc;  if (lfoPhaseSlow >= juce::MathConstants<double>::twoPi) lfoPhaseSlow -= juce::MathConstants<double>::twoPi;
        lfoPhaseFast += fastInc;  if (lfoPhaseFast >= juce::MathConstants<double>::twoPi) lfoPhaseFast -= juce::MathConstants<double>::twoPi;

        const float currentDelaySamples = smoothDelayTime.getNextValue() + mod;

        float readPosition = static_cast<float>(writePosition) - currentDelaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(delayBufLen);

        const int   readIdx = static_cast<int>(readPosition) % delayBufLen;
        const float frac    = readPosition - std::floor (readPosition);

        // Two delay lines: A (left input) and B (right input), tapped, filtered, saturated
        const float tapA = shapeLoop (readCubic (delayLeftPtr,  readIdx, frac), 0);
        const float tapB = shapeLoop (readCubic (delayRightPtr, readIdx, frac), 1);

        const float inLeft  = leftIn[sample];
        const float inRight = rightIn[sample];

        const float inLevel = std::max (std::abs (inLeft), std::abs (inRight));
        if (inLevel > duckEnvelope)
            duckEnvelope += (inLevel - duckEnvelope) * duckAttack;
        else
            duckEnvelope += (inLevel - duckEnvelope) * duckRelease;

        // Ducker: while the input is present (above ~-45 dB, fully engaged by ~-18 dB) the wet
        // signal is pulled down by DUCKING; at 100% the echoes vanish under the dry signal and
        // swell back (~250 ms) once it stops. Feedback keeps running, so trails return in the gaps.
        const float duckAmount = juce::jlimit (0.0f, 1.0f, (duckEnvelope - 0.0056f) / (0.126f - 0.0056f));
        const float duckGain = 1.0f - duckingVal * duckAmount;

        // Feed the lines. Ping Pong sums to mono and starts on one side (set by PAN) so the
        // bounce is always audible; the other modes use the real stereo input through PAN.
        float injA, injB;
        if (fbMode == 2)
        {
            const float mono = (inLeft + inRight) * 0.5f;
            // first echo lands on the PAN side (left at centre); output is swapped below
            injA = pingStartsLeft ? 0.0f : mono;
            injB = pingStartsLeft ? mono : 0.0f;
        }
        else
        {
            injA = inLeft  * inGainL;
            injB = inRight * inGainR;
        }

        float wetL, wetR;
        if (fbMode == 0)        // Normal: echoes stay on the side of the input
        {
            delayLeftPtr[writePosition]  = injA + tapA * feedbackVal;
            delayRightPtr[writePosition] = injB + tapB * feedbackVal;
            wetL = tapA;  wetR = tapB;
        }
        else if (fbMode == 1)   // Inverted: output channels swapped (input right -> echoes left)
        {
            delayLeftPtr[writePosition]  = injA + tapA * feedbackVal;
            delayRightPtr[writePosition] = injB + tapB * feedbackVal;
            wetL = tapB;  wetR = tapA;
        }
        else                    // Ping Pong: inverted + cross-feedback, so sides swap on every echo
        {
            delayLeftPtr[writePosition]  = injA + tapB * feedbackVal;
            delayRightPtr[writePosition] = injB + tapA * feedbackVal;
            wetL = tapB;  wetR = tapA;
        }

        const float outLeft  = (inLeft  * dryVal) + wetL * duckGain * wetVal;
        const float outRight = (inRight * dryVal) + wetR * duckGain * wetVal;

        if (numChannels > 1)
        {
            leftOut[sample]  = outLeft;
            rightOut[sample] = outRight;
        }
        else
        {
            leftOut[sample] = 0.5f * (outLeft + outRight);   // mono bus: fold the stereo echoes
        }

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
