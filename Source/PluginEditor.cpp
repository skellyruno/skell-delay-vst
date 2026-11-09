#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>

namespace
{
    constexpr int kWidth  = 720;   // same aspect as the artwork (2135 x 736)
    constexpr int kHeight = 248;

    const juce::Colour kLime (0xff39ff14);

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)); }

    juce::String hertz (double v)
    {
        return v >= 1000.0 ? juce::String (v / 1000.0, 1) + "k" : juce::String (juce::roundToInt (v));
    }

    juce::String milliseconds (double v)
    {
        if (v < 10.0)  return juce::String (v, 2) + " ms";
        if (v < 100.0) return juce::String (v, 1) + " ms";
        return juce::String (juce::roundToInt (v)) + " ms";
    }

    void styleLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (10.5f).withStyle ("Bold Italic"));
        l.setColour (juce::Label::textColourId, kLime);
        l.setJustificationType (juce::Justification::centred);
    }
}

DelayAudioProcessorEditor::DelayAudioProcessorEditor (DelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const int savedWidth = audioProcessor.getUiWidth();
    bgImage = juce::ImageFileFormat::loadFrom (BinaryData::skell_bg_png, BinaryData::skell_bg_pngSize);
    setLookAndFeel (&skellLookAndFeel);

    addAndMakeVisible (content);   // fixed 720x248 layout, scaled to the window size
    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (kWidth) / kHeight);

    content.addAndMakeVisible (leftMeter);
    content.addAndMakeVisible (rightMeter);
    content.addAndMakeVisible (delayDisplay);

    // ---- knobs: (knob, parameter id, label, readout shown while hovering / dragging)
    setupKnob (feedback, "FEEDBACK", "FEEDBACK", percent);
    setupKnob (pan, "PAN", "PAN", [] (double v)
    {
        const int p = juce::roundToInt (std::abs (v) * 100.0);
        return p == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (p);
    });
    setupKnob (hiCut,   "HICUT",   "TONE",   hertz);
    setupKnob (tape,    "TAPE",    "TAPE",   percent);
    setupKnob (ducking, "DUCKING", "DUCKER", percent);

    setupKnob (dry,     "DRY",     "DRY",    percent);
    setupKnob (wet,     "WET",     "WET",    percent);

    // TIME: its attachment (ms vs note value) is chosen by applyTimeMode()
    time.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    time.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    content.addAndMakeVisible (time.slider);
    styleLabel (time.label, "TIME");
    content.addAndMakeVisible (time.label);
    allLabels.push_back (&time.label);
    time.slider.undo = &audioProcessor.undoManager;
    time.slider.setTitle ("Time");

    // ---- switches
    syncBtn.setClickingTogglesState (true);
    syncBtn.onClick = [this]() { setSync (syncBtn.getToggleState()); };
    content.addAndMakeVisible (syncBtn);

    pingBtn.setClickingTogglesState (true);
    pingBtn.onClick = [this]() { setChoice ("FB_MODE", pingBtn.getToggleState() ? 2 : 0); };
    content.addAndMakeVisible (pingBtn);

    revBtn.setClickingTogglesState (true);
    revBtn.onClick = [this]() { setChoice ("REVERSE", revBtn.getToggleState() ? 1 : 0); };
    content.addAndMakeVisible (revBtn);

    // SHIM cycles: off -> +12 -> +7 -> -12 -> -7 -> off (amount 50% unless a preset set its own)
    shimBtn.onClick = [this]()
    {
        const float amount = audioProcessor.apvts.getRawParameterValue ("SHIMMER")->load();
        const int pitch = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("SHIM_PITCH")->load() + 0.5f);
        const int state = amount > 0.001f ? 1 + pitch : 0;
        const int next = (state + 1) % (kNumShimIntervals + 1);
        if (next == 0)
        {
            setParamValue ("SHIMMER", 0.0f);
        }
        else
        {
            setParamValue ("SHIM_PITCH", static_cast<float> (next - 1));
            setParamValue ("SHIMMER", amount > 0.001f ? amount : 0.5f);
        }
        refreshButtons();
    };
    content.addAndMakeVisible (shimBtn);

    // presets: click the name to open the menu
    for (auto* b : { &presetPrev, &presetBtn, &presetNext })
    {
        b->getProperties().set ("flat", true);
        b->getProperties().set ("big", true);
        content.addAndMakeVisible (b);
    }
    presetBtn.onClick  = [this]() { toggleBrowser(); };
    presetPrev.onClick = [this]() { stepPreset (-1); };
    presetNext.onClick = [this]() { stepPreset (+1); };
    content.addAndMakeVisible (modCtl);

    hintLabel.setText ("tip: ADV opens more controls  |  hover any knob for help  |  double-click resets, Shift-drag is fine control", juce::dontSendNotification);
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setInterceptsMouseClicks (false, false);
    hintLabel.getProperties().set ("note", true);
    hintLabel.setVisible (! audioProcessor.getHintSeen());
    content.addAndMakeVisible (hintLabel);
    allLabels.push_back (&hintLabel);

    abBtn.getProperties().set ("flat", true);
    abBtn.getProperties().set ("led", true);
    abBtn.getProperties().set ("big", true);
    abBtn.onClick = [this]()
    {
        audioProcessor.selectABSlot (1 - audioProcessor.getABSlot());
        refreshButtons();
    };
    content.addAndMakeVisible (abBtn);

    // BBD / KILL / BYPASS
    bbdBtn.setClickingTogglesState (true);
    bbdBtn.onClick = [this]() { setChoice ("BBD", bbdBtn.getToggleState() ? 1 : 0); };
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.onClick = [this]() { setChoice ("BYPASS", bypassBtn.getToggleState() ? 1 : 0); };
    killBtn.onClick = [this]() { audioProcessor.killEchoes(); };
    // APAN cycles: off -> 1/1 -> 1/2 -> 1/4 -> 1/8 -> 1/16 -> off (depth 80% unless a preset set its own)
    apanBtn.onClick = [this]()
    {
        const int cur = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("AUTOPAN")->load() + 0.5f);
        const int next = (cur + 1) % kNumAutoPan;
        if (next > 0 && audioProcessor.apvts.getRawParameterValue ("AP_DEPTH")->load() < 0.01f)
            setParamValue ("AP_DEPTH", 0.8f);
        setParamValue ("AUTOPAN", static_cast<float> (next));
        refreshButtons();
    };
    for (auto* b : { &bbdBtn, &killBtn, &bypassBtn, &apanBtn })
    {
        b->getProperties().set ("flat", true);
        content.addAndMakeVisible (b);
    }

    // the readout doubles as the gate control: click = next pattern, scroll = depth
    delayDisplay.onClick = [this]()
    {
        const int cur = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("GATE")->load() + 0.5f);
        setParamValue ("GATE", static_cast<float> ((cur + 1) % kNumGatePatterns));
    };
    delayDisplay.onWheel = [this] (float dy)
    {
        const float d = audioProcessor.apvts.getRawParameterValue ("GATE_DEPTH")->load();
        setParamValue ("GATE_DEPTH", juce::jlimit (0.0f, 1.0f, d + dy * 0.5f));
    };

    // feel: one button that cycles straight -> dotted -> triplet; only shown while tempo sync is on
    feelBtn.onClick = [this]()
    {
        const int cur = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FEEL")->load() + 0.5f);
        setChoice ("FEEL", (cur + 1) % 3);
    };
    feelBtn.getProperties().set ("flat", true);
    content.addChildComponent (feelBtn);
    for (auto* b : { &syncBtn, &pingBtn, &revBtn })
        b->getProperties().set ("flat", true);
    shimBtn.getProperties().set ("flat", true);
    shimBtn.getProperties().set ("led", true);

    // ---- advanced panel (added last so it sits on top of the front controls)
    {
        auto signedPct = [] (double v) { const int p = juce::roundToInt (v * 100.0); return (p > 0 ? "+" : "") + juce::String (p); };
        auto plain     = [] (double v) { return juce::String (juce::roundToInt (v)); };
        auto millis    = [] (double v) { return juce::String (juce::roundToInt (v)) + "ms"; };
        auto times     = [] (double v) { return juce::String (v, 2) + "x"; };
        content.addChildComponent (advPanel);
        setupKnob (advLoCut,     "LOCUT",      "LO CUT",    hertz,     &advPanel);
        setupKnob (advAttack,    "ATTACK",     "ATTACK",    signedPct, &advPanel);
        setupKnob (advSustain,   "SUSTAIN",    "SUSTAIN",   signedPct, &advPanel);
        setupKnob (advSwirl,     "SWIRL",      "SWIRL",     percent,   &advPanel);
        setupKnob (advSauce,     "SAUCE",      "SAUCE",     percent,   &advPanel);
        setupKnob (advWider,     "WIDER",      "WIDER",     percent,   &advPanel);
        setupKnob (advRicher,    "RICHER",     "RICHER",    percent,   &advPanel);
        setupKnob (advShimmer,   "SHIMMER",    "SHIMMER",   percent,   &advPanel);
        content.addChildComponent (advPanel2);
        setupKnob (advElastic,   "ELASTIC",    "ELASTIC",   percent,   &advPanel2);
        setupKnob (advTelepathy, "TELEPATHY",  "TELEPATHY", percent,   &advPanel2);
        setupKnob (advEcology,   "ECOLOGY",    "ECOLOGY",   percent,   &advPanel2);
        setupKnob (advNudge,     "POCKET_AMT", "NUDGE",     percent,   &advPanel2);
        content.addChildComponent (advPanel3);
        setupKnob (advGrain,     "GRAIN",      "GRAIN",     percent,   &advPanel3);
        setupKnob (advSpectral,  "SPECTRAL",   "SPECTRAL",  percent,   &advPanel3);
        content.addChildComponent (advPanel4);
        advPanel4.addAndMakeVisible (trail);
        const char* notes[5] = { "delay follows how fast you perform", "a ghost of the word just before the next",
                                 "every repeat lives and dies on its own", "pulls words toward the grid", "shows how each word lands" };
        for (int n = 0; n < 5; ++n)
        {
            advNote[n].setText (notes[n], juce::dontSendNotification);
            advNote[n].setJustificationType (juce::Justification::centred);
            advNote[n].setFont (juce::FontOptions (8.0f));
            advNote[n].setColour (juce::Label::textColourId, juce::Colour (0xff2a7a14));
            advNote[n].setInterceptsMouseClicks (false, false);
            advNote[n].getProperties().set ("note", true);
            allLabels.push_back (&advNote[n]);
            advPanel2.addAndMakeVisible (advNote[n]);
        }
        setupKnob (advSmooth,    "SMOOTH",     "SMOOTH",    plain,     &advPanel);
        setupKnob (advDuckRel,   "DUCK_REL",   "DUCK REL",  millis,    &advPanel);
        setupKnob (advGateDepth, "GATE_DEPTH", "GATE",      percent,   &advPanel);
        setupKnob (advPanDepth,  "AP_DEPTH",   "PAN DEPTH", percent,   &advPanel);
        setupKnob (advModRate,   "MOD_RATE",   "MOD RATE",  times,     &advPanel);
        setupKnob (advRepeats,   "REPEATS",    "REPEATS",   nullptr,   &advPanel);
        setupKnob (advFbMode,    "FB_MODE",    "FB MODE",   nullptr,   &advPanel);

        advCheap.onClick   = [this]() { setChoice ("CHEAP",   advCheap.getToggleState()   ? 1 : 0); };
        advMono.onClick    = [this]() { setChoice ("MONO",    advMono.getToggleState()    ? 1 : 0); };
        advDestiny.onClick = [this]() { setChoice ("DESTINY", advDestiny.getToggleState() ? 1 : 0); };
        advPocket.onClick  = [this]() { setChoice ("POCKET",  advPocket.getToggleState()  ? 1 : 0); };
        for (auto* b : { &advCheap, &advMono, &advDestiny, &advPocket })
        {
            b->setClickingTogglesState (true);
            b->getProperties().set ("flat", true);
            (b == &advPocket ? advPanel2 : advPanel).addAndMakeVisible (b);
        }

        advBtn.getProperties().set ("flat", true);
        advBtn.getProperties().set ("led", true);
        advBtn.onClick = [this]() { setAdvPage ((advPage + 1) % 5); };   // closed -> sound -> smart -> play -> view -> closed

        // page 3: play (grain, spectral, performance buttons, tap / learn)
        const char* pn[5] = { "grains of echo", "lows late, highs early", "hold: loop a slice",
                              "hold: wind down like tape", "tap: backwards swell" };
        for (int n = 0; n < 5; ++n)
        {
            playNote[n].setText (pn[n], juce::dontSendNotification);
            playNote[n].setJustificationType (juce::Justification::centred);
            playNote[n].setFont (juce::FontOptions (8.0f));
            playNote[n].getProperties().set ("note", true);
            playNote[n].setInterceptsMouseClicks (false, false);
            advPanel3.addAndMakeVisible (playNote[n]);
            allLabels.push_back (&playNote[n]);
        }
        momentary (stutterBtn, "STUTTER");
        momentary (tapeStopBtn, "TAPESTOP");
        revThrowBtn.onClick = [this]()
        {
            setParamValue ("REVTHROW", 1.0f);
            juce::Component::SafePointer<DelayAudioProcessorEditor> safe (this);
            juce::Timer::callAfterDelay (80, [safe] { if (safe != nullptr) safe->setParamValue ("REVTHROW", 0.0f); });
        };
        tapBtn.onClick = [this]() { doTap(); };
        learnBtn.onClick = [this]() { doLearn(); };
        for (auto* b : { &stutterBtn, &tapeStopBtn, &revThrowBtn, &tapBtn, &learnBtn })
        {
            b->getProperties().set ("big", true);
            advPanel3.addAndMakeVisible (b);
            b->setTitle (b->getButtonText());
        }
        playInfo.setJustificationType (juce::Justification::centred);
        playInfo.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        playInfo.setInterceptsMouseClicks (false, false);
        advPanel3.addAndMakeVisible (playInfo);
        allLabels.push_back (&playInfo);

        // page 4: view (echo trail, accessibility, undo / redo)
        hcBtn.setClickingTogglesState (true);
        bigTextBtn.setClickingTogglesState (true);
        hcBtn.onClick = [this]() { audioProcessor.setHighContrast (hcBtn.getToggleState());  applyUiSettings(); };
        bigTextBtn.onClick = [this]() { audioProcessor.setLargeText (bigTextBtn.getToggleState());  applyUiSettings(); };
        undoBtn.onClick = [this]() { audioProcessor.undoManager.undo();  refreshButtons(); };
        redoBtn.onClick = [this]() { audioProcessor.undoManager.redo();  refreshButtons(); };
        resetSizeBtn.onClick = [this]() { setSize (kWidth, kHeight); };
        for (auto* b : { &hcBtn, &bigTextBtn, &undoBtn, &redoBtn, &resetSizeBtn })
        {
            b->getProperties().set ("flat", true);
            b->getProperties().set ("big", true);
            advPanel4.addAndMakeVisible (b);
            b->setTitle (b->getButtonText());
        }
        hcBtn.getProperties().set ("led", true);
        bigTextBtn.getProperties().set ("led", true);

        content.addAndMakeVisible (advBtn);
    }

    // ---- hover tips: every switch and hidden gesture explains itself
    auto tip = [] (auto& c, const char* t) { c.setTooltip (t); c.setDescription (t); };
    tip (syncBtn,   "Tempo sync: lock the delay time to the host tempo");
    tip (pingBtn,   "Ping pong: echoes bounce between left and right");
    tip (revBtn,    "Reverse: echoes play backwards");
    tip (shimBtn,   "Shimmer: click to cycle pitch-shifted repeats (+12, +7, -12, -7, off)");
    tip (feelBtn,   "Feel: click to cycle straight, dotted and triplet");
    tip (modCtl,    "Modulation: click for the shape, drag or scroll for the speed");
    tip (bbdBtn,    "BBD: analogue bucket-brigade chip character");
    tip (killBtn,   "Kill: instantly clear all echoes");
    tip (bypassBtn, "Bypass: stops new sound entering, echoes ring out");
    tip (apanBtn,   "Auto pan: click to cycle the speed (off, 1/1 ... 1/16)");
    tip (presetBtn, "Presets: click to browse, save or delete");
    tip (presetPrev, "Previous preset");
    tip (presetNext, "Next preset");
    tip (advBtn,    "Advanced: click for page 1 (sound), 2 (smart), 3 (play), 4 (view), then closed");
    tip (abBtn,     "A/B compare: swap between two complete settings. Each side keeps its own sound");
    tip (advGrain.slider,    "Grain: each echo is scattered into overlapping grains with small timing and pitch differences");
    tip (advSpectral.slider, "Spectral: lows and highs get different delay times, the split widens with every repeat");
    tip (stutterBtn,   "Stutter: hold to loop a short slice of the echo, release to continue");
    tip (tapeStopBtn,  "Tape stop: hold and the echoes slow to a halt, release and they spin back up");
    tip (revThrowBtn,  "Reverse throw: one backwards swell of the echo");
    tip (tapBtn,       "Tap tempo: tap the beat, the delay time follows (switches to free time)");
    tip (learnBtn,     "Learn: set the delay time from how fast you are playing or singing right now");
    tip (hcBtn,        "High contrast: brighter text and outlines");
    tip (bigTextBtn,   "Large text: bigger labels, readouts and tooltips");
    tip (undoBtn,      "Undo the last change (Ctrl/Cmd+Z)");
    tip (redoBtn,      "Redo (Ctrl/Cmd+Shift+Z)");
    tip (resetSizeBtn, "Back to the default window size");
    tip (trail,        "Echo trail: how loud and how bright each repeat is, one bar per delay time");
    tip (advTelepathy.slider, "Telepathy: learns your rhythm and swells a faint ghost of the last word just before the next one is expected");
    tip (advEcology.slider,   "Ecology: each repeat ages, darkens, drifts and may die on its own; quiet rooms make echoes bloom, busy ones eat them");
    tip (delayDisplay, "Click: next gate pattern. Scroll: gate depth. Top right: decay time to -60 dB");
    tip (time.slider,     "Time: delay time (note value when synced)");
    tip (feedback.slider, "Feedback: how much of each echo is repeated");
    tip (pan.slider,      "Pan: position of the echoes in the stereo field");
    tip (hiCut.slider,    "Tone: low-pass filter on the echoes (darker to the left)");
    tip (tape.slider,     "Tape: wobble, saturation, roll-off and a little hiss");
    tip (ducking.slider,  "Ducker: lowers the echoes while you play");
    tip (dry.slider,      "Dry: level of the unprocessed signal");
    tip (wet.slider,      "Wet: level of the echoes");
    tip (advLoCut.slider,     "Lo cut: high-pass filter on the echoes");
    tip (advAttack.slider,    "Attack: softens or sharpens the start of each echo");
    tip (advSustain.slider,   "Sustain: lengthens or shortens the body of each echo");
    tip (advSwirl.slider,     "Swirl: phaser on the echoes");
    tip (advSauce.slider,     "Sauce: reverb on the echoes");
    tip (advWider.slider,     "Wider: stereo widening through opposite time modulation");
    tip (advRicher.slider,    "Richer: chorus-style pitch wobble");
    tip (advShimmer.slider,   "Shimmer amount: how much pitch-shifted signal is fed back");
    tip (advElastic.slider,   "Elastic: the delay follows how fast the performer is playing, fast = tighter, slow = wider (0.5x to 2x)");
    tip (advNudge.slider,     "Nudge: how far each vocal word is pulled toward the 1/16 grid (0 = measure only, max 30 ms). Adds 40 ms latency while above 0");
    tip (advSmooth.slider,    "Smooth: how gently the delay time glides when changed");
    tip (advDuckRel.slider,   "Duck release: how fast the echoes return after playing stops");
    tip (advGateDepth.slider, "Gate depth: how far the gate closes");
    tip (advPanDepth.slider,  "Pan depth: how far auto pan swings");
    tip (advModRate.slider,   "Mod rate: speed of Wider and Richer");
    tip (advRepeats.slider,   "Repeats: stop after exactly this many echoes (Off = normal feedback)");
    tip (advFbMode.slider,    "Feedback mode: normal, inverted or ping pong");
    tip (advCheap,   "Cheap: 11-bit vintage digital delay sound");
    tip (advMono,    "Mono: sum the output to mono");
    tip (advPocket,  "Pocket: the readout shows how early or late each vocal word lands on the beat (needs the host playing)");
    tip (advDestiny, "Destiny: drifting, unpredictable delay time");

    applyTimeMode (audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f);
    refreshButtons();

    // browser overlay (added last of the front panel so it covers it)
    content.addChildComponent (browser);
    browser.onClose = [this]() { toggleBrowser(); };
    browser.onChanged = [this]() { refreshButtons(); };
    browser.onSave = [this]() { showSaveDialog(); };
    browser.onDelete = [this] (juce::File file)
    {
        juce::Component::SafePointer<DelayAudioProcessorEditor> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete preset",
                                            "Delete \"" + file.getFileNameWithoutExtension() + "\"?", "Delete", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe, file] (int r)
                                            {
                                                if (r == 1 && safe != nullptr)
                                                {
                                                    safe->audioProcessor.deleteUserPreset (file);
                                                    safe->browser.refresh();
                                                    safe->refreshButtons();
                                                }
                                            }));
    };
    for (auto* b : { &syncBtn, &pingBtn, &revBtn, &shimBtn, &bbdBtn, &killBtn, &bypassBtn, &apanBtn, &advBtn, &abBtn, &presetPrev, &presetNext })
        b->setTitle (b->getButtonText());
    presetBtn.setTitle ("Presets");
    for (auto* l : allLabels) l->setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (true);

    applyUiSettings();
    timerCallback();   // show the real state from the first frame
    startTimerHz (30);
    {
        const int w = juce::jlimit (kWidth * 3 / 4, kWidth * 2, savedWidth);
        setSize (w, w * kHeight / kWidth);
    }
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DelayAudioProcessorEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text,
                                           std::function<juce::String (double)> textFn, juce::Component* parent)
{
    if (parent == nullptr)
        parent = &content;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    parent->addAndMakeVisible (k.slider);

    styleLabel (k.label, text);
    parent->addAndMakeVisible (k.label);
    allLabels.push_back (&k.label);

    k.attach = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, k.slider);
    k.slider.undo = &audioProcessor.undoManager;
    k.slider.setTitle (text);
    k.slider.useDefaultFromParameter (dynamic_cast<juce::RangedAudioParameter*> (audioProcessor.apvts.getParameter (paramId)));
    {
        static const std::map<juce::String, juce::String> units =
        {
            { "FEEDBACK", "%" }, { "TAPE", "%" }, { "DUCKING", "%" }, { "DRY", "%" }, { "WET", "%" }, { "SWIRL", "%" }, { "SAUCE", "%" },
            { "WIDER", "%" }, { "RICHER", "%" }, { "SHIMMER", "%" }, { "ATTACK", "%" }, { "SUSTAIN", "%" }, { "ELASTIC", "%" },
            { "TELEPATHY", "%" }, { "ECOLOGY", "%" }, { "POCKET_AMT", "%" }, { "GRAIN", "%" }, { "SPECTRAL", "%" }, { "GATE_DEPTH", "%" },
            { "AP_DEPTH", "%" }, { "HICUT", " Hz" }, { "LOCUT", " Hz" }
        };
        const auto it = units.find (paramId);
        if (it != units.end()) k.slider.unit = it->second;
    }
    if (textFn)   // choice parameters keep the text the attachment gives them
        k.slider.textFromValueFunction = std::move (textFn);   // after the attachment, which installs its own
}

