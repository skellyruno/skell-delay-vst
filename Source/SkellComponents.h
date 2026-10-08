#pragma once
#include <JuceHeader.h>

class SkellMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        level = juce::jlimit (0.0f, 1.0f, newLevel);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a120a));
        g.fillRoundedRectangle (bounds, 2.0f);

        const int numSegments = 18;
        const float gap = 1.5f;
        const float segHeight = (bounds.getHeight() - (gap * (numSegments + 1))) / numSegments;

        const int litSegments = static_cast<int>(level * numSegments);

        for (int i = 0; i < numSegments; ++i)
        {
            float yPos = bounds.getBottom() - ((i + 1) * (segHeight + gap));
            bool isLit = i < litSegments;

            juce::Colour ledColour = (i > 15) ? juce::Colour (0xffff3333) : juce::Colour (0xff00ff66);
            g.setColour (isLit ? ledColour : ledColour.withAlpha (0.15f));
            g.fillRect (bounds.getX() + 1.5f, yPos, bounds.getWidth() - 3.0f, segHeight);
        }
    }

private:
    float level = 0.0f;
};

class SkellDisplay : public juce::Component
{
public:
    void setText (const juce::String& newText)
    {
        displayText = newText;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff050805));
        g.fillRoundedRectangle (bounds, 3.5f);
        g.setColour (juce::Colour (0xff00ff66).withAlpha (0.4f));
        g.drawRoundedRectangle (bounds, 3.5f, 1.25f);

        g.setColour (juce::Colour (0xff00ff66));
        g.setFont (juce::FontOptions (17.0f).withStyle ("Bold Italic"));
        g.drawText (displayText, bounds, juce::Justification::centred, true);
    }

private:
    juce::String displayText = "1/8T";
};
