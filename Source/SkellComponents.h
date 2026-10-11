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
            juce::Colour ledColour = (i > 15) ? juce::Colour (0xffff3333) : juce::Colour (0xff39ff14);
            g.setColour (isLit ? ledColour : ledColour.withAlpha (0.15f));
            g.fillRect (bounds.getX() + 1.5f, yPos, bounds.getWidth() - 3.0f, segHeight);
        }
    }

private:
    float level = 0.0f;
};

// --- Pure Tap Visualizer Screen (Time Text Removed) ---
class SkellDisplay : public juce::Component
{
public:
    void setParams (float feedback, float ducking)
    {
        feedbackVal = feedback;
        duckingVal = ducking;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Dark LCD Frame
        g.setColour (juce::Colour (0xff050c03));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (0xff39ff14).withAlpha (0.4f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.25f);

        // Animated Delay Tap Echo Bars
        auto visualArea = bounds.reduced (6.0f);
        const int numTaps = 8;
        const float barWidth = 5.0f;
        const float spacing = (visualArea.getWidth() - (numTaps * barWidth)) / (numTaps - 1);

        for (int i = 0; i < numTaps; ++i)
        {
            float decayFactor = std::pow (juce::jlimit (0.1f, 0.95f, feedbackVal), static_cast<float>(i));
            float duckFactor = 1.0f - (duckingVal * 0.5f);
            float heightPercent = decayFactor * duckFactor;

            float barHeight = visualArea.getHeight() * 0.75f * heightPercent;
            float xPos = visualArea.getX() + i * (barWidth + spacing);
            float yPos = visualArea.getBottom() - barHeight - 3.0f;

            g.setColour (juce::Colour (0xff39ff14).withAlpha (0.25f + (0.75f * decayFactor)));
            g.fillRoundedRectangle (xPos, yPos, barWidth, barHeight, 1.5f);
        }
    }

private:
    float feedbackVal = 0.5f;
    float duckingVal = 0.0f;
};