void DelayAudioProcessorEditor::setParamValue (const juce::String& paramId, float value)
{
    if (auto* param = audioProcessor.apvts.getParameter (paramId))
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastUndoStepMs > 300.0) audioProcessor.newUndoStep();   // clicks that belong together share one undo step
        lastUndoStepMs = now;
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void DelayAudioProcessorEditor::momentary (juce::TextButton& b, const char* paramId)
{
    b.onStateChange = [this, &b, paramId]()
    {
        auto* param = audioProcessor.apvts.getParameter (paramId);
        const float want = b.isDown() ? 1.0f : 0.0f;
        if (param != nullptr && std::abs (param->getValue() - want) > 0.5f)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (want);
            param->endChangeGesture();
        }
    };
}

void DelayAudioProcessorEditor::setChoice (const juce::String& paramId, int index)
{
    setParamValue (paramId, static_cast<float> (index));
    refreshButtons();
}

// UI only - never writes a parameter, so host automation / preset recall stays in sync
void DelayAudioProcessorEditor::refreshButtons()
{
    const int feelIdx = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FEEL")->load() + 0.5f);
    feelBtn.setButtonText (feelIdx == 1 ? "DOTTED" : feelIdx == 2 ? "TRIPLET" : "STRAIGHT");

    const int fbMode = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FB_MODE")->load() + 0.5f);
    pingBtn.setToggleState (fbMode == 2, juce::dontSendNotification);

    revBtn.setToggleState (audioProcessor.apvts.getRawParameterValue ("REVERSE")->load() > 0.5f, juce::dontSendNotification);

    const int pitchIdx = juce::jlimit (0, kNumShimIntervals - 1,
                                       static_cast<int> (audioProcessor.apvts.getRawParameterValue ("SHIM_PITCH")->load() + 0.5f));
    const bool shimOn = audioProcessor.apvts.getRawParameterValue ("SHIMMER")->load() > 0.001f;
    shimBtn.setButtonText (shimOn ? juce::String (kShimNames[pitchIdx]) : juce::String ("SHIM"));
    shimBtn.setToggleState (shimOn, juce::dontSendNotification);

    bbdBtn.setToggleState (audioProcessor.apvts.getRawParameterValue ("BBD")->load() > 0.5f, juce::dontSendNotification);
    bypassBtn.setToggleState (audioProcessor.apvts.getRawParameterValue ("BYPASS")->load() > 0.5f, juce::dontSendNotification);

    const int apIdx = juce::jlimit (0, kNumAutoPan - 1, static_cast<int> (audioProcessor.apvts.getRawParameterValue ("AUTOPAN")->load() + 0.5f));
    apanBtn.setButtonText (apIdx > 0 ? juce::String (kAutoPanNames[apIdx]) : juce::String ("APAN"));
    apanBtn.getProperties().set ("led", true);
    apanBtn.setToggleState (apIdx > 0, juce::dontSendNotification);

    presetBtn.setButtonText (audioProcessor.getCurrentPresetName());
    abBtn.setButtonText (audioProcessor.getABSlot() == 0 ? "A" : "B");
    abBtn.setToggleState (audioProcessor.getABSlot() == 1, juce::dontSendNotification);
    hcBtn.setToggleState (audioProcessor.getHighContrast(), juce::dontSendNotification);
    bigTextBtn.setToggleState (audioProcessor.getLargeText(), juce::dontSendNotification);
    undoBtn.setEnabled (audioProcessor.undoManager.canUndo());
    redoBtn.setEnabled (audioProcessor.undoManager.canRedo());

    advCheap.setToggleState   (audioProcessor.apvts.getRawParameterValue ("CHEAP")->load()   > 0.5f, juce::dontSendNotification);
    advMono.setToggleState    (audioProcessor.apvts.getRawParameterValue ("MONO")->load()    > 0.5f, juce::dontSendNotification);
    advDestiny.setToggleState (audioProcessor.apvts.getRawParameterValue ("DESTINY")->load() > 0.5f, juce::dontSendNotification);
    advPocket.setToggleState  (audioProcessor.apvts.getRawParameterValue ("POCKET")->load()  > 0.5f, juce::dontSendNotification);
}

