#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    bgImage = juce::ImageFileFormat::loadFrom (BinaryData::skell_bg_png, BinaryData::skell_bg_pngSize);
    setLookAndFeel (&skellLookAndFeel);

    addAndMakeVisible (leftMeter);
    addAndMakeVisible (rightMeter);
    addAndMakeVisible (delayDisplay);

    // ---- knobs: (knob, parameter id, label, readout shown while hovering / dragging)
    setupKnob (feedback, "FEEDBACK", "FEEDBACK", percent);
    setupKnob (hiCut,   "HICUT",   "TONE",   hertz);
    setupKnob (tape,    "TAPE",    "TAPE",   percent);
    setupKnob (ducking, "DUCKING", "DUCKER", percent);

    setupKnob (dry,     "DRY",     "DRY",    percent);
    setupKnob (wet,     "WET",     "WET",    percent);

    // TIME: its attachment (ms vs note value) is chosen by applyTimeMode()
    time.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    time.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (time.slider);
    styleLabel (time.label, "TIME");
    addAndMakeVisible (time.label);

    // ---- switches
    syncBtn.setClickingTogglesState (true);
    syncBtn.onClick = [this]() { setSync (syncBtn.getToggleState()); };
    addAndMakeVisible (syncBtn);

    pingBtn.setClickingTogglesState (true);
    pingBtn.onClick = [this]() { setChoice ("FB_MODE", pingBtn.getToggleState() ? 2 : 0); };
    addAndMakeVisible (pingBtn);

    revBtn.setClickingTogglesState (true);
    revBtn.onClick = [this]() { setChoice ("REVERSE", revBtn.getToggleState() ? 1 : 0); };
    addAndMakeVisible (revBtn);

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
    addAndMakeVisible (shimBtn);

    // presets: click the name to open the menu
    for (auto* b : { &presetPrev, &presetBtn, &presetNext })
    {
        b->getProperties().set ("flat", true);
        b->getProperties().set ("big", true);
        addAndMakeVisible (b);
    }
    presetBtn.onClick  = [this]() { showPresetMenu(); };
    presetPrev.onClick = [this]() { stepPreset (-1); };
    presetNext.onClick = [this]() { stepPreset (+1); };
    addAndMakeVisible (modCtl);

    // feel: one button that cycles straight -> dotted -> triplet; only shown while tempo sync is on
    feelBtn.onClick = [this]()
    {
        const int cur = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FEEL")->load() + 0.5f);
        setChoice ("FEEL", (cur + 1) % 3);
    };
    feelBtn.getProperties().set ("flat", true);
    addChildComponent (feelBtn);
    for (auto* b : { &syncBtn, &pingBtn, &revBtn })
        b->getProperties().set ("flat", true);
    shimBtn.getProperties().set ("flat", true);
    shimBtn.getProperties().set ("led", true);

    applyTimeMode (audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f);
    refreshButtons();

    timerCallback();   // show the real state from the first frame
    startTimerHz (30);
    setSize (kWidth, kHeight);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DelayAudioProcessorEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text,
                                           std::function<juce::String (double)> textFn)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (k.slider);

    styleLabel (k.label, text);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, k.slider);
    k.slider.textFromValueFunction = std::move (textFn);   // after the attachment, which installs its own
}

void DelayAudioProcessorEditor::setParamValue (const juce::String& paramId, float value)
{
    if (auto* param = audioProcessor.apvts.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
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

    presetBtn.setButtonText (audioProcessor.getProgramName (audioProcessor.getCurrentProgram()));
}

void DelayAudioProcessorEditor::stepPreset (int delta)
{
    const int n = audioProcessor.getNumPrograms();
    audioProcessor.setCurrentProgram ((audioProcessor.getCurrentProgram() + delta + n) % n);
    refreshButtons();
}

void DelayAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&skellLookAndFeel);
    const int current = audioProcessor.getCurrentProgram();
    for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        menu.addItem (i + 1, audioProcessor.getProgramName (i), true, i == current);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn).withMinimumWidth (presetBtn.getWidth()),
                        [this] (int result)
                        {
                            if (result > 0)
                            {
                                audioProcessor.setCurrentProgram (result - 1);
                                refreshButtons();
                            }
                        });
}

void DelayAudioProcessorEditor::applyTimeMode (bool sync)
{
    // Re-create the attachment so the knob takes the other parameter's range and value
    time.attach.reset();
    time.attach = std::make_unique<SliderAttachment> (audioProcessor.apvts,
                                                      sync ? "SYNC_DIV" : "DELAY_TIME", time.slider);
    timeIsSync = sync;

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
    delayDisplay.setParams (audioProcessor.getEffectiveDelayMs(),
                            static_cast<float> (feedback.slider.getValue()),
                            static_cast<float> (ducking.slider.getValue()),
                            fbMode, extras);
    modCtl.repaint();
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
    // Level meters follow the artwork's border channel (see SkellMeter)
    leftMeter.setBounds  (8, 104, 40, 108);
    rightMeter.setBounds (672, 104, 40, 108);

    const int small = 44, big = 64;
    const int smallTop = 112, bigTop = 94;   // labels of both sizes share one baseline

    // left: TONE  TAPE  |  TIME        right: FEEDBACK  |  DUCKER  DRY  WET
    placeKnob (hiCut,   96,  smallTop, small);
    placeKnob (tape,    164, smallTop, small);
    placeKnob (time,    244, bigTop,   big);

    placeKnob (feedback, 468, bigTop,   big);
    placeKnob (ducking,  538, smallTop, small);
    placeKnob (dry,      590, smallTop, small);
    placeKnob (wet,      642, smallTop, small);

    // centre column: preset, a compact readout, two quiet rows of text switches
    const int cx = 302, cw = 120;
    presetPrev.setBounds (cx,            76, 18, 16);
    presetBtn.setBounds  (cx + 18,       76, cw - 36, 16);
    presetNext.setBounds (cx + cw - 18,  76, 18, 16);

    delayDisplay.setBounds (cx + 8, 98, cw - 16, 32);

    const int bw = cw / 4;
    syncBtn.setBounds (cx,          138, bw, 16);
    pingBtn.setBounds (cx + bw,     138, bw, 16);
    revBtn.setBounds  (cx + 2 * bw, 138, bw, 16);
    shimBtn.setBounds (cx + 3 * bw, 138, bw, 16);

    if (feelBtn.isVisible())   // tempo sync on: feel on the left, modulation on the right
    {
        feelBtn.setBounds (cx,      158, 58, 16);
        modCtl.setBounds  (cx + 62, 158, 58, 16);
    }
    else
    {
        modCtl.setBounds (cx + 31, 158, 58, 16);
    }
}
