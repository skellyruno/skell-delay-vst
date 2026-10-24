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
    setupKnob (shimmer, "SHIMMER", "SHIM",   percent);

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

    shimPitchBtn.onClick = [this]()
    {
        const int cur = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("SHIM_PITCH")->load() + 0.5f);
        setChoice ("SHIM_PITCH", (cur + 1) % kNumShimIntervals);
    };
    addAndMakeVisible (shimPitchBtn);

    // A / B compare
    juce::TextButton* ab[2] = { &abA, &abB };
    for (int i = 0; i < 2; ++i)
    {
        ab[i]->setRadioGroupId (1002);
        ab[i]->setClickingTogglesState (true);
        ab[i]->setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        ab[i]->onClick = [this, i]() { audioProcessor.selectABSlot (i); refreshButtons(); };
        addAndMakeVisible (ab[i]);
    }

    // presets: [<] name [>]
    presetPrev.setConnectedEdges (juce::Button::ConnectedOnRight);
    presetBtn.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    presetNext.setConnectedEdges (juce::Button::ConnectedOnLeft);
    presetPrev.onClick = [this]() { stepPreset (-1); };
    presetNext.onClick = [this]() { stepPreset (+1); };
    presetBtn.onClick  = [this]() { showPresetMenu(); };
    addAndMakeVisible (presetPrev);
    addAndMakeVisible (presetBtn);
    addAndMakeVisible (presetNext);

    // straight / dotted / triplet - only shown while tempo sync is on
    juce::TextButton* feel[3] = { &feelStraightBtn, &feelDottedBtn, &feelTripletBtn };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = feel[i];
        b->setRadioGroupId (1001);
        b->setClickingTogglesState (true);
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < 2) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges (edges);
        b->onClick = [this, i]() { setChoice ("FEEL", i); };
        addChildComponent (b);
    }

    applyTimeMode (audioProcessor.apvts.getRawParameterValue ("SYNC")->load() > 0.5f);
    refreshButtons();

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

void DelayAudioProcessorEditor::setChoice (const juce::String& paramId, int index)
{
    if (auto* param = audioProcessor.apvts.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
        param->endChangeGesture();
    }
    refreshButtons();
}

// UI only - never writes a parameter, so host automation / preset recall stays in sync
void DelayAudioProcessorEditor::refreshButtons()
{
    const int feelIdx = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FEEL")->load() + 0.5f);
    feelStraightBtn.setToggleState (feelIdx == 0, juce::dontSendNotification);
    feelDottedBtn.setToggleState   (feelIdx == 1, juce::dontSendNotification);
    feelTripletBtn.setToggleState  (feelIdx == 2, juce::dontSendNotification);

    const int fbMode = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("FB_MODE")->load() + 0.5f);
    pingBtn.setToggleState (fbMode == 2, juce::dontSendNotification);

    revBtn.setToggleState (audioProcessor.apvts.getRawParameterValue ("REVERSE")->load() > 0.5f, juce::dontSendNotification);

    const int pitchIdx = juce::jlimit (0, kNumShimIntervals - 1,
                                       static_cast<int> (audioProcessor.apvts.getRawParameterValue ("SHIM_PITCH")->load() + 0.5f));
    shimPitchBtn.setButtonText (kShimNames[pitchIdx]);

    abA.setToggleState (audioProcessor.getABSlot() == 0, juce::dontSendNotification);
    abB.setToggleState (audioProcessor.getABSlot() == 1, juce::dontSendNotification);

    presetBtn.setButtonText (audioProcessor.getProgramName (audioProcessor.getCurrentProgram()));
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

void DelayAudioProcessorEditor::stepPreset (int delta)
{
    const int n = audioProcessor.getNumPrograms();
    audioProcessor.setCurrentProgram ((audioProcessor.getCurrentProgram() + delta + n) % n);
    refreshButtons();
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
    feelStraightBtn.setVisible (sync);
    feelDottedBtn.setVisible (sync);
    feelTripletBtn.setVisible (sync);

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
    delayDisplay.setParams (audioProcessor.getEffectiveDelayMs(),
                            static_cast<float> (feedback.slider.getValue()),
                            static_cast<float> (ducking.slider.getValue()),
                            fbMode);
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
    // Level meters on the frame edges
    leftMeter.setBounds  (11, 47, 9, 144);
    rightMeter.setBounds (700, 47, 9, 144);

    const int small = 42, big = 62;
    const int smallTop = 113, bigTop = 96;   // labels of both sizes share one baseline

    // left: PAN  TONE  TAPE  SHIM  |  TIME
    placeKnob (pan,     62,  smallTop, small);
    placeKnob (hiCut,   108, smallTop, small);
    placeKnob (tape,    154, smallTop, small);
    placeKnob (shimmer, 200, smallTop, small);
    shimPitchBtn.setBounds (200 - 16, 95, 32, 14);
    placeKnob (time,    258, bigTop,   big);

    // centre: preset bar, readout, switches
    const int cx = 302, cw = 120;
    presetPrev.setBounds (cx,            64, 16, 16);
    presetBtn.setBounds  (cx + 16,       64, cw - 32, 16);
    presetNext.setBounds (cx + cw - 16,  64, 16, 16);

    delayDisplay.setBounds (cx, 84, cw, 32);
    syncBtn.setBounds (cx,           120, 58, 16);
    pingBtn.setBounds (cx + 60,      120, 60, 16);
    feelStraightBtn.setBounds (cx,                 139, cw / 3, 16);
    feelDottedBtn.setBounds   (cx + cw / 3,        139, cw / 3, 16);
    feelTripletBtn.setBounds  (cx + 2 * (cw / 3),  139, cw - 2 * (cw / 3), 16);

    revBtn.setBounds (cx,        158, 62, 16);
    abA.setBounds    (cx + 64,   158, 28, 16);
    abB.setBounds    (cx + 92,   158, 28, 16);

    // right: FEEDBACK  |  DUCKER  DRY  WET
    placeKnob (feedback, 486, bigTop,   big);
    placeKnob (ducking,  552, smallTop, small);
    placeKnob (dry,      604, smallTop, small);
    placeKnob (wet,      656, smallTop, small);
}