namespace
{
    juce::String relativePath (const juce::File& f)
    {
        return f.getRelativePathFrom (DelayAudioProcessor::getUserPresetDirectory()).replaceCharacter ('\\', '/');
    }
}

void DelayAudioProcessorEditor::stepPreset (int delta)
{
    // one list: the factory presets, then the user presets
    const int nFactory = audioProcessor.getNumPrograms();
    const auto files = audioProcessor.getUserPresetFiles();
    const int total = nFactory + files.size();

    int cur = audioProcessor.getCurrentProgram();
    const auto userPath = audioProcessor.getCurrentUserPresetPath();
    if (userPath.isNotEmpty())
        for (int j = 0; j < files.size(); ++j)
            if (relativePath (files[j]) == userPath)
                cur = nFactory + j;

    const int next = (cur + delta + total) % total;
    if (next < nFactory)
        audioProcessor.setCurrentProgram (next);
    else
        audioProcessor.loadUserPreset (files[next - nFactory]);
    refreshButtons();
}

void DelayAudioProcessorEditor::toggleBrowser()
{
    browserOpen = ! browserOpen;
    if (browserOpen)
    {
        setAdvPage (0);
        browser.refresh();
        browser.setVisible (true);
        browser.toFront (true);
        browser.grabKeyboardFocus();
    }
    else
    {
        browser.setVisible (false);
        grabKeyboardFocus();
    }
    refreshButtons();
}

