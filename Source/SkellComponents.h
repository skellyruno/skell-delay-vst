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

// --- Minimal readout: note value, exact ms, and a tap-decay strip ---
class SkellDisplay : public juce::Component
{
public:
    void setText (const juce::String& newText)
    {
        if (newText != fractionText) { fractionText = newText; repaint(); }
    }

    void setParams (float timeMs, float feedback, float ducking)
    {
        if (timeMs == timeValueMs && feedback == feedbackVal && ducking == duckingVal)
            return;
        timeValueMs = timeMs;
        feedbackVal = feedback;
        duckingVal = ducking;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto lime = juce::Colour (0xff39ff14);
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        // Flat dark inset with a single hairline - no glow, no bevel
        g.setColour (juce::Colour (0xff050b04));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (lime.withAlpha (0.22f));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        auto inner = bounds.reduced (8.0f, 4.0f);

        // Note value
        g.setColour (lime);
        g.setFont (juce::FontOptions (19.0f).withStyle ("Bold Italic"));
        g.drawText (fractionText, inner.removeFromTop (22.0f), juce::Justification::centred, false);

        // Exact time, quiet
        g.setColour (lime.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
        g.drawText (juce::String (timeValueMs, 1) + " ms", inner.removeFromTop (12.0f),
                    juce::Justification::centred, false);

        // Tap-decay strip: thin ticks, evenly spaced, own row (never behind text)
        inner.removeFromTop (3.0f);
        const int numTaps = 10;
        const float tickW = 2.0f;
        const float step = (inner.getWidth() - tickW) / (numTaps - 1);
        const float fb = juce::jlimit (0.05f, 0.95f, feedbackVal);
        const float duckFactor = 1.0f - (duckingVal * 0.4f);

        for (int i = 0; i < numTaps; ++i)
        {
            const float level = std::pow (fb, static_cast<float>(i)) * (i > 0 ? duckFactor : 1.0f);
            const float h = juce::jmax (1.0f, inner.getHeight() * level);
            g.setColour (lime.withAlpha (0.2f + 0.6f * level));
            g.fillRect (inner.getX() + i * step, inner.getBottom() - h, tickW, h);
        }
    }

private:
    juce::String fractionText = "1/8";
    float timeValueMs = 250.0f;
    float feedbackVal = 0.5f;
    float duckingVal = 0.0f;
};
