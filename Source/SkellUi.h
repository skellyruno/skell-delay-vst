#pragma once
#include <JuceHeader.h>
#include <cmath>

// Interface-wide settings that the look-and-feel and the components read (accessibility: contrast, text size)
struct SkellUi
{
    static inline bool  highContrast = false;
    static inline bool  largeText    = false;
    static inline float scale        = 1.0f;     // current window scale (1 = 720 px wide)

    static float minTextPx() { return largeText ? 12.5f : 9.5f; }
    // a font size (in the 720-px layout) that still comes out at least minTextPx on screen, capped so layouts do not burst
    static float fit (float size) { return juce::jlimit (size, size * 1.5f, juce::jmax (size, minTextPx() / juce::jmax (0.5f, scale))); }

    static juce::Colour lime()      { return juce::Colour (0xff39ff14); }
    static juce::Colour dimText()   { return highContrast ? juce::Colour (0xff9bff86) : juce::Colour (0xff2a7a14); }
    static juce::Colour dimBorder() { return highContrast ? juce::Colour (0xff39ff14) : juce::Colour (0xff155e05); }
    static juce::Colour ring()      { return highContrast ? juce::Colour (0xff39ff14).withAlpha (0.6f) : juce::Colour (0xff184a08); }
    static juce::Colour midGreen()  { return highContrast ? juce::Colour (0xff8cff70) : juce::Colour (0xff2ccb0f); }
    static juce::Colour note()      { return highContrast ? juce::Colour (0xff9bff86) : juce::Colour (0xff2a7a14); }
};

// ---------------------------------------------------------------------------------------------
// Parsing what a person types into a knob ("250", "250 ms", "35%", "1.5k", "L30", "1/8", "dotted"...).
// Stepped knobs are matched against their displayed names; everything else is mapped linearly between what the
// knob displays at its minimum and at its maximum.
inline bool skellParseDisplay (const juce::String& raw, double& out)
{
    auto t = raw.trim().toLowerCase();
    if (t.isEmpty()) return false;
    double sign = 1.0;
    bool pan = false;
    if (t.startsWithChar ('l') && t.length() > 1 && juce::CharacterFunctions::isDigit (t[1])) { sign = -1.0; t = t.substring (1); pan = true; }
    else if (t.startsWithChar ('r') && t.length() > 1 && juce::CharacterFunctions::isDigit (t[1])) { t = t.substring (1); pan = true; }
    else if (t == "c") { out = 0.0; return true; }
    juce::ignoreUnused (pan);
    juce::String num;
    int i = 0;
    if (i < t.length() && (t[i] == '+' || t[i] == '-')) num += t[i++];
    bool digits = false;
    for (; i < t.length(); ++i)
    {
        const auto c = t[i];
        if (juce::CharacterFunctions::isDigit (c)) { num += c; digits = true; }
        else if (c == '.' || c == ',') num += '.';
        else break;
    }
    if (! digits) return false;
    double v = num.getDoubleValue();
    const auto rest = t.substring (i).trim();
    if (rest.startsWithChar ('k')) v *= 1000.0;
    out = v * sign;
    return true;
}

inline bool skellTypedToValue (juce::Slider& s, const juce::String& text, double& result)
{
    const auto t = text.trim().toLowerCase();
    if (t.isEmpty()) return false;
    const double lo = s.getMinimum(), hi = s.getMaximum(), step = s.getInterval();

    if (step > 0.0 && (hi - lo) / step <= 64.0)   // stepped: choices, note values, repeat counts
    {
        for (double v = lo; v <= hi + 1.0e-9; v += step)
        {
            const auto name = s.getTextFromValue (v).trim().toLowerCase();
            if (name == t) { result = v; return true; }
        }
        for (double v = lo; v <= hi + 1.0e-9; v += step)
            if (s.getTextFromValue (v).trim().toLowerCase().startsWith (t) && t.length() >= 2) { result = v; return true; }
    }

    double n = 0.0, d0 = 0.0, d1 = 0.0;
    if (! skellParseDisplay (t, n)) return false;
    if (! skellParseDisplay (s.getTextFromValue (lo), d0) || ! skellParseDisplay (s.getTextFromValue (hi), d1) || std::abs (d1 - d0) < 1.0e-9)
        return false;
    const double v = lo + (n - d0) / (d1 - d0) * (hi - lo);
    result = juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), v);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The slider every knob uses: double-click resets, Shift-drag is fine control (8x), right-click or Enter types an exact value,
