#pragma once
#include <JuceHeader.h>

// Level meter that follows the artwork's border channel: a vertical rail that flares inward at the
// bottom, where the frame curves toward the skull. Boundary points were measured from the artwork
// (window coordinates for the left edge; the right meter is the mirror image).
class SkellMeter : public juce::Component
{
public:
    explicit SkellMeter (bool isRightSide = false) : right (isRightSide)
    {
        setInterceptsMouseClicks (false, false);
    }

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
        constexpr float yTop = 108.0f, yBottom = 205.0f;
        constexpr int numSegments = 18;
        constexpr float gap = 1.3f;
        const float segH = ((yBottom - yTop) - gap * (numSegments - 1)) / numSegments;
        const int lit = static_cast<int> (level * numSegments);

        // whole rail, dark
        juce::Path rail;
        buildRail (rail, yTop - 1.0f, yBottom + 1.0f);
        g.setColour (juce::Colour (0xff070d07));
        g.fillPath (rail);

        for (int i = 0; i < numSegments; ++i)
        {
            const float yb = yBottom - static_cast<float> (i) * (segH + gap);
            juce::Path seg;
            buildQuad (seg, yb - segH, yb, 1.0f);
            const bool red = i >= 16;
            const juce::Colour c = red ? juce::Colour (0xffff3333) : juce::Colour (0xff39ff14);
            g.setColour (i < lit ? c : c.withAlpha (0.14f));
            g.fillPath (seg);
        }
    }

private:
    struct Row { float y, xl, xr; };

    // channel between the outer and inner border lines (left side, window coordinates)
    static const Row* rows (int& n)
    {
        static const Row table[] = {
            { 106.0f, 12.1f, 26.4f }, { 110.0f, 12.1f, 24.6f }, { 113.0f, 12.1f, 22.3f }, { 116.0f, 12.1f, 20.6f },
            { 122.0f, 12.1f, 20.2f }, { 143.0f, 12.1f, 20.2f }, { 146.0f, 12.1f, 21.9f }, { 149.0f, 12.1f, 22.9f },
            { 188.0f, 12.1f, 22.9f }, { 191.0f, 12.1f, 24.3f }, { 194.0f, 12.8f, 26.3f }, { 197.0f, 14.8f, 28.3f },
            { 200.0f, 17.5f, 30.4f }, { 203.0f, 19.9f, 31.4f }, { 206.0f, 21.6f, 36.8f }, { 209.0f, 22.5f, 40.1f }
        };
        n = static_cast<int> (sizeof (table) / sizeof (table[0]));
        return table;
    }

    void edgesAt (float y, float& xl, float& xr) const
    {
        int n = 0;
        const Row* t = rows (n);
        y = juce::jlimit (t[0].y, t[n - 1].y, y);
        int k = 0;
        while (k < n - 2 && y > t[k + 1].y) ++k;
        const float f = (y - t[k].y) / (t[k + 1].y - t[k].y);
        xl = t[k].xl + f * (t[k + 1].xl - t[k].xl);
        xr = t[k].xr + f * (t[k + 1].xr - t[k].xr);
    }

    // the whole channel: follows every measured point, not just the two ends
    void buildRail (juce::Path& p, float y0, float y1) const
    {
        auto toLocal = [this] (float x, float y)
        {
            const float ax = right ? static_cast<float> (getParentWidth()) - x : x;
            return juce::Point<float> (ax - static_cast<float> (getX()), y - static_cast<float> (getY()));
        };
        int n = 0;
        const Row* t = rows (n);
        float l, r;
        edgesAt (y0, l, r);
        p.startNewSubPath (toLocal (l, y0));
        for (int i = 0; i < n; ++i)
            if (t[i].y > y0 && t[i].y < y1)
                p.lineTo (toLocal (t[i].xl, t[i].y));
        edgesAt (y1, l, r);
        p.lineTo (toLocal (l, y1));
        p.lineTo (toLocal (r, y1));
        for (int i = n - 1; i >= 0; --i)
            if (t[i].y > y0 && t[i].y < y1)
                p.lineTo (toLocal (t[i].xr, t[i].y));
        edgesAt (y0, l, r);
        p.lineTo (toLocal (r, y0));
        p.closeSubPath();
    }

    // quad between y0 and y1 following the channel edges, inset by `inset` pixels
    void buildQuad (juce::Path& p, float y0, float y1, float inset) const
    {
        float l0, r0, l1, r1;
        edgesAt (y0, l0, r0);
        edgesAt (y1, l1, r1);
        l0 += inset; l1 += inset; r0 -= inset; r1 -= inset;

        auto toLocal = [this] (float x, float y)
        {
            const float ax = right ? static_cast<float> (getParentWidth()) - x : x;
            return juce::Point<float> (ax - static_cast<float> (getX()), y - static_cast<float> (getY()));
        };
        p.startNewSubPath (toLocal (l0, y0));
        p.lineTo (toLocal (r0, y0));
        p.lineTo (toLocal (r1, y1));
        p.lineTo (toLocal (l1, y1));
        p.closeSubPath();
    }

    bool right;
    float level = 0.0f;
};