void DelayAudioProcessorEditor::setAdvPage (int page)
{
    if (page > 0 && browserOpen) toggleBrowser();
    advPage = page;
    if (page > 0 && hintLabel.isVisible()) { hintLabel.setVisible (false);  audioProcessor.setHintSeen (true); }
    juce::Component* panels[4] = { &advPanel, &advPanel2, &advPanel3, &advPanel4 };
    for (int i = 0; i < 4; ++i)
    {
        panels[i]->setVisible (advPage == i + 1);
        if (advPage == i + 1) panels[i]->toFront (false);
    }
    advBtn.setButtonText (advPage == 0 ? "ADV" : "ADV " + juce::String (advPage));
    advBtn.setToggleState (advPage > 0, juce::dontSendNotification);
    advBtn.toFront (false);
    if (advPage >= 3) timerCallback();   // fill the new page straight away
}

void DelayAudioProcessorEditor::applyUiSettings()
{
    SkellUi::highContrast = audioProcessor.getHighContrast();
    SkellUi::largeText = audioProcessor.getLargeText();
    skellLookAndFeel.refreshColours();
    updateTextSizes();
    sendLookAndFeelChange();
    repaint();
}

// every label stays at least a readable size on screen (more with LARGE TEXT), however small the window is
void DelayAudioProcessorEditor::updateTextSizes()
{
    SkellUi::scale = static_cast<float> (getWidth()) / static_cast<float> (kWidth);
    for (auto* l : allLabels)
    {
        const bool note = l->getProperties().contains ("note") || l == &playInfo;
        const bool isInfo = l == &playInfo;
        const float base = isInfo ? 9.0f : (note ? 8.0f : 10.5f);
        auto f = juce::FontOptions (SkellUi::fit (base));
        if (! note || isInfo) f = f.withStyle ("Bold Italic");
        l->setFont (f);
        if (note) l->setMinimumHorizontalScale (1.0f);      // long notes wrap to a second line instead of squeezing
        if (note && ! isInfo) l->setColour (juce::Label::textColourId, SkellUi::note());
    }
    playInfo.setColour (juce::Label::textColourId, SkellUi::lime());
    for (auto& n : advNote) n.setColour (juce::Label::textColourId, SkellUi::note());
}