// arrow keys step (Shift = fine), Delete resets, and the tooltip always shows the current value with its unit.
class SkellSlider : public juce::Slider
{
public:
    SkellSlider()
    {
        setWantsKeyboardFocus (true);
        setMouseDragSensitivity (kNormalSensitivity);
    }

    juce::UndoManager* undo = nullptr;
    juce::String unit;                 // appended to the value in the tooltip when the readout itself carries none
    double resetValue = 0.0;

    void useDefaultFromParameter (const juce::RangedAudioParameter* p)
    {
        if (p != nullptr) { resetValue = p->convertFrom0to1 (p->getDefaultValue());  setDoubleClickReturnValue (true, resetValue); }
    }

    juce::String getTooltip() override
    {
        auto desc = juce::SettableTooltipClient::getTooltip();
        auto v = getTextFromValue (getValue());
        if (unit.isNotEmpty() && ! v.endsWithIgnoreCase (unit.trim())) v += unit;
        return desc.isEmpty() ? v : desc + "\n" + v;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (undo != nullptr) undo->beginNewTransaction();
        setMouseDragSensitivity (e.mods.isShiftDown() ? kFineSensitivity : kNormalSensitivity);
        juce::Slider::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        juce::Slider::mouseUp (e);
        setMouseDragSensitivity (kNormalSensitivity);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const bool fine = k.getModifiers().isShiftDown();
        const double range = getMaximum() - getMinimum();
        double step = getInterval() > 0.0 ? getInterval() : range / 100.0;
        if (! fine && getInterval() <= 0.0) step = range / 20.0;
        const auto code = k.getKeyCode();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) { nudge (step); return true; }
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) { nudge (-step); return true; }
        if (code == juce::KeyPress::homeKey) { setNew (getMinimum()); return true; }
        if (code == juce::KeyPress::endKey)  { setNew (getMaximum()); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { setNew (resetValue); return true; }
        if (code == juce::KeyPress::returnKey) { showTypeDialog(); return true; }
        return false;
    }

    void focusGained (FocusChangeType cause) override { ring = cause == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { ring = false; repaint(); }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (ring && hasKeyboardFocus (false))
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawEllipse (getLocalBounds().toFloat().reduced (1.5f), 1.5f);
        }
    }

    void showTypeDialog()
    {
        auto* w = new juce::AlertWindow ("Type a value", getTitle().isNotEmpty() ? getTitle() : "Value", juce::MessageBoxIconType::NoIcon, getTopLevelComponent());
        w->addTextEditor ("v", getTextFromValue (getValue()));
        w->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SkellSlider> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (r == 1 && safe != nullptr)
            {
                double v = 0.0;
                if (skellTypedToValue (*safe, w->getTextEditorContents ("v"), v))
                    safe->setNew (v);
            }
        }), true);
    }

    static constexpr int kNormalSensitivity = 250, kFineSensitivity = 2000;

private:
    bool ring = false;
    void setNew (double v)
    {
        if (undo != nullptr) undo->beginNewTransaction();
        setValue (snapValue (v, notDragging), juce::sendNotificationSync);
    }
    void nudge (double d) { setNew (juce::jlimit (getMinimum(), getMaximum(), getValue() + d)); }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (getTitle().isNotEmpty() ? getTitle() + "  " + getTextFromValue (getValue()) : getTextFromValue (getValue()));
        m.addItem (1, "Type a value...");
        m.addItem (2, "Reset to default");
        juce::Component::SafePointer<SkellSlider> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->showTypeDialog();
            if (r == 2) safe->setNew (safe->resetValue);
        });
    }
};
