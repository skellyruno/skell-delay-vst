#pragma once
#include <JuceHeader.h>
#include "SkellUi.h"

class SkellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SkellLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff39ff14));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xff39ff14));
        setColour (juce::Label::textColourId, juce::Colour (0xff39ff14));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff39ff14));

        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff081204));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xff39ff14));
        setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff39ff14).withAlpha (0.4f));

        // preset menu
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff081204));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xff39ff14));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff39ff14));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff040a02));
        setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xff2ccb0f));
    }

    // call after SkellUi settings (contrast, text size) change
    void refreshColours()
    {
        setColour (juce::PopupMenu::headerTextColourId, SkellUi::midGreen());
        setColour (juce::TooltipWindow::outlineColourId, SkellUi::lime().withAlpha (SkellUi::highContrast ? 0.9f : 0.4f));
        setColour (juce::TooltipWindow::backgroundColourId, SkellUi::highContrast ? juce::Colours::black : juce::Colour (0xff081204));
    }

    // Tooltips: first line = what it is, second line = the current value, larger and brighter
    juce::TextLayout tipLayout (const juce::String& tip, float maxW)
    {
        const float size = SkellUi::largeText ? 15.0f : 13.0f;
        juce::AttributedString a;
        a.setJustification (juce::Justification::topLeft);
        a.setWordWrap (juce::AttributedString::byWord);
        const auto lines = juce::StringArray::fromLines (tip);
        for (int i = 0; i < lines.size(); ++i)
        {
            const bool value = lines.size() > 1 && i == lines.size() - 1;
            a.append (lines[i] + (i + 1 < lines.size() ? "\n" : ""), juce::FontOptions (value ? size + 1.0f : size).withStyle (value ? "Bold" : "Regular"),
                      value ? juce::Colours::white : findColour (juce::TooltipWindow::textColourId));
        }
        juce::TextLayout l;
        l.createLayoutWithBalancedLineLengths (a, maxW);
        return l;
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area) override
    {
        const auto l = tipLayout (tip, 300.0f);
        const int w = static_cast<int> (std::ceil (l.getWidth())) + 18, h = static_cast<int> (std::ceil (l.getHeight())) + 12;
        return juce::Rectangle<int> (pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                     pos.y > area.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (area);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        auto b = juce::Rectangle<float> (0, 0, (float) width, (float) height);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
        tipLayout (text, 300.0f).draw (g, b.reduced (9.0f, 6.0f));
    }


    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto radius = juce::jmin (width, height) / 2.0f - 4.0f;
        const auto centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;

        const bool isInteracting = slider.isMouseOverOrDragging();
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // 1. Dark Background Track
        juce::Path bgTrack;
        bgTrack.addCentredArc (centreX, centreY, radius - 2.0f, radius - 2.0f, 0.0f,
                               rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff081404));
        g.strokePath (bgTrack, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 2. Active Lime Green Arc
        const bool isBipolar = (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0);
        const float zeroAngle = isBipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

        if (std::abs (angle - zeroAngle) > 0.001f)
        {
            juce::Path activeTrack;
            activeTrack.addCentredArc (centreX, centreY, radius - 2.0f, radius - 2.0f, 0.0f,
                                       juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);

            juce::Colour limeColor = isInteracting ? juce::Colour (0xff66ff33) : juce::Colour (0xff39ff14);
            g.setColour (limeColor);
            g.strokePath (activeTrack, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (isInteracting)
            {
                g.setColour (limeColor.withAlpha (0.45f));
                g.strokePath (activeTrack, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // 3. Inner Cap Body
        const auto capRadius = radius - 6.0f;
        const auto capX = centreX - capRadius;
        const auto capY = centreY - capRadius;
        const auto capW = capRadius * 2.0f;

        g.setColour (isInteracting ? juce::Colour (0xff102408) : juce::Colour (0xff081204));
        g.fillEllipse (capX, capY, capW, capW);

        // Rim Border
        g.setColour (isInteracting ? juce::Colour (0xff39ff14) : SkellUi::ring());
        g.drawEllipse (capX, capY, capW, capW, isInteracting ? 1.5f : 1.0f);

        if (isInteracting)
        {
            g.setColour (juce::Colour (0xff39ff14).withAlpha (0.25f));
            g.drawEllipse (capX - 1.5f, capY - 1.5f, capW + 3.0f, capW + 3.0f, 1.0f);
        }

        // 4. Indicator Needle
        juce::Path p;
        p.addRectangle (-1.25f, -capRadius + 2.0f, 2.5f, capRadius * 0.38f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));

        g.setColour (isInteracting ? juce::Colour (0xffffffff) : juce::Colour (0xff39ff14));
        g.fillPath (p);

        // 5. Centered Value Readout (HOVER / TURN ONLY)
        if (isInteracting)
        {
            juce::String valText = slider.getTextFromValue (slider.getValue());
            float fontSize = SkellUi::fit (juce::jlimit (9.0f, 13.0f, capRadius * 0.65f));

            g.setFont (juce::FontOptions (fontSize).withStyle ("Bold"));
            g.setColour (juce::Colour (0xffffffff));

            juce::Rectangle<int> textBounds (juce::roundToInt (capX),
                                             juce::roundToInt (capY),
                                             juce::roundToInt (capW),
                                             juce::roundToInt (capW));
            g.drawText (valText, textBounds, juce::Justification::centred, false);
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        auto bounds = button.getLocalBounds().toFloat();
        auto boxSize = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto boxRect = juce::Rectangle<float> ((bounds.getWidth() - boxSize) * 0.5f,
                                               (bounds.getHeight() - boxSize) * 0.5f,
                                               boxSize, boxSize).reduced (1.0f);

        const bool isChecked = button.getToggleState();

        g.setColour (juce::Colour (0xff050c03));
        g.fillRoundedRectangle (boxRect, 3.0f);

        g.setColour (isChecked ? juce::Colour (0xff39ff14) : SkellUi::dimBorder());
        g.drawRoundedRectangle (boxRect, 3.0f, 1.5f);

        if (isChecked)
        {
            auto fillRect = boxRect.reduced (3.0f);
            g.setColour (juce::Colour (0xff39ff14));
            g.fillRoundedRectangle (fillRect, 2.0f);

            g.setColour (juce::Colour (0xff39ff14).withAlpha (0.35f));
            g.drawRoundedRectangle (boxRect.expanded (1.5f), 4.0f, 1.0f);
        }
    }

    // Segmented control: buttons joined via setConnectedEdges share one outline
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (backgroundColour, shouldDrawButtonAsDown);

        if (button.getProperties().contains ("flat"))   // text-only switches: no box at all
        {
            if (shouldDrawButtonAsHighlighted)
            {
                g.setColour (juce::Colour (0xff39ff14).withAlpha (0.08f));
                g.fillRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 4.0f);
            }
            return;
        }

        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool isActive = button.getToggleState();
        const int edges = button.getConnectedEdgeFlags();
        const bool l = (edges & juce::Button::ConnectedOnLeft)  == 0;
        const bool r = (edges & juce::Button::ConnectedOnRight) == 0;

        juce::Path shape;
        shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                   4.0f, 4.0f, l, r, l, r);

        if (isActive)
            g.setColour (juce::Colour (0xff39ff14));
        else
            g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (0xff12280a) : juce::Colour (0xff091404));
        g.fillPath (shape);

        g.setColour (juce::Colour (0xff39ff14).withAlpha (0.3f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsDown);

        const bool isActive = button.getToggleState();

        if (button.getProperties().contains ("flat"))
        {
            const bool isToggle = button.getClickingTogglesState() || button.getProperties().contains ("led");
            const auto lime = juce::Colour (0xff39ff14);
            const bool bright = isActive || ! isToggle;
            auto c = bright ? lime : SkellUi::dimText();
            if (shouldDrawButtonAsHighlighted) c = c.brighter (0.25f);

            const float size = SkellUi::fit (button.getProperties().contains ("big") ? 10.0f : 8.5f);
            g.setColour (c);
            g.setFont (juce::FontOptions (size).withStyle ("Bold"));
            auto area = button.getLocalBounds();
            g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 1, 0.6f);

            if (isToggle && isActive)   // small glowing bar under the text
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (juce::FontOptions (size).withStyle ("Bold"), button.getButtonText(), 0.0f, 0.0f);
                const auto w = juce::jmin (static_cast<float> (area.getWidth()) - 6.0f,
                                           ga.getBoundingBox (0, -1, true).getWidth() + 4.0f);
                const auto bar = juce::Rectangle<float> (area.getCentreX() - w * 0.5f, static_cast<float> (area.getBottom()) - 3.0f, w, 1.5f);
                g.setColour (lime.withAlpha (0.35f));
                g.fillRoundedRectangle (bar.expanded (0.0f, 1.5f), 1.5f);
                g.setColour (lime);
                g.fillRoundedRectangle (bar, 0.75f);
            }
            return;
        }

        g.setColour (isActive ? juce::Colour (0xff040a02) : SkellUi::midGreen());
        g.setFont (juce::FontOptions (SkellUi::fit (8.0f)).withStyle ("Bold"));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1, 0.6f);
    }
};