void DelayAudioProcessorEditor::doTap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (tapCount > 0 && now - tapTimes[tapCount - 1] > 2000.0) tapCount = 0;     // too long: start over
    if (tapCount == 6) { for (int i = 1; i < 6; ++i) tapTimes[i - 1] = tapTimes[i];  --tapCount; }
    tapTimes[tapCount++] = now;
    if (tapCount >= 2)
    {
        const double ms = (tapTimes[tapCount - 1] - tapTimes[0]) / (tapCount - 1);
        const float clamped = juce::jlimit (20.0f, 2000.0f, static_cast<float> (ms));
        if (audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f) setSync (false);
        setParamValue ("DELAY_TIME", clamped);
        tapInfo = "tapped " + juce::String (juce::roundToInt (clamped)) + " ms  (" + juce::String (juce::roundToInt (60000.0f / clamped)) + " bpm)";
    }
    else
        tapInfo = "keep tapping...";
}

void DelayAudioProcessorEditor::doLearn()
{
    const float ms = audioProcessor.getLearnedIntervalMs();
    if (ms <= 0.0f) { tapInfo = "nothing to learn from yet - play or sing";  return; }
    const float clamped = juce::jlimit (20.0f, 2000.0f, ms);
    if (audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f) setSync (false);
    setParamValue ("DELAY_TIME", clamped);
    tapInfo = "learned " + juce::String (juce::roundToInt (clamped)) + " ms from your playing";
}

