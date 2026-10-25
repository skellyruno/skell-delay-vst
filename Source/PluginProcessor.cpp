#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    // Keeps the feedback loop bounded even at 100% feedback (transparent below 1.0)
    inline float loopLimit (float y)
    {
        const float a = std::abs (y);
        return a <= 1.0f ? y : std::copysign (1.0f + 0.5f * std::tanh (a - 1.0f), y);
    }

    // Safety limiter for the WET signal only (transparent below -0.4 dBFS). The dry signal never goes through it.
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

    // 4-point cubic Hermite interpolation
    inline float hermite (float y0, float y1, float y2, float y3, float f)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    constexpr float kParamRampSeconds = 0.02f;
}

DelayAudioProcessor::DelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)   // optional: the ducker listens to this
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
    using PID = juce::ParameterID;       // version hint 1: required for stable AU / VST3 parameter ids
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addFloat = [&params] (const char* id, const char* name, Range range, float def)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (PID { id, 1 }, name, range, def));
    };
    auto addBool = [&params] (const char* id, const char* name, bool def)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (PID { id, 1 }, name, def));
    };
    auto addChoice = [&params] (const char* id, const char* name, juce::StringArray items, int def)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (PID { id, 1 }, name, items, def));
    };

    // ---- Delay ----
    Range timeRange (0.01f, 2500.0f, 0.01f);       // Comeback Kid free mode: 0.01 ms - 2500 ms
    timeRange.setSkewForCentre (250.0f);
    addFloat ("DELAY_TIME", "Delay Time", timeRange, 250.0f);

    addBool ("SYNC", "Tempo Sync", false);

    juce::StringArray divNames;
    for (const auto& d : kSyncDivisions)
        divNames.add (d.name);
    addChoice ("SYNC_DIV", "Sync Note", divNames, kDefaultSyncDivision);
    addChoice ("FEEL", "Feel", { "Straight", "Dotted", "Triplet" }, 0);

    addFloat ("FEEDBACK", "Feedback", Range (0.0f, 1.0f, 0.01f), 0.5f);
    addChoice ("FB_MODE", "Feedback Mode", { "Normal", "Inverted", "Ping Pong" }, 0);
    addBool ("DESTINY", "Destiny", false);
    addBool ("REVERSE", "Reverse", false);
    addFloat ("SHIMMER", "Shimmer", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addChoice ("SHIM_PITCH", "Shimmer Pitch", { "+12", "+7", "-12", "-7" }, 0);
    addFloat ("SMOOTH", "Smoothing", Range (0.0f, 100.0f, 1.0f), 50.0f);

    // ---- Shaper ----
    Range loRange (20.0f, 2000.0f, 1.0f);    loRange.setSkewForCentre (200.0f);
    Range hiRange (1000.0f, 20000.0f, 1.0f); hiRange.setSkewForCentre (6000.0f);
    addFloat ("LOCUT", "Lo-Cut", loRange, 20.0f);
    addFloat ("HICUT", "Hi-Cut", hiRange, 20000.0f);
    addFloat ("ATTACK",  "Attack",  Range (-1.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SUSTAIN", "Sustain", Range (-1.0f, 1.0f, 0.01f), 0.0f);

    // ---- Flavor ----
    addBool ("CHEAP", "Cheap", false);
    addFloat ("TAPE",  "Tape",  Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SWIRL", "Swirl", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("SAUCE", "Sauce", Range (0.0f, 1.0f, 0.01f), 0.0f);

    // ---- Stereo ----
    Range modRate (0.1f, 8.0f, 0.01f);  modRate.setSkewForCentre (1.0f);
    addFloat ("MOD_RATE", "Mod Rate", modRate, 1.0f);                       // speed multiplier for Wider / Richer
    addChoice ("MOD_SHAPE", "Mod Shape", { "Sine", "Triangle", "Random" }, 0);
    addFloat ("WIDER",  "Wider",  Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("RICHER", "Richer", Range (0.0f, 1.0f, 0.01f), 0.0f);
    addFloat ("PAN",    "Pan",    Range (-1.0f, 1.0f, 0.01f), 0.0f);
    addBool ("MONO", "Mono", false);

    // ---- Output ----
    addFloat ("DUCKING", "Ducker", Range (0.0f, 1.0f, 0.01f), 0.0f);
    Range relRange (20.0f, 1000.0f, 1.0f);  relRange.setSkewForCentre (250.0f);
    addFloat ("DUCK_REL", "Ducker Release", relRange, 250.0f);
    addFloat ("DRY", "Direct Level",  Range (0.0f, 1.0f, 0.01f), 1.0f);
    addFloat ("WET", "Delayed Level", Range (0.0f, 1.0f, 0.01f), 1.0f);

    return { params.begin(), params.end() };
}

const juce::String DelayAudioProcessor::getName() const { return JucePlugin_Name; }
bool DelayAudioProcessor::acceptsMidi() const { return false; }
bool DelayAudioProcessor::producesMidi() const { return false; }
bool DelayAudioProcessor::isMidiEffect() const { return false; }
double DelayAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int DelayAudioProcessor::getNumPrograms() { return static_cast<int> (getFactoryPresets().size()); }

int DelayAudioProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, static_cast<int> (apvts.state.getProperty ("preset", 0)));
}

void DelayAudioProcessor::setCurrentProgram (int index) { applyPreset (index); }

const juce::String DelayAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, static_cast<int> (presets.size())) ? presets[static_cast<size_t> (index)].name : "";
}