// --- Compact echo visualiser ---------------------------------------------------
// Bars = the repeat train, decaying with FEEDBACK (ducking trims repeats after the first).
// A pulse sweeps across the taps at the
// real delay time so you can see the tempo of the echoes.
//   Normal / Inverted : mirrored bars about the centre line
//   Ping Pong         : repeats alternate up (L) / down (R)
class SkellDisplay : public juce::Component, private juce::Timer
{
public:
    SkellDisplay() { startTimerHz (30); }
    ~SkellDisplay() override { stopTimer(); }

    // Extra things the repeats show: tape wobble, reverb tail, reversed swell, shimmer markers
    struct Extras { float tape = 0.0f, sauce = 0.0f, shimmer = 0.0f; bool reverse = false, shimUp = true; };

    void setParams (float timeMs, float feedback, float ducking, int newFbMode, const Extras& newExtras)
    {
        timeValueMs = juce::jmax (10.0f, timeMs);
        feedbackVal = feedback;
        duckingVal = ducking;
        fbMode = newFbMode;
        extras = newExtras;
    }

    void paint (juce::Graphics& g) override
    {
        const auto lime = juce::Colour (0xff39ff14);
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (juce::Colour (0xff040a03));
        g.fillRoundedRectangle (bounds, 5.0f);

        auto area = bounds.reduced (6.0f, 4.0f);
        const float midY = area.getCentreY();
        const float halfH = area.getHeight() * 0.5f;

        // faint grid: centre line + vertical tick marks
        g.setColour (lime.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());

        const int numTaps = 12;
        const float barW = 5.0f;
        const float step = (area.getWidth() - barW) / (numTaps - 1);
        const float fb = juce::jlimit (0.05f, 1.0f, feedbackVal);
        const float duckFactor = 1.0f - duckingVal * 0.4f;

        // sweeping pulse, one tap per delay time
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double phase = std::fmod (nowMs / timeValueMs, static_cast<double>(numTaps));
        const int activeTap = static_cast<int>(phase);
        const float pulse = 1.0f - static_cast<float>(phase - activeTap);   // 1 -> 0 after each hit

        for (int i = 0; i < numTaps; ++i)
        {
            const float tapeFade = 1.0f - 0.4f * extras.tape * static_cast<float> (i) / static_cast<float> (numTaps);
            const float level = std::pow (fb, static_cast<float>(i)) * (i > 0 ? duckFactor : 1.0f) * tapeFade;
            const float h = juce::jmax (1.5f, halfH * level);

            // tape: the repeats drift slightly in time, more with every pass
            const float wobble = extras.tape * 1.3f * std::sin (static_cast<float> (nowMs * 0.004) + static_cast<float> (i) * 1.3f)
                                 * (0.3f + 0.7f * static_cast<float> (i) / static_cast<float> (numTaps));
            const float x = area.getX() + i * step + wobble;

            float top, bottom;
            const bool ping = fbMode == 2;
            const bool up = (i % 2 == 0);
            if (ping)
            {
                top    = up ? midY - h : midY;
                bottom = up ? midY     : midY + h;
            }
            else
            {
                top = midY - h;  bottom = midY + h;
            }

            const float boost = (i == activeTap) ? pulse : 0.0f;
            const auto barColour = lime.withAlpha (juce::jlimit (0.0f, 1.0f, 0.22f + 0.55f * level + 0.35f * boost));

            // sauce: a reverb tail smeared behind each repeat
            if (extras.sauce > 0.01f)
            {
                const float tailW = step * (0.4f + 1.4f * extras.sauce);
                juce::ColourGradient tail (lime.withAlpha (0.22f * extras.sauce * (0.4f + level)), x + barW, midY,
                                           lime.withAlpha (0.0f), x + barW + tailW, midY, false);
                g.setGradientFill (tail);
                const float inset = h * 0.25f;
                g.fillRect (x + barW, top + (ping ? 0.0f : inset), tailW, (bottom - top) - (ping ? 0.0f : 2.0f * inset));
            }

            g.setColour (barColour);
            if (extras.reverse)   // reversed repeats swell up into the hit instead of starting at full level
            {
                const float lowH = h * 0.12f;
                juce::Path ramp;
                const float t0 = ping ? (up ? midY - lowH : midY) : midY - lowH;
                const float b0 = ping ? (up ? midY : midY + lowH) : midY + lowH;
                ramp.startNewSubPath (x, t0);
                ramp.lineTo (x + barW, top);
                ramp.lineTo (x + barW, bottom);
                ramp.lineTo (x, b0);
                ramp.closeSubPath();
                g.fillPath (ramp);
            }
            else
            {
                g.fillRoundedRectangle (x, top, barW, bottom - top, 1.0f);
            }

            // shimmer: tiny chevrons climbing (or sinking) with every repeat
            if (extras.shimmer > 0.01f && i > 0)
            {
                const float climb = static_cast<float> (i) * 1.2f * extras.shimmer;
                g.setColour (lime.withAlpha (juce::jlimit (0.0f, 0.95f, 0.25f + 0.35f * extras.shimmer * level + 0.2f * boost)));
                juce::Path chev;
                const float cx2 = x + barW * 0.5f;
                if (extras.shimUp)
                {
                    const float ty = top - 3.0f - climb;
                    chev.startNewSubPath (cx2 - 2.0f, ty + 2.0f);  chev.lineTo (cx2, ty);  chev.lineTo (cx2 + 2.0f, ty + 2.0f);
                }
                else
                {
                    const float by = bottom + 3.0f + climb;
                    chev.startNewSubPath (cx2 - 2.0f, by - 2.0f);  chev.lineTo (cx2, by);  chev.lineTo (cx2 + 2.0f, by - 2.0f);
                }
                g.strokePath (chev, juce::PathStrokeType (1.0f));
            }

            if (boost > 0.05f)   // hot cap + glow on the active tap
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f * boost));
                g.fillRect (x, (ping && i % 2 == 1) ? bottom - 1.5f : top, barW, 1.5f);
                g.setColour (lime.withAlpha (0.18f * boost));
                g.fillRoundedRectangle (x - 2.0f, top - 2.0f, barW + 4.0f, bottom - top + 4.0f, 2.0f);
            }
        }

        // single hairline frame
        g.setColour (lime.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

private:
    void timerCallback() override { repaint(); }

    Extras extras;
    float timeValueMs = 250.0f;
    float feedbackVal = 0.5f;
    float duckingVal = 0.0f;
    int fbMode = 0;
};