bool DelayAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) audioProcessor.undoManager.redo();
        else                 audioProcessor.undoManager.undo();
        refreshButtons();
        return true;
    }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y')) { audioProcessor.undoManager.redo();  refreshButtons();  return true; }
    return false;
}

void DelayAudioProcessorEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name (use Category/Name to put it in a folder):",
                                     juce::MessageBoxIconType::NoIcon, this);
    auto suggestion = audioProcessor.getCurrentUserPresetPath().upToLastOccurrenceOf (".", false, false);
    if (suggestion.isEmpty())
        suggestion = audioProcessor.getCurrentPresetName();
    w->addTextEditor ("name", suggestion);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DelayAudioProcessorEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r == 1 && safe != nullptr)
        {
            safe->audioProcessor.saveUserPreset (w->getTextEditorContents ("name"));
            safe->browser.refresh();
            safe->refreshButtons();
        }
    }), true);
}

void DelayAudioProcessorEditor::applyTimeMode (bool sync)
{
    // Re-create the attachment so the knob takes the other parameter's range and value
    time.attach.reset();
    time.attach = std::make_unique<SliderAttachment> (audioProcessor.apvts,
                                                      sync ? "SYNC_DIV" : "DELAY_TIME", time.slider);
    timeIsSync = sync;
    time.slider.undo = &audioProcessor.undoManager;
    time.slider.useDefaultFromParameter (dynamic_cast<juce::RangedAudioParameter*> (audioProcessor.apvts.getParameter (sync ? "SYNC_DIV" : "DELAY_TIME")));
    time.slider.unit = "";

    if (sync)
    {
        auto* feelParam = audioProcessor.apvts.getRawParameterValue ("FEEL");
        time.slider.textFromValueFunction = [feelParam] (double v)
        {
            const int idx  = juce::jlimit (0, kNumSyncDivisions - 1, juce::roundToInt (v));
            const int feel = static_cast<int> (feelParam->load() + 0.5f);
            return juce::String (kSyncDivisions[idx].name) + (feel == 1 ? "." : feel == 2 ? "T" : "");
        };
    }
    else
    {
        time.slider.textFromValueFunction = milliseconds;
    }

    syncBtn.setToggleState (sync, juce::dontSendNotification);
    syncBtn.setButtonText (sync ? "SYNC" : "FREE");
    feelBtn.setVisible (sync);
    resized();   // the modulation control shares the bottom row with the feel button

    time.slider.updateText();
    time.slider.repaint();
}