void DelayAudioProcessor::changeProgramName (int, const juce::String&) {}

void DelayAudioProcessor::applyPreset (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return;

    // start from the defaults so a preset never inherits leftovers from the previous one
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            r->setValueNotifyingHost (r->getDefaultValue());

    for (const auto& v : presets[static_cast<size_t> (index)].values)
        if (auto* r = apvts.getParameter (v.id))
            r->setValueNotifyingHost (r->convertTo0to1 (v.value));

    apvts.state.setProperty ("preset", index, nullptr);
}

void DelayAudioProcessor::selectABSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive)
        return;

    abState[abActive] = apvts.copyState();
    abActive = slot;
    if (abState[slot].isValid())
        apvts.replaceState (abState[slot].createCopy());
}

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

void DelayAudioProcessor::snapSmoothers()
{
    const float sr = static_cast<float> (sampleRate);
    const int fbMode = static_cast<int> (param ("FB_MODE") + 0.5f);

    smDry.setCurrentAndTargetValue (param ("DRY"));
    smWet.setCurrentAndTargetValue (param ("WET"));
    smFeedback.setCurrentAndTargetValue (param ("FEEDBACK"));
    smDucking.setCurrentAndTargetValue (param ("DUCKING"));
    smPan.setCurrentAndTargetValue (param ("PAN"));
    smTape.setCurrentAndTargetValue (param ("TAPE"));
    smSwirl.setCurrentAndTargetValue (param ("SWIRL"));
    smSauce.setCurrentAndTargetValue (param ("SAUCE"));
    smWider.setCurrentAndTargetValue (param ("WIDER"));
    smRicher.setCurrentAndTargetValue (param ("RICHER"));
    smAttack.setCurrentAndTargetValue (param ("ATTACK"));
    smSustain.setCurrentAndTargetValue (param ("SUSTAIN"));
    smPing.setCurrentAndTargetValue (fbMode == 2 ? 1.0f : 0.0f);
    smSwap.setCurrentAndTargetValue (fbMode >= 1 ? 1.0f : 0.0f);
    smLoHz.setCurrentAndTargetValue (juce::jlimit (20.0f, 2000.0f, param ("LOCUT")));
    smHiHz.setCurrentAndTargetValue (juce::jlimit (1000.0f, sr * 0.45f, param ("HICUT")));
    smRev.setCurrentAndTargetValue (param ("REVERSE") > 0.5f ? 1.0f : 0.0f);
    smShim.setCurrentAndTargetValue (param ("SHIMMER"));
}

