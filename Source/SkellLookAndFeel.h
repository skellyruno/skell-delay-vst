#pragma once
#include <JuceHeader.h>

class SkellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SkellLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff00ff66));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xff00ff66));
        setColour (juce::Label::textColourId, juce::Colour (0xff00ff66));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff00ff66));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        juce::ignoreUnused (slider);

        auto radius = (float) juce::jmin (width, height) / 2.0f - 2.5f;
        auto centreX = (float) x + (float) width  * 0.5f;
        auto centreY = (float) y + (float) height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        g.setColour (juce::Colour (0xff101510));
        g.fillEllipse (rx, ry, rw, rw);

        g.setColour (juce::Colour (0xff1e291e));
        g.drawEllipse (rx, ry, rw, rw, 1.25f);

        g.setColour (juce::Colour (0xff00ff66).withAlpha (0.25f));
        g.drawEllipse (rx + 1.5f, ry + 1.5f, rw - 3.0f, rw - 3.0f, 1.0f);

        juce::Path p;
        p.addRectangle (-1.0f, -radius + 2.5f, 2.0f, radius * 0.65f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));

        g.setColour (juce::Colour (0xff00ff66));
        g.fillPath (p);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (backgroundColour, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const bool isActive = button.getToggleState();

        if (isActive)
        {
            g.setColour (juce::Colour (0xff00ff66));
            g.fillRoundedRectangle (bounds, 2.5f);

            g.setColour (juce::Colour (0xff00ff66).withAlpha (0.5f));
            g.drawRoundedRectangle (bounds.expanded (1.0f), 3.0f, 1.25f);
        }
        else
        {
            g.setColour (juce::Colour (0xff0d120d));
            g.fillRoundedRectangle (bounds, 2.5f);

            g.setColour (juce::Colour (0xff004411));
            g.drawRoundedRectangle (bounds, 2.5f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        const bool isActive = button.getToggleState();

        g.setColour (isActive ? juce::Colour (0xff050805) : juce::Colour (0xff00aa44));
        g.setFont (juce::FontOptions (8.5f).withStyle ("Bold"));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
    }
};