// --- Modulation control for Wider / Richer ------------------------------------------------
// Click = cycle the LFO shape (sine / triangle / random). Drag up/down or scroll = rate (0.1x - 8x).
// The icon shows the shape with a dot running at the real speed. It is dimmed while Wider and Richer are both 0.
class SkellModControl : public juce::Component
{
public:
    explicit SkellModControl (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    void paint (juce::Graphics& g) override
    {
        const auto lime = juce::Colour (0xff39ff14);
        const float rate = value ("MOD_RATE");
        const int shape = juce::jlimit (0, 2, static_cast<int> (value ("MOD_SHAPE") + 0.5f));
        const bool active = value ("WIDER") > 0.001f || value ("RICHER") > 0.001f;
        const float alpha = (active ? 1.0f : 0.45f) * (isMouseOver() ? 1.0f : 0.8f);

        auto b = getLocalBounds().toFloat();
        if (isMouseOver())
        {
            g.setColour (lime.withAlpha (0.08f));
            g.fillRoundedRectangle (b.reduced (1.0f), 4.0f);
        }

        // waveform icon
        auto icon = juce::Rectangle<float> (b.getX() + 6.0f, b.getCentreY() - 3.5f, 18.0f, 7.0f);
        juce::Path wave;
        const int n = 24;
        for (int i = 0; i <= n; ++i)
        {
            const float t = static_cast<float> (i) / n;
            const float v = shapeAt (shape, t);
            const juce::Point<float> p (icon.getX() + t * icon.getWidth(), icon.getCentreY() - v * icon.getHeight() * 0.5f);
            if (i == 0) wave.startNewSubPath (p); else wave.lineTo (p);
        }
        g.setColour (lime.withAlpha (0.8f * alpha));
        g.strokePath (wave, juce::PathStrokeType (1.0f));

        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float ph = static_cast<float> (std::fmod (now * 0.5 * rate, 1.0));
        g.setColour (juce::Colours::white.withAlpha (0.9f * alpha));
        g.fillEllipse (icon.getX() + ph * icon.getWidth() - 1.5f, icon.getCentreY() - shapeAt (shape, ph) * icon.getHeight() * 0.5f - 1.5f, 3.0f, 3.0f);

        g.setColour (lime.withAlpha (alpha));
        g.setFont (juce::FontOptions (8.5f).withStyle ("Bold"));
        g.drawText (juce::String (rate, rate < 10.0f ? 2 : 1) + "x", b.withTrimmedLeft (26.0f).toNearestInt(), juce::Justification::centredLeft, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        startY = e.y;
        startRate = value ("MOD_RATE");
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = startY - e.y;
        if (! dragged && std::abs (dy) < 3)
            return;
        if (! dragged)
        {
            dragged = true;
            if (auto* p = apvts.getParameter ("MOD_RATE")) p->beginChangeGesture();
        }
        setRate (startRate * std::pow (2.0f, static_cast<float> (dy) / 40.0f));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragged)
        {
            if (auto* p = apvts.getParameter ("MOD_RATE")) p->endChangeGesture();
        }
        else if (auto* p = apvts.getParameter ("MOD_SHAPE"))
        {
            const int next = (static_cast<int> (value ("MOD_SHAPE") + 0.5f) + 1) % 3;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next)));
            p->endChangeGesture();
        }
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        setRate (value ("MOD_RATE") * std::pow (2.0f, w.deltaY * 2.0f));
    }

private:
    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    void setRate (float r)
    {
        if (auto* p = apvts.getParameter ("MOD_RATE"))
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.1f, 8.0f, r)));
        repaint();
    }

    static float shapeAt (int shape, float t)
    {
        if (shape == 0) return std::sin (juce::MathConstants<float>::twoPi * t);
        if (shape == 1) return t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
        // random: a few steps with glides
        static const float steps[] = { 0.2f, 0.9f, -0.6f, 0.4f, -0.9f, 0.1f };
        const float pos = t * 5.0f;
        const int k = juce::jlimit (0, 4, static_cast<int> (pos));
        const float f = pos - static_cast<float> (k);
        const float s = f * f * (3.0f - 2.0f * f);
        return steps[k] + s * (steps[k + 1] - steps[k]);
    }

    juce::AudioProcessorValueTreeState& apvts;
    bool dragged = false;
    int startY = 0;
    float startRate = 1.0f;
};