void DelayAudioProcessor::clearState()
{
    delayBuffer.clear();
    writePosition = 0;

    for (int c = 0; c < 2; ++c)
    {
        loCut[c].reset();
        hiCut[c].reset();
        dcX[c] = dcY[c] = 0.0f;
        destinyTarget[c] = destinyValue[c] = 0.0f;
        tapeLp[c] = 0.0f;
        phaserFeedback[c] = 0.0f;
        for (auto& z : apState[c]) z = 0.0f;
    }

    for (int c = 0; c < 2; ++c)
    {
        revPhase[c] = c == 0 ? 0.0 : 0.5;
        revLen[c] = 0.0f;
        phaseWow[c] = c == 0 ? 0.0 : 0.9;
        phaseFlut[c] = c == 0 ? 0.0 : 1.7;
        driftValue[c] = 0.0f;
        loopLp[c] = 0.0f;
        std::fill (shimRing[c].begin(), shimRing[c].end(), 0.0f);
    }
    shimWrite = 0;
    shimPhase = 0.0f;

    phaseWider = phaseRicher1 = phaseRicher2 = phaseSwirl = 0.0;
    for (int k = 0; k < 3; ++k) rndHeld[k] = rndSmooth[k] = 0.0f;
    destinyCountdown = 0;
    envFast = envSlow = 0.0f;
    duckEnvelope = 0.0f;
    fadeLeft = 0;
    lastTargetSamples = -1.0f;
    reverb.reset();
}

void DelayAudioProcessor::reset()
{
    if (delayBuffer.getNumSamples() == 0)
        return;

    clearState();
    snapSmoothers();
    smoothDelayTime.setCurrentAndTargetValue ((computeDelayMs() / 1000.0f) * static_cast<float> (sampleRate));
}

void DelayAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;

    const int maxDelaySamples = static_cast<int> (sampleRate * 6.0);
    delayBuffer.setSize (2, maxDelaySamples);

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)), 1 };
    for (int c = 0; c < 2; ++c)
    {
        loCut[c].prepare (spec);
        loCut[c].setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hiCut[c].prepare (spec);
        hiCut[c].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    reverb.setSampleRate (sampleRate);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.62f;  rp.damping = 0.55f;  rp.wetLevel = 0.33f;  rp.dryLevel = 0.0f;
    rp.width = 1.0f;      rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    wetL.assign (static_cast<size_t> (kMaxChunk), 0.0f);
    wetR.assign (static_cast<size_t> (kMaxChunk), 0.0f);
    revL.assign (static_cast<size_t> (kMaxChunk), 0.0f);
    revR.assign (static_cast<size_t> (kMaxChunk), 0.0f);
    duckBuf.assign (static_cast<size_t> (kMaxChunk), 1.0f);

    for (auto& r : shimRing)
        r.assign (static_cast<size_t> (kShimRing), 0.0f);
    shimWin = static_cast<float> (juce::jmin (0.035 * sampleRate, 12000.0));

    for (auto* s : { &smDry, &smWet, &smFeedback, &smDucking, &smPan, &smTape, &smSwirl, &smSauce,
                     &smWider, &smRicher, &smAttack, &smSustain, &smPing, &smSwap, &smLoHz, &smHiHz, &smRev, &smShim })
        s->reset (sampleRate, kParamRampSeconds);

    fadeTotal = juce::jmax (1, static_cast<int> (0.04 * sampleRate));   // 40 ms cross-fade for time jumps

    clearState();
    snapSmoothers();

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

    if (layouts.inputBuses.size() > 1)   // sidechain: off, mono or stereo
    {
        const auto sc = layouts.inputBuses.getReference (1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

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

    const int total = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), juce::jmax (1, getMainBusNumOutputChannels()));

    if (numChannels == 0 || total == 0 || delayBuffer.getNumSamples() == 0)
        return;

    // optional sidechain (second input bus): the ducker follows it instead of the main input
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (auto* scBus = getBus (true, 1))
        if (scBus->isEnabled() && scBus->getNumberOfChannels() > 0)
        {
            auto sc = getBusBuffer (buffer, true, 1);
            scL = sc.getReadPointer (0);
            scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
        }

    // Chunking keeps all scratch memory pre-allocated, whatever block size the host uses
    for (int pos = 0; pos < total; pos += kMaxChunk)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), numChannels, pos, juce::jmin (kMaxChunk, total - pos));
        processChunk (chunk, scL != nullptr ? scL + pos : nullptr, scR != nullptr ? scR + pos : nullptr);
    }
}

void DelayAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, const float* scL, const float* scR)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---------------------------------------------------------------- parameters
    const int  fbMode  = static_cast<int> (param ("FB_MODE") + 0.5f);
    const bool destiny = param ("DESTINY") > 0.5f;
    const bool cheap   = param ("CHEAP") > 0.5f;
    const bool mono    = param ("MONO") > 0.5f;

    const float sr = static_cast<float> (sampleRate);

    smDry.setTargetValue      (param ("DRY"));
    smWet.setTargetValue      (param ("WET"));
    smFeedback.setTargetValue (param ("FEEDBACK"));
    smDucking.setTargetValue  (param ("DUCKING"));
    smPan.setTargetValue      (param ("PAN"));
    smTape.setTargetValue     (param ("TAPE"));
    smSwirl.setTargetValue    (param ("SWIRL"));
    smSauce.setTargetValue    (param ("SAUCE"));
    smWider.setTargetValue    (param ("WIDER"));
    smRicher.setTargetValue   (param ("RICHER"));
    smAttack.setTargetValue   (param ("ATTACK"));
    smSustain.setTargetValue  (param ("SUSTAIN"));
    smPing.setTargetValue     (fbMode == 2 ? 1.0f : 0.0f);
    smSwap.setTargetValue     (fbMode >= 1 ? 1.0f : 0.0f);
    smLoHz.setTargetValue     (juce::jlimit (20.0f, 2000.0f, param ("LOCUT")));
    smHiHz.setTargetValue     (juce::jlimit (1000.0f, sr * 0.45f, param ("HICUT")));
    smRev.setTargetValue      (param ("REVERSE") > 0.5f ? 1.0f : 0.0f);
    smShim.setTargetValue     (param ("SHIMMER"));
    const float shimRatio = kShimRatio[juce::jlimit (0, kNumShimIntervals - 1, static_cast<int> (param ("SHIM_PITCH") + 0.5f))];

    // SMOOTH (0-100) = glide time in ms for small time changes. Only re-time the ramp when the
    // knob moves, preserving the current value so there is no jump/click.
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

    // Small change -> glide. Big jump -> cross-fade from the old position to the new one.
    const float targetSamples = (delayMs / 1000.0f) * sr;
    if (lastTargetSamples < 0.0f)
        lastTargetSamples = targetSamples;

    const float jumpLimit = juce::jmax (0.02f * sr, 0.2f * lastTargetSamples);
    if (std::abs (targetSamples - lastTargetSamples) > jumpLimit)
    {
        fadeOldDelay = smoothDelayTime.getCurrentValue();
        fadeLeft = fadeTotal;
        smoothDelayTime.setCurrentAndTargetValue (targetSamples);
    }
    else
    {
        smoothDelayTime.setTargetValue (targetSamples);
    }
    lastTargetSamples = targetSamples;

    const int delayBufLen = delayBuffer.getNumSamples();
    const float maxDelaySamples = static_cast<float> (delayBufLen - 16);

    // Modulation / detector constants
    const double modRateMul = juce::jlimit (0.1f, 8.0f, param ("MOD_RATE"));
    const int    modShape   = static_cast<int> (param ("MOD_SHAPE") + 0.5f);   // 0 sine, 1 triangle, 2 random
    const double incWider   = juce::MathConstants<double>::twoPi * 0.23 * modRateMul / sampleRate;
    const double incRicher1 = juce::MathConstants<double>::twoPi * 0.70 * modRateMul / sampleRate;
    const double incRicher2 = juce::MathConstants<double>::twoPi * 1.31 * modRateMul / sampleRate;

    // one LFO value for slot k at the given phase, in the chosen shape
    auto lfoShape = [this, modShape] (int k, double phase, double inc)
    {
        switch (modShape)
        {
            case 1:
            {
                const float t = static_cast<float> (phase / juce::MathConstants<double>::twoPi);
                return t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
            }
            case 2:
                rndSmooth[k] += juce::jmin (0.5f, 3.0f * static_cast<float> (inc)) * (rndHeld[k] - rndSmooth[k]);
                return rndSmooth[k];
            default:
                return static_cast<float> (std::sin (phase));
        }
    };
    auto advanceLfo = [this] (int k, double& phase, double inc)
    {
        phase += inc;
        if (phase >= juce::MathConstants<double>::twoPi)
        {
            phase -= juce::MathConstants<double>::twoPi;
            rndHeld[k] = rng.nextFloat() * 2.0f - 1.0f;   // new random target once per cycle
        }
    };
    const double incSwirl   = juce::MathConstants<double>::twoPi * 0.35 / sampleRate;

    const int   destinyPeriod = static_cast<int> (0.11f * sr);
    const float destinySmooth = 1.0f - std::exp (-1.0f / (0.04f * sr));

    // Ducker: attenuates the wet signal while the dry input is playing
    const float duckAttack  = 1.0f - std::exp (-1.0f / (0.004f * sr));
    const float duckRelease = 1.0f - std::exp (-1.0f / (juce::jlimit (20.0f, 1000.0f, param ("DUCK_REL")) * 0.001f * sr));

    // Transient shaper detectors
    const float fastA = 1.0f - std::exp (-1.0f / (0.0005f * sr));
    const float fastR = 1.0f - std::exp (-1.0f / (0.020f  * sr));
    const float slowA = 1.0f - std::exp (-1.0f / (0.020f  * sr));
    const float slowR = 1.0f - std::exp (-1.0f / (0.200f  * sr));

    // 5 Hz DC blocker in the loop: stops DC (or sub-sonic drift) building up at high feedback
    const float dcR = 1.0f - juce::MathConstants<float>::twoPi * 5.0f / sr;

    // tape wow / flutter / drift (in the loop)
    const double incWow[2]  = { juce::MathConstants<double>::twoPi * 0.55 / sampleRate, juce::MathConstants<double>::twoPi * 0.63 / sampleRate };
    const double incFlut[2] = { juce::MathConstants<double>::twoPi * 6.3  / sampleRate, juce::MathConstants<double>::twoPi * 7.1  / sampleRate };
    const float driftCoef  = std::exp (-juce::MathConstants<float>::twoPi * 1.0f / sr);
    const float driftNoise = std::sqrt (1.0f - driftCoef * driftCoef) * 1.732f * 0.35f;

    auto* leftIn  = buffer.getReadPointer (0);
    auto* rightIn = (numChannels > 1 && getMainBusNumInputChannels() > 1) ? buffer.getReadPointer (1) : leftIn;
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

    auto readShim = [this] (int c, float d)
    {
        const float pos = static_cast<float> (shimWrite) - d;
        const int i1 = static_cast<int> (std::floor (pos));
        const float f = pos - static_cast<float> (i1);
        const float* b = shimRing[c].data();
        constexpr int m = kShimRing - 1;
        return hermite (b[(i1 - 1) & m], b[i1 & m], b[(i1 + 1) & m], b[(i1 + 2) & m], f);
    };

    // state that is refreshed every few samples from the smoothed values
    float tapeDrive = 1.0f, tapeCoef = 1.0f, hissGain = 0.0f, loopDrive = 1.0f, loopCoef = 1.0f;
    bool loCutOn = false, hiCutOn = false;
    float peakL = 0.0f, peakR = 0.0f;

    // ================================================================ pass 1: delay engine + wet flavour chain
    for (int i = 0; i < numSamples; ++i)
    {
        // ---- smoothed parameters for this sample
        const float feedbackVal = smFeedback.getNextValue();
        const float duckingVal  = smDucking.getNextValue();
        const float widerDepth  = smWider.getNextValue()  * 0.006f * sr;   // up to +-6 ms, opposite on L/R
        const float richerDepth = smRicher.getNextValue() * 0.004f * sr;   // up to +-4 ms pitch wobble
        const float attackAmt   = smAttack.getNextValue();
        const float sustainAmt  = smSustain.getNextValue();
        const float tapeAmt     = smTape.getNextValue();
        const float swirlAmt    = smSwirl.getNextValue();
        const float ping        = smPing.getNextValue();   // 0 = normal / inverted routing, 1 = ping-pong
        const float swap        = smSwap.getNextValue();   // 0 = left/right as is, 1 = swapped (inverted / ping-pong)
        const float loHz        = smLoHz.getNextValue();
        const float hiHz        = smHiHz.getNextValue();
        const float rev         = smRev.getNextValue();
        const float shimAmt     = smShim.getNextValue();

        if ((i & 31) == 0)   // filter coefficients: refresh from the smoothed cutoffs every 32 samples
        {
            loCutOn = loHz > 20.5f;
            hiCutOn = hiHz < 19999.0f;
            for (int c = 0; c < 2; ++c)
            {
                loCut[c].setCutoffFrequency (loHz);
                hiCut[c].setCutoffFrequency (hiHz);
                if (! loCutOn) loCut[c].reset();
                if (! hiCutOn) hiCut[c].reset();
            }
        }

        if ((i & 15) == 0)   // tape: saturation drive, HF roll-off and a little hiss
        {
            tapeDrive = 1.0f + 5.0f * tapeAmt;
            const float tapeHz = 18000.0f * std::pow (0.3f, tapeAmt);
            tapeCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (tapeHz, sr * 0.45f) / sr);
            hissGain = 0.002f * tapeAmt * tapeAmt;

            loopDrive = 1.0f + tapeAmt;                                  // in-loop saturation: every repeat is squashed a bit more
            const float loopHz = 16000.0f * std::pow (0.4f, tapeAmt);   // ... and a bit darker
            loopCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (loopHz, sr * 0.45f) / sr);
        }

        // ---- modulated read positions
        const float base = smoothDelayTime.getNextValue();

        const float lfoW  = lfoShape (0, phaseWider, incWider);
        const float lfoR  = (lfoShape (1, phaseRicher1, incRicher1) + 0.5f * lfoShape (2, phaseRicher2, incRicher2)) * (1.0f / 1.5f);
        advanceLfo (0, phaseWider,   incWider);
        advanceLfo (1, phaseRicher1, incRicher1);
        advanceLfo (2, phaseRicher2, incRicher2);

        float modL = richerDepth * lfoR + widerDepth * lfoW;
        float modR = richerDepth * lfoR - widerDepth * lfoW;

        if (tapeAmt > 0.0f)   // tape transport: slow wow, fast flutter, random drift - independent per channel
        {
            const float wowD = tapeAmt * 0.0008f * sr, flutD = tapeAmt * 0.00003f * sr, driftD = tapeAmt * 0.0004f * sr;
            for (int c = 0; c < 2; ++c)
            {
                driftValue[c] = driftValue[c] * driftCoef + (rng.nextFloat() * 2.0f - 1.0f) * driftNoise;
                const float m = wowD * static_cast<float> (std::sin (phaseWow[c])) + flutD * static_cast<float> (std::sin (phaseFlut[c]))
                                + driftD * driftValue[c];
                (c == 0 ? modL : modR) += m;
                phaseWow[c]  += incWow[c];   if (phaseWow[c]  >= juce::MathConstants<double>::twoPi) phaseWow[c]  -= juce::MathConstants<double>::twoPi;
                phaseFlut[c] += incFlut[c];  if (phaseFlut[c] >= juce::MathConstants<double>::twoPi) phaseFlut[c] -= juce::MathConstants<double>::twoPi;
            }
        }

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
            modL += depth * destinyValue[0];
            modR += depth * destinyValue[1];
        }

        const float dlyL = juce::jlimit (3.0f, maxDelaySamples, base + modL);
        const float dlyR = juce::jlimit (3.0f, maxDelaySamples, base + modR);

        float rawA = readDelayed (delayLeftPtr,  dlyL);
        float rawB = readDelayed (delayRightPtr, dlyR);

        if (fadeLeft > 0)   // equal-power cross-fade from the previous delay position to the new one
        {
            const float t = 1.0f - static_cast<float> (fadeLeft) / static_cast<float> (fadeTotal);
            const float gNew = std::sin (juce::MathConstants<float>::halfPi * t);
            const float gOld = std::cos (juce::MathConstants<float>::halfPi * t);
            const float oldL = juce::jlimit (3.0f, maxDelaySamples, fadeOldDelay + modL);
            const float oldR = juce::jlimit (3.0f, maxDelaySamples, fadeOldDelay + modR);
            rawA = rawA * gNew + readDelayed (delayLeftPtr,  oldL) * gOld;
            rawB = rawB * gNew + readDelayed (delayRightPtr, oldR) * gOld;
            --fadeLeft;
        }

        if (rev > 0.0f)   // reverse: two heads read backwards through the last N samples, hann-windowed
        {
            const float maxLen = 0.5f * maxDelaySamples - 8.0f;
            float revA = 0.0f, revB = 0.0f;
            for (int h = 0; h < 2; ++h)
            {
                if (revLen[h] <= 0.0f)
                    revLen[h] = juce::jlimit (64.0f, maxLen, base);

                const float p = static_cast<float> (revPhase[h]);
                const float s = std::sin (juce::MathConstants<float>::pi * p);
                const float w = s * s;                       // w(p) + w(p + 0.5) = 1
                const float d = 2.0f * revLen[h] * p;        // delay grows 2 samples per sample -> read position runs backwards
                revA += w * readDelayed (delayLeftPtr,  juce::jlimit (3.0f, maxDelaySamples, d + modL));
                revB += w * readDelayed (delayRightPtr, juce::jlimit (3.0f, maxDelaySamples, d + modR));

                revPhase[h] += 1.0 / static_cast<double> (revLen[h]);
                if (revPhase[h] >= 1.0)
                {
                    revPhase[h] -= 1.0;
                    revLen[h] = juce::jlimit (64.0f, maxLen, base);   // length is only re-latched where the window is zero
                }
            }
            rawA = rawA * (1.0f - rev) + revA * rev;
            rawB = rawB * (1.0f - rev) + revB * rev;
        }
        else
        {
            revPhase[0] = 0.0;  revPhase[1] = 0.5;
            revLen[0] = revLen[1] = 0.0f;
        }

        // ---- DC blocker -> Lo-Cut -> Hi-Cut (inside the loop, so every repeat is filtered again)
        float tapA = rawA - dcX[0] + dcR * dcY[0];   dcX[0] = rawA;   dcY[0] = tapA;
        float tapB = rawB - dcX[1] + dcR * dcY[1];   dcX[1] = rawB;   dcY[1] = tapB;
        if (loCutOn) { tapA = loCut[0].processSample (0, tapA);  tapB = loCut[1].processSample (0, tapB); }
        if (hiCutOn) { tapA = hiCut[0].processSample (0, tapA);  tapB = hiCut[1].processSample (0, tapB); }

        if (tapeAmt > 0.0f)   // tape in the loop: soft saturation + HF loss on every pass
        {
            tapA = std::tanh (tapA * loopDrive) / loopDrive;
            tapB = std::tanh (tapB * loopDrive) / loopDrive;
            loopLp[0] += loopCoef * (tapA - loopLp[0]);   tapA = loopLp[0];
            loopLp[1] += loopCoef * (tapB - loopLp[1]);   tapB = loopLp[1];
        }
        else
        {
            loopLp[0] = tapA;  loopLp[1] = tapB;
        }

        tapA = loopLimit (tapA);
        tapB = loopLimit (tapB);

        // ---- shimmer: the fed-back signal is pitch-shifted (two crossfaded delay taps), so every repeat climbs / falls
        float fbSrcA = tapA, fbSrcB = tapB;
        shimRing[0][static_cast<size_t> (shimWrite)] = tapA;
        shimRing[1][static_cast<size_t> (shimWrite)] = tapB;
        if (shimAmt > 0.0f)
        {
            const float phi = shimPhase;
            const float phi2 = phi >= 0.5f ? phi - 0.5f : phi + 0.5f;
            const float w1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phi);
            const float w2 = 1.0f - w1;
            const float d1 = 2.0f + phi * shimWin, d2 = 2.0f + phi2 * shimWin;
            const float shA = w1 * readShim (0, d1) + w2 * readShim (0, d2);
            const float shB = w1 * readShim (1, d1) + w2 * readShim (1, d2);
            fbSrcA = loopLimit (tapA * (1.0f - shimAmt) + shA * shimAmt);
            fbSrcB = loopLimit (tapB * (1.0f - shimAmt) + shB * shimAmt);

            shimPhase += (1.0f - shimRatio) / shimWin;
            while (shimPhase >= 1.0f) shimPhase -= 1.0f;
            while (shimPhase < 0.0f)  shimPhase += 1.0f;
        }
        shimWrite = (shimWrite + 1) & (kShimRing - 1);

        const float inLeft  = leftIn[i];
        const float inRight = rightIn[i];

        // ---- ducker envelope follows the dry input
        const float inLevel = scL != nullptr ? std::max (std::abs (scL[i]), std::abs (scR[i]))
                                             : std::max (std::abs (inLeft), std::abs (inRight));
        duckEnvelope += (inLevel - duckEnvelope) * (inLevel > duckEnvelope ? duckAttack : duckRelease);
        // engaged from about -45 dB, fully by about -18 dB; feedback keeps running so trails return in the gaps
        const float duckAmount = juce::jlimit (0.0f, 1.0f, (duckEnvelope - 0.0056f) / (0.126f - 0.0056f));
        duckBuf[static_cast<size_t> (i)] = 1.0f - duckingVal * duckAmount;

        // ---- feed the lines. Ping Pong sums the input to mono and starts on the left so the bounce
        // is audible on any source. All routing changes are cross-faded (ping / swap), so switching
        // the mode while audio plays does not click.
        const float mid  = (inLeft + inRight) * 0.5f;
        const float injA = inLeft * (1.0f - ping);
        const float injB = inRight * (1.0f - ping) + mid * ping;

        const float fbA = fbSrcA * (1.0f - ping) + fbSrcB * ping;   // ping-pong feeds each line from the other
        const float fbB = fbSrcB * (1.0f - ping) + fbSrcA * ping;
        delayLeftPtr[writePosition]  = injA + fbA * feedbackVal;
        delayRightPtr[writePosition] = injB + fbB * feedbackVal;

        writePosition = (writePosition + 1) % delayBufLen;

        // wet output: left/right swapped for Inverted and Ping Pong
        float wl = tapA * (1.0f - swap) + tapB * swap;
        float wr = tapB * (1.0f - swap) + tapA * swap;

        // ---- wet flavour chain: Attack/Sustain -> Cheap -> Tape -> Swirl
        if (attackAmt != 0.0f || sustainAmt != 0.0f)
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
    const bool sauceActive = smSauce.getTargetValue() > 0.0f || smSauce.isSmoothing();
    if (sauceActive)
    {
        std::copy (wetL.begin(), wetL.begin() + numSamples, revL.begin());
        std::copy (wetR.begin(), wetR.begin() + numSamples, revR.begin());
        reverb.processStereo (revL.data(), revR.data(), numSamples);
    }

    // ================================================================ pass 2: Sauce -> Mono -> Pan -> Ducker -> levels
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t k = static_cast<size_t> (i);
        const float dryVal   = smDry.getNextValue();
        const float wetVal   = smWet.getNextValue();
        const float panVal   = smPan.getNextValue();
        const float sauceAmt = smSauce.getNextValue();

        float wl = wetL[k];
        float wr = wetR[k];

        if (sauceActive)
        {
            wl += sauceAmt * revL[k];
            wr += sauceAmt * revR[k];
        }

        if (mono)
            wl = wr = 0.5f * (wl + wr);

        // PAN positions the delayed signal only (balance law, centre = unity)
        const float panGainL = panVal > 0.0f ? 1.0f - panVal : 1.0f;
        const float panGainR = panVal < 0.0f ? 1.0f + panVal : 1.0f;

        const float wetGain = duckBuf[k] * wetVal;
        const float wetOutL = safetyLimit (wl * wetGain * panGainL);   // the limiter only ever sees the wet signal
        const float wetOutR = safetyLimit (wr * wetGain * panGainR);

        const float outLeft  = leftIn[i]  * dryVal + wetOutL;
        const float outRight = rightIn[i] * dryVal + wetOutR;

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
