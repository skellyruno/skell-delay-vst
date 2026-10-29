#pragma once

#include <JuceHeader.h>
#include "GatePatterns.h"

// Tempo-synced note values (beats = quarter notes). The FEEL parameter then makes them
// straight, dotted or triplet - same scheme as Comeback Kid.
struct SyncDivision { const char* name; float beats; };
inline constexpr SyncDivision kSyncDivisions[] = {
    { "1/64", 0.0625f }, { "1/32", 0.125f }, { "1/16", 0.25f }, { "1/8", 0.5f },
    { "1/4",  1.0f },    { "1/2",  2.0f },   { "1/1",  4.0f }
};
inline constexpr int kNumSyncDivisions = static_cast<int> (sizeof (kSyncDivisions) / sizeof (kSyncDivisions[0]));
inline constexpr int kDefaultSyncDivision = 3;   // 1/8

inline constexpr float kFeelMultiplier[] = { 1.0f, 1.5f, 2.0f / 3.0f };   // straight, dotted, triplet

// Shimmer: pitch interval applied to the repeats as they are fed back
inline constexpr const char* kShimNames[] = { "+12", "+7", "-12", "-7" };
inline constexpr float kShimRatio[] = { 2.0f, 1.49831f, 0.5f, 0.66742f };
inline constexpr int kNumShimIntervals = 4;

class DelayAudioProcessor  : public juce::AudioProcessor
{
public:
    DelayAudioProcessor();
    ~DelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;   // host stop/start: clears echoes, filters, reverb

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

