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

    // MIX: custom slider writing both DRY and WET
    mixSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixSlider.setRange (0.0, 1.0, 0.001);
    mixSlider.setDoubleClickReturnValue (true, 0.5);
    mixSlider.textFromValueFunction = percent;
    mixSlider.onDragStart = [this]()
    {
        mixDragging = true;
        for (auto* id : { "DRY", "WET" })
            if (auto* p = audioProcessor.apvts.getParameter (id)) p->beginChangeGesture();
    };
    mixSlider.onDragEnd = [this]()
    {
        for (auto* id : { "DRY", "WET" })
            if (auto* p = audioProcessor.apvts.getParameter (id)) p->endChangeGesture();
        mixDragging = false;
    };
    mixSlider.onValueChange = [this]() { applyMix (mixSlider.getValue()); };
    addAndMakeVisible (mixSlider);
    styleLabel (mixLabel, "MIX");
    addAndMakeVisible (mixLabel);
    syncMixFromParams();

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

void DelayAudioProcessorEditor::applyMix (double mix)
{
    const float wetV = static_cast<float> (juce::jmin (1.0, mix * 2.0));
    const float dryV = static_cast<float> (juce::jmin (1.0, (1.0 - mix) * 2.0));
    if (auto* d = audioProcessor.apvts.getParameter ("DRY")) d->setValueNotifyingHost (d->convertTo0to1 (dryV));
    if (auto* w = audioProcessor.apvts.getParameter ("WET")) w->setValueNotifyingHost (w->convertTo0to1 (wetV));
}

void DelayAudioProcessorEditor::syncMixFromParams()
{
    const double d = audioProcessor.apvts.getRawParameterValue ("DRY")->load();
    const double w = audioProcessor.apvts.getRawParameterValue ("WET")->load();
    double m;
    if (d >= w)  m = d > 0.0001 ? 0.5 * w / d : 0.0;         // dry full, echoes coming in
    else         m = w > 0.0001 ? 1.0 - 0.5 * d / w : 1.0;   // echoes full, dry going out
    mixSlider.setValue (juce::jlimit (0.0, 1.0, m), juce::dontSendNotification);
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
    const bool shimOn = audioProcessor.apvts.getRawParameterValue ("SHIMMER")->load() > 0.001f;
    shimBtn.setButtonText (shimOn ? juce::String (kShimNames[pitchIdx]) : juce::String ("SHIM"));
    shimBtn.setToggleState (shimOn, juce::dontSendNotification);

    if (! mixDragging)
        syncMixFromParams();

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
    // Level meters follow the artwork's border channel (see SkellMeter)
    leftMeter.setBounds  (8, 104, 40, 108);
    rightMeter.setBounds (672, 104, 40, 108);

    const int small = 46, big = 64;
    const int smallTop = 111, bigTop = 94;   // labels of both sizes share one baseline

    // left: TONE  TAPE  |  TIME        right: FEEDBACK  |  DUCKER  MIX   (mirrored about the centre)
    placeKnob (hiCut,   96,  smallTop, small);
    placeKnob (tape,    164, smallTop, small);
    placeKnob (time,    244, bigTop,   big);

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

    revBtn.setBounds  (cx,       158, 38, 16);
    shimBtn.setBounds (cx + 40,  158, 40, 16);
    abA.setBounds     (cx + 82,  158, 19, 16);
    abB.setBounds     (cx + 101, 158, 19, 16);

    placeKnob (feedback, 476, bigTop,   big);
    placeKnob (ducking,  552, smallTop, small);

    mixSlider.setBounds (624 - small / 2, smallTop, small, small);
    mixLabel.setBounds  (624 - 34, smallTop + small + 1, 68, 14);
}
