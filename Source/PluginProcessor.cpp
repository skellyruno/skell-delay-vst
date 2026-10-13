#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Keeps the feedback loop bounded even at 100% feedback (transparent below 1.0)
    inline float loopLimit (float y)
    {
        const float a = std::abs (y);
        return a <= 1.0f ? y : std::copysign (1.0f + 0.5f * std::tanh (a - 1.0f), y);
    }

    // Final safety limiter on the plugin output (transparent below -0.4 dBFS)
    inline float safetyLimit (float x)
    {
        const float a = std::abs (x);
        return a <= 0.95f ? x : std::copysign (0.95f + 0.05f * std::tanh ((a - 0.95f) / 0.05f), x);
    }

    // First-order all-pass section (phaser stage)
    inline float allPass (float x, float a, float& z)
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
}

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
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addFloat = [&params] (const char* id, const char* name, Range range, float def)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (id, name, range, def));
    };

    // ---- Delay ----
    Range timeRange (0.01f, 2500.0f, 0.01f);       // Comeback Kid free mode: 0.01 ms - 2500 ms
    timeRange.setSkewForCentre (250.0f);
    addFloat ("DELAY_TIME", "Delay Time", timeRange, 250.0f);

    params.push_back (std::make_unique<juce::AudioParameterBool> ("SYNC", "Tempo Sync", false));

    juce::StringArray divNames;
    for (const auto& d : kSyncDivisions)
        divNames.add (d.name);
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("SYNC_DIV", "Sync Note", divNames, kDefaultSyncDivision));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "FEEL", "Feel", juce::StringArray { "Straight", "Dotted", "Triplet" }, 0));

    addFloat ("FEEDBACK", "Feedback", Range (0.0f, 1.0f, 0.01f), 0.5f);
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "FB_MODE", "Feedback Mode", juce::StringArray { "Normal", "Inverted", "Ping Pong" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("DESTINY", "Destiny", false));
    addFloat ("SMOOTH", "Smoothing", Range (0.0f, 100.0f, 1.0f), 50.0f);

    // ---- Shaper ----
    Range loRange (20.0f, 2000.0f, 1.0f);   loRange.setSkewForCentre (200.0f);
    Range hiRange (1000.0f, 20000.0f, 1.0f); hiRange.setSkewForCentre (6000.0f);
    addFloat ("LOCUT", "Lo-Cut", loRange, 20.0f);
    addFloat ("HICUT", "Hi-Cut", hiRange, 20000.0f);
    addFloat ("ATTACK",  "Attack",  Range (-1.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SUSTAIN", "Sustain", Range (-1.0f, 1.0f, 0.01f), 0.0f);

    // ---- Flavor ----
    params.push_back (std::make_unique<juce::AudioParameterBool> ("CHEAP", "Cheap", false));
    addFloat ("TAPE",  "Tape",  Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SWIRL", "Swirl", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SAUCE", "Sauce", Range (0.0f, 1.0f, 0.01f), 0.0f);

    // ---- Stereo ----
    addFloat ("WIDER",  "Wider",  Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("RICHER", "Richer", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("PAN",    "Pan",    Range (-1.0f, 1.0f, 0.01f), 0.0f);
    params.push_back (std::make_unique<juce::AudioParameterBool> ("MONO", "Mono", false));

    // ---- Output ----
    addFloat ("DUCKING", "Ducker", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("DRY", "Direct Level",  Range (0.0f, 1.0f, 0.01f), 1.0f);
    addFloat ("WET", "Delayed Level", Range (0.0f, 1.0f, 0.01f), 1.0f);

    return { params.begin(), params.end() };
}

const juce::String DelayAudioProcessor::getName() const { return JucePlugin_Name; }
bool DelayAudioProcessor::acceptsMidi() const { return false; }
bool DelayAudioProcessor::producesMidi() const { return false; }
bool DelayAudioProcessor::isMidiEffect() const { return false; }
double DelayAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int DelayAudioProcessor::getNumPrograms() { return 1; }
int DelayAudioProcessor::getCurrentProgram() { return 0; }
void DelayAudioProcessor::setCurrentProgram (int) {}
const juce::String DelayAudioProcessor::getProgramName (int) { return {}; }
void DelayAudioProcessor::changeProgramName (int, const juce::String&) {}

float DelayAudioProcessor::computeDelayMs() const
{
    float ms;
    if (param ("SYNC") > 0.5f)
    {
        const int idx  = juce::jlimit (0, kNumSyncDivisions - 1, static_cast<int> (param ("SYNC_DIV") + 0.5f));
        const int feel = juce::jlimit (0, 2, static_cast<int> (param ("FEEL") + 0.5f));
        ms = kSyncDivisions[idx].beats * kFeelMultiplier[feel] * 60000.0f
               / static_cast<float> (juce::jmax (20.0, hostBpm));
    }
    else
    {
        ms = param ("DELAY_TIME");
    }

    // keep the read head inside the buffer (and at least a few samples behind the write head)
    const float minMs = 3000.0f / static_cast<float> (sampleRate);
    const float maxMs = 1000.0f * static_cast<float> (delayBuffer.getNumSamples() - 16) / static_cast<float> (sampleRate);
    return juce::jlimit (minMs, juce::jmax (minMs, maxMs), ms);
}

void DelayAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;

    const int maxDelaySamples = static_cast<int> (sampleRate * 6.0);
    delayBuffer.setSize (2, maxDelaySamples);
    delayBuffer.clear();
    writePosition = 0;

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)), 1 };
    for (int c = 0; c < 2; ++c)
    {
        loCut[c].prepare (spec);
        loCut[c].setType (juce::dsp::StateVariableTPTFilterType::highpass);
        loCut[c].reset();
        hiCut[c].prepare (spec);
        hiCut[c].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        hiCut[c].reset();

        destinyTarget[c] = destinyValue[c] = 0.0f;
        tapeLp[c] = 0.0f;
        phaserFeedback[c] = 0.0f;
        for (auto& z : apState[c]) z = 0.0f;
    }

    phaseWider = phaseRicher1 = phaseRicher2 = phaseSwirl = 0.0;
    destinyCountdown = 0;
    envFast = envSlow = 0.0f;
    duckEnvelope = 0.0f;

    reverb.setSampleRate (sampleRate);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.62f;  rp.damping = 0.55f;  rp.wetLevel = 0.33f;  rp.dryLevel = 0.0f;
    rp.width = 1.0f;      rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    reverb.reset();

    const size_t scratch = static_cast<size_t> (juce::jmax (samplesPerBlock, 4096));
    wetL.assign (scratch, 0.0f);  wetR.assign (scratch, 0.0f);
    revL.assign (scratch, 0.0f);  revR.assign (scratch, 0.0f);
    duckBuf.assign (scratch, 1.0f);

    lastSmoothParam = param ("SMOOTH");
    smoothDelayTime.reset (sampleRate, juce::jmax (0.001, lastSmoothParam * 0.001));
    smoothDelayTime.setCurrentAndTargetValue ((computeDelayMs() / 1000.0f) * static_cast<float> (sampleRate));
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

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0 || delayBuffer.getNumSamples() == 0)
        return;

    // ---------------------------------------------------------------- parameters
    const float panVal      = param ("PAN");
    const float feedbackVal = param ("FEEDBACK");
    const float duckingVal  = param ("DUCKING");
    const float dryVal      = param ("DRY");
    const float wetVal      = param ("WET");
    const int   fbMode      = static_cast<int> (param ("FB_MODE"));
    const bool  destiny     = param ("DESTINY") > 0.5f;
    const float widerAmt    = param ("WIDER");
    const float richerAmt   = param ("RICHER");
    const float attackAmt   = param ("ATTACK");
    const float sustainAmt  = param ("SUSTAIN");
    const bool  cheap       = param ("CHEAP") > 0.5f;
    const float tapeAmt     = param ("TAPE");
    const float swirlAmt    = param ("SWIRL");
    const float sauceAmt    = param ("SAUCE");
    const bool  mono        = param ("MONO") > 0.5f;

    // SMOOTH (0-100) = glide time in ms. Only re-time the ramp when the knob moves,
    // preserving the current value so there is no jump/click.
    const float smoothParam = param ("SMOOTH");
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
    smoothDelayTime.setTargetValue ((delayMs / 1000.0f) * static_cast<float> (sampleRate));

    const float sr = static_cast<float> (sampleRate);
    const int delayBufLen = delayBuffer.getNumSamples();
    const float maxDelaySamples = static_cast<float> (delayBufLen - 16);

    if (static_cast<int> (wetL.size()) < numSamples)   // host gave a bigger block than announced
    {
        wetL.resize (static_cast<size_t> (numSamples));  wetR.resize (static_cast<size_t> (numSamples));
        revL.resize (static_cast<size_t> (numSamples));  revR.resize (static_cast<size_t> (numSamples));
        duckBuf.resize (static_cast<size_t> (numSamples));
    }

    // Filters in the feedback loop (Lo-Cut / Hi-Cut)
    const float loHz = juce::jlimit (20.0f, 2000.0f, param ("LOCUT"));
    const float hiHz = juce::jlimit (1000.0f, sr * 0.45f, param ("HICUT"));
    // Fully open positions bypass the filters, so the default sound is untouched
    const bool loCutOn = loHz > 20.5f;
    const bool hiCutOn = hiHz < 19999.0f;
    for (int c = 0; c < 2; ++c)
    {
        loCut[c].setCutoffFrequency (loHz);
        hiCut[c].setCutoffFrequency (hiHz);
        if (! loCutOn) loCut[c].reset();
        if (! hiCutOn) hiCut[c].reset();
    }

    // Time-modulation depths (samples)
    const float widerDepth  = widerAmt  * 0.006f * sr;    // up to +-6 ms, opposite on L/R
    const float richerDepth = richerAmt * 0.004f * sr;    // up to +-4 ms pitch wobble
    const double incWider   = juce::MathConstants<double>::twoPi * 0.23 / sampleRate;
    const double incRicher1 = juce::MathConstants<double>::twoPi * 0.70 / sampleRate;
    const double incRicher2 = juce::MathConstants<double>::twoPi * 1.31 / sampleRate;
    const double incSwirl   = juce::MathConstants<double>::twoPi * 0.35 / sampleRate;

    const int   destinyPeriod = static_cast<int> (0.11f * sr);
    const float destinySmooth = 1.0f - std::exp (-1.0f / (0.04f * sr));

    // Ducker: attenuates the wet signal while the dry input is playing
    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.004f * sr));
    const float duckRelease = 1.0f - std::exp (-1.0f / (0.250f * sr));

    // Transient shaper detectors
    const float fastA = 1.0f - std::exp (-1.0f / (0.0005f * sr));
    const float fastR = 1.0f - std::exp (-1.0f / (0.020f  * sr));
    const float slowA = 1.0f - std::exp (-1.0f / (0.020f  * sr));
    const float slowR = 1.0f - std::exp (-1.0f / (0.200f  * sr));
    const bool shaperOn = (attackAmt != 0.0f || sustainAmt != 0.0f);

    // Tape: saturation drive, HF roll-off and a little hiss
    const float tapeDrive = 1.0f + 5.0f * tapeAmt;
    const float tapeHz    = 18000.0f * std::pow (0.3f, tapeAmt);
    const float tapeCoef  = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (tapeHz, sr * 0.45f) / sr);
    const float hissGain  = 0.002f * tapeAmt * tapeAmt;

    // Wet pan (balance law, centre = unity)
    const float panGainL = panVal > 0.0f ? 1.0f - panVal : 1.0f;
    const float panGainR = panVal < 0.0f ? 1.0f + panVal : 1.0f;

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

    auto readDelayed = [&] (const float* buf, float delaySamples)
    {
        float pos = static_cast<float> (writePosition) - delaySamples;
        while (pos < 0.0f)
            pos += static_cast<float> (delayBufLen);
        int i1 = static_cast<int> (pos);
        const float frac = pos - static_cast<float> (i1);
        if (i1 >= delayBufLen) i1 -= delayBufLen;
        return readCubic (buf, i1, frac);
    };

    float peakL = 0.0f, peakR = 0.0f;

    // ================================================================ pass 1: delay engine + wet flavour chain
    for (int i = 0; i < numSamples; ++i)
    {
        // ---- modulated read positions
        const float base = smoothDelayTime.getNextValue();

        const float lfoW  = static_cast<float> (std::sin (phaseWider));
        const float lfoR  = (static_cast<float> (std::sin (phaseRicher1)) + 0.5f * static_cast<float> (std::sin (phaseRicher2))) * (1.0f / 1.5f);
        phaseWider   += incWider;   if (phaseWider   >= juce::MathConstants<double>::twoPi) phaseWider   -= juce::MathConstants<double>::twoPi;
        phaseRicher1 += incRicher1; if (phaseRicher1 >= juce::MathConstants<double>::twoPi) phaseRicher1 -= juce::MathConstants<double>::twoPi;
        phaseRicher2 += incRicher2; if (phaseRicher2 >= juce::MathConstants<double>::twoPi) phaseRicher2 -= juce::MathConstants<double>::twoPi;

        const float common = richerDepth * lfoR;
        const float wide   = widerDepth * lfoW;
        float dlyL = base + common + wide;
        float dlyR = base + common - wide;

        if (destiny)
        {
            if (--destinyCountdown <= 0)
            {
                destinyCountdown = destinyPeriod;
                destinyTarget[0] = rng.nextFloat() * 2.0f - 1.0f;
                destinyTarget[1] = rng.nextFloat() * 2.0f - 1.0f;
            }
            destinyValue[0] += destinySmooth * (destinyTarget[0] - destinyValue[0]);
            destinyValue[1] += destinySmooth * (destinyTarget[1] - destinyValue[1]);
            const float depth = 0.0006f * sr + 0.0008f * base;
            dlyL += depth * destinyValue[0];
            dlyR += depth * destinyValue[1];
        }

        dlyL = juce::jlimit (3.0f, maxDelaySamples, dlyL);
        dlyR = juce::jlimit (3.0f, maxDelaySamples, dlyR);

        // ---- taps, then Lo-Cut -> Hi-Cut (inside the loop, so every repeat is filtered again)
        float tapA = readDelayed (delayLeftPtr,  dlyL);
        float tapB = readDelayed (delayRightPtr, dlyR);
        if (loCutOn) { tapA = loCut[0].processSample (0, tapA);  tapB = loCut[1].processSample (0, tapB); }
        if (hiCutOn) { tapA = hiCut[0].processSample (0, tapA);  tapB = hiCut[1].processSample (0, tapB); }
        tapA = loopLimit (tapA);
        tapB = loopLimit (tapB);

        const float inLeft  = leftIn[i];
        const float inRight = rightIn[i];

        // ---- ducker envelope follows the dry input
        const float inLevel = std::max (std::abs (inLeft), std::abs (inRight));
        duckEnvelope += (inLevel - duckEnvelope) * (inLevel > duckEnvelope ? duckAttack : duckRelease);
        // engaged from about -45 dB, fully by about -18 dB; feedback keeps running so trails return in the gaps
        const float duckAmount = juce::jlimit (0.0f, 1.0f, (duckEnvelope - 0.0056f) / (0.126f - 0.0056f));
        duckBuf[static_cast<size_t> (i)] = 1.0f - duckingVal * duckAmount;

        // ---- feed the lines (Normal / Inverted / Ping Pong)
        float injA, injB;
        if (fbMode == 2)
        {
            // Ping Pong sums to mono and starts on the left so the bounce is audible on any source
            const float mid = (inLeft + inRight) * 0.5f;
            injA = 0.0f;
            injB = mid;
        }
        else
        {
            injA = inLeft;
            injB = inRight;
        }

        float wl, wr;
        if (fbMode == 0)        // Normal: echoes stay on the side of the input
        {
            delayLeftPtr[writePosition]  = injA + tapA * feedbackVal;
            delayRightPtr[writePosition] = injB + tapB * feedbackVal;
            wl = tapA;  wr = tapB;
        }
        else if (fbMode == 1)   // Inverted: left/right swapped in the output
        {
            delayLeftPtr[writePosition]  = injA + tapA * feedbackVal;
            delayRightPtr[writePosition] = injB + tapB * feedbackVal;
            wl = tapB;  wr = tapA;
        }
        else                    // Ping Pong: inverted + cross-feedback, sides swap on every echo
        {
            delayLeftPtr[writePosition]  = injA + tapB * feedbackVal;
            delayRightPtr[writePosition] = injB + tapA * feedbackVal;
            wl = tapB;  wr = tapA;
        }

        writePosition = (writePosition + 1) % delayBufLen;

        // ---- wet flavour chain: Attack/Sustain -> Cheap -> Tape -> Swirl
        if (shaperOn)
        {
            const float lvl = std::max (std::abs (wl), std::abs (wr));
            envFast += (lvl - envFast) * (lvl > envFast ? fastA : fastR);
            envSlow += (lvl - envSlow) * (lvl > envSlow ? slowA : slowR);

            const float diffDb = 20.0f * std::log10 ((envFast + 1.0e-5f) / (envSlow + 1.0e-5f));
            const float onset  = juce::jlimit (0.0f, 1.0f,  diffDb / 6.0f);   // fast > slow: attack portion
            const float tail   = juce::jlimit (0.0f, 1.0f, -diffDb / 6.0f);   // fast < slow: decaying tail
            const float g = juce::Decibels::decibelsToGain (12.0f * (attackAmt * onset + sustainAmt * tail));
            wl *= g;  wr *= g;
        }

        if (cheap)   // 11-bit signal path, like vintage digital delays
        {
            wl = std::round (wl * 1024.0f) / 1024.0f;
            wr = std::round (wr * 1024.0f) / 1024.0f;
        }

        if (tapeAmt > 0.0f)
        {
            wl = std::tanh (wl * tapeDrive) / tapeDrive;
            wr = std::tanh (wr * tapeDrive) / tapeDrive;
            tapeLp[0] += tapeCoef * (wl - tapeLp[0]);   wl = tapeLp[0];
            tapeLp[1] += tapeCoef * (wr - tapeLp[1]);   wr = tapeLp[1];
            wl += (rng.nextFloat() * 2.0f - 1.0f) * hissGain;
            wr += (rng.nextFloat() * 2.0f - 1.0f) * hissGain;
        }

        if (swirlAmt > 0.0f)   // 4-stage phaser, L/R LFOs a quarter-cycle apart
        {
            float* wetPair[2] = { &wl, &wr };
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = static_cast<float> (std::sin (phaseSwirl + (c == 0 ? 0.0 : juce::MathConstants<double>::halfPi)));
                const float fc = 250.0f * std::pow (10.0f, 1.1f * (0.5f + 0.5f * lfo));        // 250 Hz .. ~3.1 kHz
                const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, sr * 0.45f) / sr);
                const float a = (t - 1.0f) / (t + 1.0f);

                const float x = *wetPair[c];
                float y = x + 0.4f * phaserFeedback[c];
                for (int s = 0; s < 4; ++s)
                    y = allPass (y, a, apState[c][s]);
                phaserFeedback[c] = y;

                *wetPair[c] = x * (1.0f - 0.5f * swirlAmt) + y * (0.5f * swirlAmt);
            }
            phaseSwirl += incSwirl;
            if (phaseSwirl >= juce::MathConstants<double>::twoPi) phaseSwirl -= juce::MathConstants<double>::twoPi;
        }

        wetL[static_cast<size_t> (i)] = wl;
        wetR[static_cast<size_t> (i)] = wr;
    }

    // ================================================================ Sauce: algorithmic reverb on the wet signal
    if (sauceAmt > 0.0f)
    {
        std::copy (wetL.begin(), wetL.begin() + numSamples, revL.begin());
        std::copy (wetR.begin(), wetR.begin() + numSamples, revR.begin());
        reverb.processStereo (revL.data(), revR.data(), numSamples);
    }

    // ================================================================ pass 2: Mono -> Pan -> Ducker -> levels -> safety limiter
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t k = static_cast<size_t> (i);
        float wl = wetL[k];
        float wr = wetR[k];

        if (sauceAmt > 0.0f)
        {
            wl += sauceAmt * revL[k];
            wr += sauceAmt * revR[k];
        }

        if (mono)
            wl = wr = 0.5f * (wl + wr);

        const float wetGain = duckBuf[k] * wetVal;
        const float outLeft  = safetyLimit (leftIn[i]  * dryVal + wl * wetGain * panGainL);
        const float outRight = safetyLimit (rightIn[i] * dryVal + wr * wetGain * panGainR);

        if (numChannels > 1)
        {
            leftOut[i]  = outLeft;
            rightOut[i] = outRight;
        }
        else
        {
            leftOut[i] = 0.5f * (outLeft + outRight);   // mono bus: fold the stereo echoes
        }

        peakL = std::max (peakL, std::abs (outLeft));
        peakR = std::max (peakR, std::abs (outRight));
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
