#pragma once

#include <JuceHeader.h>
#include <array>

// Spectral-flux onset detector. A short FFT every 128 samples; an onset is a peak in the positive change of
// the (log-compressed) spectrum that stands clearly above its own recent history. It reacts to the shape of the
// sound, not just its loudness, so soft consonant starts and onsets on top of a noisy background are found, while
// steady loud passages are not re-triggered.
class FluxOnsetDetector
{
public:
    static constexpr int N = 512, H = 128, kHist = 96;

    FluxOnsetDetector() : fft (9)
    {
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));
        prepare (48000.0);
    }

    void prepare (double sampleRate)
    {
        const double bin = sampleRate / (double) N;
        kMin = juce::jmax (2, (int) std::ceil (150.0 / bin));
        kMax = juce::jlimit (kMin + 4, N / 2, (int) (6000.0 / bin));
        refractory = (int) (0.06 * sampleRate);
        reset();
    }

    void reset()
    {
        ring.fill (0.0f);  prev.fill (0.0f);  hist.fill (0.0f);
        wr = 0;  sinceHop = 0;  histPos = 0;  frames = 0;  f1 = f2 = 0.0f;  sinceOnset = 1000000000;
    }

    // flags[i] = 1 on the sample at which an onset has been decided (a few ms after it actually happened, see kLagMs)
    void process (const float* x, int n, unsigned char* flags)
    {
        for (int i = 0; i < n; ++i)
        {
            flags[i] = 0;
            ring[(size_t) wr] = x[i];
            wr = (wr + 1) & (N - 1);
            if (sinceOnset < 1000000000) ++sinceOnset;
            if (++sinceHop >= H)
            {
                sinceHop = 0;
                if (frame())
                    flags[i] = 1;
            }
        }
    }

    // measured delay between the true start of a sound and the flag (see the onset test)
    static constexpr float kLagMs = 8.0f;

private:
    bool frame()
    {
        float e = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float v = ring[(size_t) ((wr + i) & (N - 1))];   // oldest first
            e += v * v;
            data[(size_t) i] = v * win[(size_t) i];
        }
        std::fill (data.begin() + N, data.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (data.data());

        float flux = 0.0f;
        for (int k = kMin; k <= kMax; ++k)
        {
            const float m = data[(size_t) k] * (4.0f / (float) N);       // about the amplitude of a sine in this bin
            const float lm = std::log2 (1.0f + 200.0f * m);
            const float d = lm - prev[(size_t) k];
            if (d > 0.0f) flux += d;
            prev[(size_t) k] = lm;
        }

        float mean = 0.0f;
        for (float v : hist) mean += v;
        mean /= (float) kHist;
        float var = 0.0f;
        for (float v : hist) var += (v - mean) * (v - mean);
        const float sd = std::sqrt (var / (float) kHist);
        const float thr = std::max (mean + 3.5f * sd, 1.3f * mean) + 2.0f;      // adapts to a noisy bed: its flux wobbles, so the bar moves up
        hist[(size_t) histPos] = flux;
        histPos = (histPos + 1) % kHist;
        if (frames < 1000000) ++frames;

        const float rms = std::sqrt (e / (float) N);
        bool onset = frames >= 24 && f1 > f2 && f1 >= flux && f1 > thr && rms > 0.004f && sinceOnset > refractory;
        if (onset) sinceOnset = 0;
        f2 = f1;  f1 = flux;
        return onset;
    }

    juce::dsp::FFT fft;
    std::array<float, N> win {};
    std::array<float, N> ring {};
    std::array<float, 2 * N> data {};
    std::array<float, N / 2 + 1> prev {};
    std::array<float, kHist> hist {};
    int frames = 0, wr = 0, sinceHop = 0, histPos = 0, kMin = 2, kMax = 100, refractory = 2000, sinceOnset = 1000000000;
    float f1 = 0.0f, f2 = 0.0f;
};
