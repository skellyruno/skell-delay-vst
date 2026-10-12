#pragma once
#include <JuceHeader.h>

class SkellMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        // Peak (linear) -> dB scale (-48..0 dB) with instant attack and smooth fall
        const float db = juce::Decibels::gainToDecibels (newLevel, -48.0f);
        const float target = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        level = (target > level) ? target : level * 0.88f;
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
            juce::Colour ledColour = (i >= 16) ? juce::Colour (0xffff3333) : juce::Colour (0xff39ff14);
            g.setColour (isLit ? ledColour : ledColour.withAlpha (0.15f));
            g.fillRect (bounds.getX() + 1.5f, yPos, bounds.getWidth() - 3.0f, segHeight);
        }
    }

private:
    float level = 0.0f;
};

// --- Echo visualiser: the repeat train, decaying with feedback ---
// Normal: mirrored bars about a centre line.  Ping-pong: repeats alternate
// up (left) / down (right).  Ducking trims everything after the first tap.
class SkellDisplay : public juce::Component
{
public:
    void setParams (float timeMs, float feedback, float ducking, bool pingPong)
    {
        juce::ignoreUnused (timeMs);
        if (feedback == feedbackVal && ducking == duckingVal && pingPong == isPingPong)
            return;
        feedbackVal = feedback;
        duckingVal = ducking;
        isPingPong = pingPong;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto lime = juce::Colour (0xff39ff14);
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (juce::Colour (0xff050b04));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (lime.withAlpha (0.22f));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        auto area = bounds.reduced (9.0f, 6.0f);
        const float midY = area.getCentreY();
        const float halfH = area.getHeight() * 0.5f;

        g.setColour (lime.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());

        const int numTaps = 12;
        const float barW = 4.0f;
        const float step = (area.getWidth() - barW) / (numTaps - 1);
        const float fb = juce::jlimit (0.05f, 0.95f, feedbackVal);
        const float duckFactor = 1.0f - duckingVal * 0.4f;

        for (int i = 0; i < numTaps; ++i)
        {
            const float level = std::pow (fb, static_cast<float>(i)) * (i > 0 ? duckFactor : 1.0f);
            const float h = juce::jmax (1.0f, halfH * level);
            const float x = area.getX() + i * step;

            g.setColour (lime.withAlpha (0.25f + 0.65f * level));

            if (isPingPong)
            {
                if (i % 2 == 0) g.fillRoundedRectangle (x, midY - h, barW, h, 1.0f);   // left
                else            g.fillRoundedRectangle (x, midY,     barW, h, 1.0f);   // right
            }
            else
            {
                g.fillRoundedRectangle (x, midY - h, barW, h * 2.0f, 1.5f);
            }
        }
    }

private:
    float feedbackVal = 0.5f;
    float duckingVal = 0.0f;
    bool isPingPong = false;
};