    // Soft bypass: the input stops feeding the delay and the dry signal passes at unity, but the echoes already
    // in the loop ring out. Hosts see this as the plugin's bypass parameter.
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("BYPASS"); }

    // Kill: fades the echoes out over 10 ms, empties the delay line, and lets new input in again
    void killEchoes() { killPending.store (true); }

    // User presets (files on disk). A name may contain one "/" to put it in a category folder.
    static void setUserPresetDirectory (const juce::File& dir);
    static juce::File getUserPresetDirectory();
    juce::Array<juce::File> getUserPresetFiles() const;
    bool saveUserPreset (const juce::String& nameWithCategory);
    bool loadUserPreset (const juce::File& file);
    bool deleteUserPreset (const juce::File& file);
    juce::String getCurrentPresetName();          // factory or user preset currently loaded
    juce::String getCurrentUserPresetPath() const;  // relative path, empty when a factory preset is loaded

    float getGatePosition() const { return gatePosition.load(); }   // 0..1 across the gate's bar, for the readout

    // Factory presets are exposed as host programs. The current one is stored in the plugin state.
    void applyPreset (int index);

    // A/B compare: two full parameter snapshots; selecting a slot stores the current one first
    void selectABSlot (int slot);
    int getABSlot() const { return abActive; }

    float getLeftLevel();   // returns peak since last call
    float getRightLevel();

    // Actual delay time in use (ms), whether free or tempo-synced - for the GUI
    float getEffectiveDelayMs() const { return effectiveDelayMs.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    // Hosts may hand over any block size; it is processed in chunks of at most this many samples
    // so that no memory is ever allocated on the audio thread.
    static constexpr int kMaxChunk = 4096;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float computeDelayMs() const;   // free ms, or note value x feel at the host tempo
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    void clearState();
    void snapSmoothers();            // jump every smoother to its parameter (no ramp)
    void processChunk (juce::AudioBuffer<float>& buffer, const float* scL, const float* scR);   // sc* = optional ducker sidechain

    // ---- delay engine ----
    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;
    double sampleRate = 44100.0;
    double hostBpm = 120.0;

    // Small time changes glide (SMOOTH sets how long). Big jumps - a new note value, a preset,
    // a tempo leap - cross-fade between the old and the new position instead, so there is no
    // pitch-sweep "siren" and no click.
    juce::LinearSmoothedValue<float> smoothDelayTime;
    float lastSmoothParam = 50.0f;
    float lastTargetSamples = -1.0f;
    float fadeOldDelay = 0.0f;
    int fadeLeft = 0, fadeTotal = 1;

    // Per-parameter smoothing (no zipper noise / clicks when a knob or automation moves)
    Smoother smDry, smWet, smFeedback, smDucking, smPan, smTape, smSwirl, smSauce,
             smWider, smRicher, smAttack, smSustain, smPing, smSwap, smLoHz, smHiHz, smRev, smShim, smBypass, smBBD;

    // Reverse: two read heads sweep backwards through the last N ms, hann-windowed and offset by half a segment
    double revPhase[2] { 0.0, 0.5 };
    float revLen[2] { 0.0f, 0.0f };

    // Tape in the loop: wow / flutter / drift on the read position, soft saturation and a per-repeat HF roll-off
    double phaseWow[2] { 0.0, 0.9 }, phaseFlut[2] { 0.0, 1.7 };
    float driftValue[2] { 0.0f, 0.0f };
    float loopLp[2] { 0.0f, 0.0f };

    // Shimmer: delay-line pitch shifter (two crossfaded taps) on the fed-back signal
    static constexpr int kShimRing = 16384;
    std::vector<float> shimRing[2];
    int shimWrite = 0;
    float shimPhase = 0.0f;
    float shimWin = 1500.0f;

    // BBD (bucket-brigade) emulation, in the loop: the clock rate falls as the delay gets longer, so the
    // repeats get darker, grittier and noisier the longer the time
    float bbdPre[2] { 0.0f, 0.0f }, bbdPost1[2] { 0.0f, 0.0f }, bbdPost2[2] { 0.0f, 0.0f }, bbdHold[2] { 0.0f, 0.0f };
    float bbdCount = 0.0f, bbdEnv = 0.0f;

    // Kill / bypass / gate
    std::atomic<bool> killPending { false };
    int killState = 0;            // 0 idle, 1 fading out, 2 silent (clear at the end of the chunk), 3 fading back in
    float killGain = 1.0f;
    std::vector<float> bypBuf;
    double gateBeats = 0.0;
    float gateGain = 1.0f;
    std::atomic<float> gatePosition { 0.0f };

    juce::ValueTree abState[2];
    int abActive = 0;

    // DC blocker (always on) + Lo-cut / Hi-cut inside the feedback loop, one per channel
    float dcX[2] { 0.0f, 0.0f }, dcY[2] { 0.0f, 0.0f };
    juce::dsp::StateVariableTPTFilter<float> loCut[2], hiCut[2];

    // Wider / Richer / Destiny modulation of the delay time
    double phaseWider = 0.0, phaseRicher1 = 0.0, phaseRicher2 = 0.0;
    float rndHeld[3] { 0.0f, 0.0f, 0.0f }, rndSmooth[3] { 0.0f, 0.0f, 0.0f };   // "Random" LFO shape: glided sample & hold
    juce::Random rng;
    float destinyTarget[2] { 0.0f, 0.0f };
    float destinyValue[2]  { 0.0f, 0.0f };
    int destinyCountdown = 0;

    // ---- wet-signal flavour chain (outside the loop): attack/sustain > cheap > tape > swirl > sauce > ducker
    float envFast = 0.0f, envSlow = 0.0f;           // transient shaper detectors
    float tapeLp[2] { 0.0f, 0.0f };                 // tape high-frequency roll-off
    double phaseSwirl = 0.0;                        // phaser LFO
    float apState[2][4] {};                         // 4 all-pass stages per channel
    float phaserFeedback[2] { 0.0f, 0.0f };
    juce::Reverb reverb;                            // "sauce"
    std::vector<float> wetL, wetR, revL, revR, duckBuf;

    float duckEnvelope = 0.0f;

    std::atomic<float> effectiveDelayMs { 250.0f };
    std::atomic<float> leftLevel { 0.0f };
    std::atomic<float> rightLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAudioProcessor)
};
