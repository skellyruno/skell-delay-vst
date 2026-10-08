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
        setColour (juce::ToggleButton::textColourId, juce::Colour (0xff00ff66));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff00ff66));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        auto radius = (float) juce::jmin (width, height) / 2.0f - 4.0f;
        auto centreX = (float) x + (float) width  * 0.5f;
        auto centreY = (float) y + (float) height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Dark metallic body fill
        g.setColour (juce::Colour (0xff121612));
        g.fillEllipse (rx, ry, rw, rw);

        // Outer rim outline
        g.setColour (juce::Colour (0xff2d3b2d));
        g.drawEllipse (rx, ry, rw, rw, 2.0f);

        // Inner bevel
        g.setColour (juce::Colour (0xff00ff66).withAlpha (0.3f));
        g.drawEllipse (rx + 3, ry + 3, rw - 6, rw - 6, 1.0f);

        // Pointer / Green notch line
        juce::Path p;
        auto pointerLength = radius * 0.7f;
        auto pointerThickness = 3.0f;
        p.addRectangle (-pointerThickness * 0.5f, -radius + 4.0f, pointerThickness, pointerLength);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));

        g.setColour (juce::Colour (0xff00ff66)); // Neon Green
        g.fillPath (p);
    }
};