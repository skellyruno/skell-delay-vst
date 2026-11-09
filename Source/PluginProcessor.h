#pragma once

#include <JuceHeader.h>
#include "GatePatterns.h"
#include "OnsetDetector.h"

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

// Auto-pan: the echoes sweep left/right once per cycle, locked to the song (same clock as the gate)
inline constexpr const char* kAutoPanNames[] = { "Off", "1/1", "1/2", "1/4", "1/8", "1/16" };
inline constexpr double kAutoPanBeats[] = { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25 };   // cycle length in quarter notes
inline constexpr int kNumAutoPan = 6;
inline constexpr const char* kRepeatNames[] = { "Off", "1", "2", "3", "4", "5", "6", "7", "8" };
inline constexpr int kNumRepeats = 9;
inline constexpr float kPocketLatencyMs = 40.0f, kPocketMaxNudgeMs = 30.0f, kPocketDetectMs = 2.0f;
inline constexpr int kPocketRing = 32768;
inline constexpr int kTeleRing = 1 << 17;
inline constexpr float kElasticRefRate = 4.0f;   // onsets per second that count as 'neutral' delivery

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

    float getGatePosition() const { return gatePosition.load(); }
    float getAutoPanValue() const { return autoPanValue.load(); }   // current auto-pan offset (-1..1), for the readout   // 0..1 across the gate's bar, for the readout

    // Factory presets are exposed as host programs. The current one is stored in the plugin state.
    void applyPreset (int index);

    // A/B compare: two full parameter snapshots; selecting a slot stores the current one first
    void selectABSlot (int slot);
    int getABSlot() const { return abActive; }

    // Pocket meter: timing of the latest vocal words vs the 1/16 grid (ms, negative = early); ago 0 = newest
    float getPocketError (int ago) const { return pocketErr[static_cast<size_t> ((pocketCount.load() - 1 - ago) & 7)].load(); }
    int getPocketCount() const { return pocketCount.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    // Echo trail: level (0..1, peak) and brightness (0 dark..1 bright) of the most recent repeats, one entry per delay period; ago 0 = newest
    static constexpr int kTrail = 16;
    float getTrailLevel (int ago) const { return trailLevel[static_cast<size_t> ((trailCount.load() - 1 - ago) & (kTrail - 1))].load(); }
    float getTrailTone  (int ago) const { return trailTone [static_cast<size_t> ((trailCount.load() - 1 - ago) & (kTrail - 1))].load(); }
    int   getTrailCount() const { return trailCount.load(); }

    // Plain-language state of the smart features, so the interface can say why one is not doing anything.
    // feature: 0 Elastic, 1 Telepathy, 2 Pocket. 'problem' is set when the user should do or change something.
    struct SmartStatus { juce::String text; bool problem = false; };
    SmartStatus getSmartStatus (int feature) const;

    // Delay time (ms) that matches how the performer is playing right now (0 = nothing recent to learn from) - for LEARN
    float getLearnedIntervalMs() const { return learnedMs.load(); }

    float getLeftLevel();   // returns peak since last call
    float getRightLevel();

    // Actual delay time in use (ms), whether free or tempo-synced - for the GUI
    float getEffectiveDelayMs() const { return effectiveDelayMs.load(); }

    // One searchable preset (factory or a file on disk)
    struct PresetEntry
    {
        juce::String name, tags, key;     // key: "F:<name>" or "U:<relative path>" - what favourites are stored under
        bool user = false;
        int factoryIndex = -1;
        juce::File file;
    };
    std::vector<PresetEntry> getPresetEntries() const;
    void loadPresetEntry (const PresetEntry& e);
    static bool isFavourite (const juce::String& key);
    static void setFavourite (const juce::String& key, bool on);

    // Randomise within musical limits: every value is drawn from a safe range (feedback never runs away, wet stays audible...)
    // and moved only 'amount' (0..1) of the way there. Time and the dry/wet mix can be held.
    void randomise (float amount, bool keepTime, bool keepMix);

    // Interface settings that travel with the plugin state
    int  getUiWidth() const;           void setUiWidth (int w);
    bool getHighContrast() const;      void setHighContrast (bool on);
    bool getHintSeen() const;          void setHintSeen (bool on);
    bool getLargeText() const;         void setLargeText (bool on);

    static constexpr int kStateVersion = 2;     // 1: before Elastic/Pocket/Telepathy/Ecology/Grain/Spectral... 2: current (+ UI settings)

    juce::UndoManager undoManager;     // must be declared before apvts
    // Starts a new undo step. First records any parameter change the host-sync timer has not copied into the state yet,
    // so it lands in the step it belongs to (the previous one) and not in this one.
    void newUndoStep (const juce::String& name = {});
    void syncStateToTree();

    juce::AudioProcessorValueTreeState apvts;

private:
    void applyTree (const juce::ValueTree& tree);   // sets every parameter from a saved state (undoable, unlike replaceState)
    juce::ValueTree uiTree() const;

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
    float repeatTimer = 0.0f, repeatGate = 1.0f, hissEnv = 0.0f;
    float elasticMul = 1.0f, elasticRate = kElasticRefRate, elFast = 0.0f, elSlow = 0.0f;
    float elFastA = 0.0f, elFastR = 0.0f, elSlowA = 0.0f, elSlowR = 0.0f;
    int elSince = 1000000000;
    // Telepathy (rhythm prediction + pre-echo ghosts) and Echo Ecology (per-repeat genomes)
    std::vector<float> teleRing[2];
    int teleWrite = 0, teleGhostLen = 0, teleGhostPos = -1, teleCurCount = 0, telePrevCount = 0;
    long long teleClock = 0, teleGhostStart = 0, telePredT = 0;
    bool telePending = false;
    float teleCur[16] {}, telePrev[16] {}, teleLp[2] {}, teleTrust = 0.5f, telePredIoi = 0.0f, teleGhostD0 = 0.0f;
    int ecoCount = 0, ecoWake = 0;
    float ecoGain = 1.0f, ecoGainT = 1.0f, ecoCoef = 1.0f, ecoCoefT = 1.0f, ecoVel = 0.0f, ecoOff = 0.0f, ecoAct = 0.0f, ecoLp[2] {};
    // Onset detection (spectral flux) for Elastic / Telepathy / Pocket; flags are per sample of the current chunk
    FluxOnsetDetector fluxEl, fluxPocket;
    std::vector<unsigned char> fluxFlags;
    std::vector<float> monoBuf;
    std::atomic<float> learnedMs { 0.0f };
    std::atomic<int> sinceOnsetMs { 100000 }, teleLocked { 0 };      // for getSmartStatus
    std::atomic<float> teleTrustShown { 0.5f }, elasticShown { 1.0f };
    // Granular echoes (two overlapping grains per channel, random offset and slight pitch scatter per grain)
    float grainPhase[2] { 0.0f, 0.5f }, grainOff[2][2] {}, grainRate[2][2] {};
    // Spectral delay: complementary one-pole split, low band read later and high band earlier (or the reverse)
    juce::dsp::LinkwitzRileyFilter<float> specLow[2], specHigh[2];
    // Performance buttons: stutter (looped slice), tape stop (read head slows to a halt), reverse throw (one backwards swell)
    std::vector<float> stutBuf[2];
    int stutCap = 0, stutPos = 0, stutN = 0;
    float stutMix = 0.0f;
    float tsSpeed = 1.0f, tsOff = 0.0f, tsGain = 1.0f;
    float rtPhase = 0.0f, rtLen = 0.0f;
    bool rtActive = false, prevRevThrow = false;
    // Echo trail telemetry
    std::atomic<float> trailLevel[kTrail] {}, trailTone[kTrail] {};
    std::atomic<int> trailCount { 0 };
    float trailPeak = 0.0f, trailToneAcc = 1.0f;
    int trailSamples = 0;
    std::vector<float> pocketRing[2];
    int pocketWrite = 0, pocketFade = 0, pocketQuiet = 0, chunkOffset = 0;
    float pocketCur = 0.0f, pocketFrom = 0.0f, pocketFast = 0.0f;
    bool pocketArmed = false;
    std::atomic<bool> hostPlaying { false };
    std::atomic<float> pocketErr[8] {};
    std::atomic<int> pocketCount { 0 };
    float gateGain = 1.0f;
    std::atomic<float> gatePosition { 0.0f };
    float apDepthSmooth = 0.0f, apRndHeld = 0.0f, apRndSmooth = 0.0f;
    long long apCycleIdx = -1;
    std::atomic<float> autoPanValue { 0.0f };

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