void DelayAudioProcessorEditor::setSync (bool shouldSync)
{
    if (auto* param = audioProcessor.apvts.getParameter ("SYNC"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (shouldSync ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
    applyTimeMode (shouldSync);
}

void DelayAudioProcessorEditor::timerCallback()
{
    leftMeter.setLevel (audioProcessor.getLeftLevel());
    rightMeter.setLevel (audioProcessor.getRightLevel());

    // follow host automation / preset recall
    const bool syncParam = audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f;
    if (syncParam != timeIsSync)
        applyTimeMode (syncParam);
    refreshButtons();

    const int fbMode = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FB_MODE")->load() + 0.5f);
    auto pv = [this] (const char* id) { return audioProcessor.apvts.getRawParameterValue (id)->load(); };
    SkellDisplay::Extras extras;
    extras.tape = pv ("TAPE");
    extras.sauce = pv ("SAUCE");
    extras.shimmer = pv ("SHIMMER");
    extras.shimUp = pv ("SHIM_PITCH") < 1.5f;   // +12 / +7 climb, -12 / -7 fall
    extras.reverse = pv ("REVERSE") > 0.5f;
    extras.gatePattern = static_cast<int> (pv ("GATE") + 0.5f);
    extras.gateDepth = pv ("GATE_DEPTH");
    extras.gatePos = audioProcessor.getGatePosition();
    extras.autoPan = pv ("AUTOPAN") > 0.5f && pv ("AP_DEPTH") > 0.005f;
    extras.autoPanPos = audioProcessor.getAutoPanValue();
    extras.pocketOn = pv ("POCKET") > 0.5f;
    extras.pocketPlaying = audioProcessor.isHostPlaying();
    extras.pocketN = audioProcessor.getPocketCount();
    for (int k = 0; k < 8; ++k) extras.pocket[k] = audioProcessor.getPocketError (k);
    {
        const float d = audioProcessor.getEffectiveDelayMs();
        const float fbv = static_cast<float> (feedback.slider.getValue());
        const int rep = static_cast<int> (pv ("REPEATS") + 0.5f);
        extras.repeats = rep;
        if (rep > 0)              extras.decayMs = static_cast<float> (rep) * d;
        else if (fbv >= 0.995f)   extras.decayMs = -1.0f;
        else if (fbv <= 0.001f)   extras.decayMs = d;
        else                      extras.decayMs = d * 60.0f / (-20.0f * std::log10 (fbv));
    }
    delayDisplay.setParams (audioProcessor.getEffectiveDelayMs(),
                            static_cast<float> (feedback.slider.getValue()),
                            static_cast<float> (ducking.slider.getValue()),
                            fbMode, extras);
    modCtl.repaint();

    if (advPage == 4)
    {
        float lv[SkellTrail::N], tn[SkellTrail::N];
        for (int k = 0; k < SkellTrail::N; ++k)
        {
            lv[k] = audioProcessor.getTrailLevel (SkellTrail::N - 1 - k);
            tn[k] = audioProcessor.getTrailTone  (SkellTrail::N - 1 - k);
        }
        trail.setData (lv, tn, audioProcessor.getEffectiveDelayMs());
    }
    if (advPage == 2)
    {
        static const char* desc[5] = { "delay follows how fast you perform", "a ghost of the word just before the next", "every repeat lives and dies on its own",
                                       "pulls words toward the grid", "shows how each word lands" };
        const int map[5] = { 0, 1, -1, -1, 2 };     // note cell -> smart feature
        for (int n = 0; n < 5; ++n)
        {
            if (map[n] < 0) continue;
            const auto st = audioProcessor.getSmartStatus (map[n]);
            advNote[n].setText (st.text.isNotEmpty() ? st.text : juce::String (desc[n]), juce::dontSendNotification);
            advNote[n].setColour (juce::Label::textColourId, st.problem ? juce::Colour (0xffffb347) : (st.text.isNotEmpty() ? SkellUi::lime() : SkellUi::note()));
        }
    }
    if (advPage == 3)
    {
        const float ms = audioProcessor.getLearnedIntervalMs();
        playInfo.setText (tapInfo.isNotEmpty() ? tapInfo : (ms > 0.0f ? "heard: one note every " + juce::String (juce::roundToInt (ms)) + " ms" : "LEARN listens to how fast you play"),
                          juce::dontSendNotification);
    }
    if (browserOpen)
        browser.updateButtons();
}

void DelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The artwork PNG is ~75% transparent, so always paint an opaque base first;
    // otherwise the host's window colour shows through the middle.
    g.fillAll (juce::Colour (0xff050a04));

    if (bgImage.isValid())
        g.drawImage (bgImage, getLocalBounds().toFloat());
}

void DelayAudioProcessorEditor::placeKnob (Knob& k, int centreX, int topY, int size)
{
    k.slider.setBounds (centreX - size / 2, topY, size, size);
    k.label.setBounds  (centreX - 34, topY + size + 1, 68, 14);
}

void DelayAudioProcessorEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / static_cast<float> (kWidth)));
    if (getWidth() >= kWidth * 3 / 4) audioProcessor.setUiWidth (getWidth());
    updateTextSizes();

    // Level meters follow the artwork's border channel (see SkellMeter)
    leftMeter.setBounds  (8, 104, 40, 108);
    rightMeter.setBounds (672, 104, 40, 108);

    const int small = 44, big = 64;
    const int smallTop = 112, bigTop = 94;   // labels of both sizes share one baseline

    // left: PAN  TONE  TAPE  |  TIME        right: FEEDBACK  |  DUCKER  DRY  WET (mirrored about the centre)
    placeKnob (pan,     78,  smallTop, small);
    placeKnob (hiCut,   130, smallTop, small);
    placeKnob (tape,    182, smallTop, small);
    placeKnob (time,    252, bigTop,   big);

    placeKnob (feedback, 468, bigTop,   big);
    placeKnob (ducking,  538, smallTop, small);
    placeKnob (dry,      590, smallTop, small);
    placeKnob (wet,      642, smallTop, small);

    // centre column: preset, a compact readout (also the gate control), three rows of text switches
    const int cx = 302, cw = 120;
    presetPrev.setBounds (cx,            71, 18, 16);
    presetBtn.setBounds  (cx + 18,       71, cw - 36, 16);
    presetNext.setBounds (cx + cw - 18,  71, 18, 16);

    delayDisplay.setBounds (cx + 8, 91, cw - 16, 32);

    const int bw = cw / 4;
    syncBtn.setBounds (cx,          129, bw, 16);
    pingBtn.setBounds (cx + bw,     129, bw, 16);
    revBtn.setBounds  (cx + 2 * bw, 129, bw, 16);
    shimBtn.setBounds (cx + 3 * bw, 129, bw, 16);

    if (feelBtn.isVisible())   // tempo sync on: feel on the left, modulation on the right
    {
        feelBtn.setBounds (cx,      147, 58, 16);
        modCtl.setBounds  (cx + 62, 147, 58, 16);
    }
    else
    {
        modCtl.setBounds (cx + 31, 147, 58, 16);
    }

    bbdBtn.setBounds    (cx,       165, 30, 16);
    killBtn.setBounds   (cx + 30,  165, 30, 16);
    bypassBtn.setBounds (cx + 60,  165, 30, 16);
    apanBtn.setBounds   (cx + 90,  165, 30, 16);

    // advanced panels. Page 1 (sound): 8 columns x 2 rows of small knobs, the switches stacked in the last cell
    advBtn.setBounds (cx + cw + 6, 71, 38, 16);
    abBtn.setBounds (cx - 30, 71, 24, 16);
    browser.setBounds (40, 54, 640, 190);
    hintLabel.setBounds (110, 218, 500, 16);
    advPanel.setBounds (52, 90, 616, 114);
    advPanel2.setBounds (52, 90, 616, 114);
    advPanel3.setBounds (52, 90, 616, 114);
    advPanel4.setBounds (52, 90, 616, 114);
    {
        const int colW = 77, k = 38, r1 = 4, r2 = 58;
        auto col = [&] (int c) { return c * colW + colW / 2; };
        placeKnob (advLoCut,     col (0), r1, k);  placeKnob (advAttack,  col (1), r1, k);
        placeKnob (advSustain,   col (2), r1, k);  placeKnob (advSwirl,   col (3), r1, k);
        placeKnob (advSauce,     col (4), r1, k);  placeKnob (advWider,   col (5), r1, k);
        placeKnob (advRicher,    col (6), r1, k);  placeKnob (advShimmer, col (7), r1, k);
        placeKnob (advSmooth,    col (0), r2, k);  placeKnob (advDuckRel, col (1), r2, k);
        placeKnob (advGateDepth, col (2), r2, k);  placeKnob (advPanDepth, col (3), r2, k);
        placeKnob (advModRate,   col (4), r2, k);  placeKnob (advFbMode,  col (5), r2, k);
        placeKnob (advRepeats,   col (6), r2, k);
        advCheap.setBounds   (col (7) - 33, r2 + 6,  66, 14);
        advMono.setBounds    (col (7) - 33, r2 + 20, 66, 14);
        advDestiny.setBounds (col (7) - 33, r2 + 34, 66, 14);
    }
    // page 2 (smart): five big cells, each with a one-line description
    {
        const int colW = 616 / 5, k = 46, top = 8;
        auto col = [&] (int c) { return c * colW + colW / 2; };
        placeKnob (advElastic,   col (0), top, k);  placeKnob (advTelepathy, col (1), top, k);
        placeKnob (advEcology,   col (2), top, k);  placeKnob (advNudge,     col (3), top, k);
        for (int n = 0; n < 5; ++n)
            advNote[n].setBounds (col (n) - colW / 2 + 2, top + k + 17, colW - 4, 30);
        advPocket.setBounds (col (4) - 33, top + 12, 66, 16);
    }
    // page 3 (play): grain + spectral on the left, the performance buttons in the middle, tap / learn on the right
    {
        const int top = 8, k = 46;
        placeKnob (advGrain, 52, top, k);   placeKnob (advSpectral, 128, top, k);
        playNote[0].setBounds (52 - 40, top + k + 17, 80, 30);   playNote[1].setBounds (128 - 40, top + k + 17, 80, 30);
        const int bx = 190, bw = 94, bh = 34, gap = 6;
        stutterBtn.setBounds   (bx,                 top + 4, bw, bh);
        tapeStopBtn.setBounds  (bx + bw + gap,      top + 4, bw, bh);
        revThrowBtn.setBounds  (bx + 2 * (bw + gap), top + 4, bw, bh);
        playNote[2].setBounds (bx,                  top + 4 + bh + 3, bw, 30);
        playNote[3].setBounds (bx + bw + gap,       top + 4 + bh + 3, bw, 30);
        playNote[4].setBounds (bx + 2 * (bw + gap), top + 4 + bh + 3, bw, 30);
        tapBtn.setBounds   (bx + 3 * (bw + gap) - 6 + 2, top + 4,        56, bh);
        learnBtn.setBounds (bx + 3 * (bw + gap) + 58, top + 4,           56, bh);
        playInfo.setBounds (190, 8 + 4 + bh + 30, 418, 18);
    }
    // page 4 (view): echo trail, accessibility, undo / redo
    {
        trail.setBounds (8, 6, 410, 102);
        const int x = 430, w = 90;
        hcBtn.setBounds (x, 8, 2 * w + 8 - 4, 22);       bigTextBtn.setBounds (x, 34, 2 * w + 4, 22);
        undoBtn.setBounds (x, 60, w, 22);                  redoBtn.setBounds (x + w + 8, 60, w - 4, 22);
        resetSizeBtn.setBounds (x, 86, 2 * w + 4, 22);
    }
}
