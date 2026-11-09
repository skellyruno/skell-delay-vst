#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Presets.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#ifdef SKELL_HARNESS   // only the hand-made harness (no CMake / BinaryData); the CMake test targets link the real ones
namespace BinaryData { const char* skell_bg_png = "x"; }
#endif
#ifdef SKELL_HARNESS
namespace juce { extern const char* const juce_compilationDate; extern const char* const juce_compilationTime;
                 const char* const juce_compilationDate = __DATE__; const char* const juce_compilationTime = __TIME__; }
#endif

static int fails = 0;
#define CHECK(c, m) do { bool ok_ = (c); std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", m); if (!ok_) ++fails; } while (0)

struct PH : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true; bool hasBpm = true, hasPpq = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; if (hasBpm) p.setBpm (bpm); if (hasPpq) p.setPpqPosition (ppq); p.setIsPlaying (playing); return p;
    }
};

struct Stats { bool finite = true; float peak = 0.0f; double secs = 0.0; double audioSecs = 0.0; };

static void setAll (DelayAudioProcessor& p, juce::Random& r, bool extreme)
{
    for (auto* prm : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            float v = r.nextFloat();
            if (extreme) { const int k = r.nextInt (3); v = k == 0 ? 0.0f : (k == 1 ? 1.0f : v); }
            rp->setValueNotifyingHost (v);
        }
}

// generator kinds
static float gen (int kind, juce::Random& r, double t, double sr, int& state)
{
    switch (kind)
    {
        case 0: return (r.nextFloat() * 2.0f - 1.0f);                                   // full-scale noise
        case 1: return std::sin (2 * 3.14159265358979 * 440.0 * t) > 0 ? 1.0f : -1.0f;   // full-scale square
        case 2: return 0.9f;                                                            // DC
        case 3: return (state++ & 1) ? 1.0f : -1.0f;                                    // Nyquist
        case 4: return (std::fmod (t, 0.5) < 1.0 / sr) ? 1.0f : 0.0f;                   // impulses
        case 5: return 1.0e-38f * (r.nextFloat() - 0.5f);                               // denormal-range input
        case 6: return 0.0f;                                                            // silence
        case 7: return 8.0f * std::sin (2 * 3.14159265358979 * 100.0 * t);              // +18 dBFS hot signal
        default: return 3.0e3f * (r.nextFloat() * 2.0f - 1.0f);                         // absurd (but < guard) levels
    }
}

static Stats run (DelayAudioProcessor& p, double sr, double seconds, int kind, juce::Random& r,
                  int blockMin, int blockMax, int slamEvery, PH* ph, bool extremeSlam)
{
    Stats s; s.audioSecs = seconds;
    juce::MidiBuffer midi; int state = 0; double t = 0.0; long done = 0; const long total = (long) (seconds * sr);
    int sinceSlam = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (done < total)
    {
        const int len = (int) std::min<long> (total - done, blockMin + r.nextInt (blockMax - blockMin + 1));
        juce::AudioBuffer<float> b (2, len);
        for (int i = 0; i < len; ++i) { const float v = gen (kind, r, t + i / sr, sr, state); b.setSample (0, i, v); b.setSample (1, i, kind == 0 ? gen (kind, r, t, sr, state) : v); }
        if (ph) ph->ppq += len / sr * ph->bpm / 60.0;
        if (slamEvery > 0 && (sinceSlam += len) >= slamEvery) { setAll (p, r, extremeSlam); sinceSlam = 0; }
        p.processBlock (b, midi);
        for (int c = 0; c < 2; ++c)
        {
            auto* d = b.getReadPointer (c);
            for (int i = 0; i < len; ++i) { if (! std::isfinite (d[i])) s.finite = false; s.peak = std::max (s.peak, std::abs (d[i])); }
        }
        done += len; t += len / sr;
    }
    s.secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return s;
}

static DelayAudioProcessor* make (double sr, int bs)
{
    auto* p = new DelayAudioProcessor(); p->setRateAndBufferSizeDetails (sr, bs); p->prepareToPlay (sr, bs); return p;
}
static void setp (DelayAudioProcessor& p, const char* id, float v)
{
    auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)); q->setValueNotifyingHost (q->convertTo0to1 (v));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Random r (99);

    std::printf("A) Everything maxed, feedback 100%%, 9 hostile signals x 20 s each @48k (wet only: dry passes through by design)\n");
    {
        double worst = 0; bool fin = true; double rt = 0;
        for (int kind = 0; kind < 9; ++kind)
        {
            std::unique_ptr<DelayAudioProcessor> p (make (48000, 512));
            for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (1.0f);
            setp (*p, "BYPASS", 0.0f); setp (*p, "DUCKING", 0.0f); setp (*p, "FEEDBACK", 1.0f); setp (*p, "DRY", 0.0f); setp (*p, "WET", 1.0f); setp (*p, "REPEATS", 0.0f);
            PH ph; p->setPlayHead (&ph);
            auto s = run (*p, 48000, 20, kind, r, 512, 512, 0, &ph, false);
            std::printf("   kind %d: peak %.2f finite %d  %.2fx realtime\n", kind, s.peak, (int) s.finite, s.audioSecs / s.secs);
            worst = std::max<double> (worst, s.peak); fin = fin && s.finite; rt = std::max (rt, s.secs / s.audioSecs);
        }
        CHECK (fin, "all outputs finite"); CHECK (worst < 40.0, "output stays bounded");
        std::printf("   worst CPU load for one instance (-O1 build): %.1f%% of one core\n", rt * 100.0);
    }

    std::printf("B) Parameter slam: every parameter randomised/extreme every 1-64 samples (block size 1..64)\n");
    {
        std::unique_ptr<DelayAudioProcessor> p (make (48000, 64));
        PH ph; p->setPlayHead (&ph);
        auto s = run (*p, 48000, 25, 0, r, 1, 64, 1, &ph, true);
        std::printf("   peak %.2f finite %d %.2fx realtime\n", s.peak, (int) s.finite, s.audioSecs / s.secs);
        CHECK (s.finite && s.peak < 40.0f, "survives per-sample automation of all parameters");
    }

    std::printf("C) Hostile host: bpm 0 / negative / NaN / huge, ppq NaN / negative / 1e12, transport flipping\n");
    {
        std::unique_ptr<DelayAudioProcessor> p (make (48000, 512)); PH ph; p->setPlayHead (&ph);
        setp (*p, "SYNC", 1.0f); setp (*p, "GATE", 5.0f); setp (*p, "AUTOPAN", 3.0f); setp (*p, "POCKET", 1.0f); setp (*p, "POCKET_AMT", 1.0f); setp (*p, "FEEDBACK", 0.8f);
        setp (*p, "ELASTIC", 1.0f);
        const double bpms[] = { 0.0, -120.0, std::numeric_limits<double>::quiet_NaN(), 1.0e9, 1.0e-9, 999.0, 20.0, std::numeric_limits<double>::infinity() };
        const double ppqs[] = { 0.0, -50.0, std::numeric_limits<double>::quiet_NaN(), 1.0e12, 123.456, std::numeric_limits<double>::infinity() };
        bool fin = true; float peak = 0;
        for (int k = 0; k < 400; ++k)
        {
            ph.bpm = bpms[r.nextInt (8)]; ph.ppq = ppqs[r.nextInt (6)]; ph.playing = r.nextBool(); ph.hasBpm = r.nextInt (6) != 0; ph.hasPpq = r.nextInt (6) != 0;
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
            for (int i = 0; i < 256; ++i) { float v = 0.6f * std::sin (0.07f * (float) (i + k * 256)); b.setSample (0, i, v); b.setSample (1, i, v); }
            p->processBlock (b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { float v = b.getSample (c, i); if (! std::isfinite (v)) fin = false; peak = std::max (peak, std::abs (v)); }
        }
        std::printf("   peak %.2f finite %d\n", peak, (int) fin);
        CHECK (fin && peak < 40.0f, "garbage tempo / position info never breaks the audio");
        // after the garbage, normal tempo must work again
        ph.bpm = 120.0; ph.ppq = 0.0; ph.playing = true; ph.hasBpm = ph.hasPpq = true;
        auto s = run (*p, 48000, 3, 0, r, 512, 512, 0, &ph, false);
        CHECK (s.finite && s.peak < 40.0f, "recovers afterwards");
    }

    std::printf("D) Sample rates 8 kHz .. 384 kHz, block sizes 1 .. 65536\n");
    {
        bool ok = true; float peak = 0;
        const double rates[] = { 8000.0, 11025.0, 22050.0, 44100.0, 96000.0, 192000.0, 384000.0 };
        const int blocks[] = { 1, 3, 32, 480, 1024, 8192, 65536 };
        for (double sr : rates)
            for (int bs : blocks)
            {
                std::unique_ptr<DelayAudioProcessor> p (make (sr, bs)); PH ph; p->setPlayHead (&ph);
                for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (r.nextFloat());
                setp (*p, "BYPASS", 0.0f);
                auto s = run (*p, sr, std::min (2.0, 150000.0 / sr + 0.5), 0, r, bs, bs, 0, &ph, false);
                if (! s.finite || s.peak > 40.0f) { ok = false; std::printf("   FAIL at %.0f Hz block %d (peak %.1f finite %d)\n", sr, bs, s.peak, (int) s.finite); }
                peak = std::max (peak, s.peak);
            }
        std::printf("   49 combinations, worst peak %.2f\n", peak);
        CHECK (ok, "every rate / block-size combination stays finite and bounded");
    }

    std::printf("E) Rapid mode hammering: presets, A/B, kill, bypass, re-prepare every block\n");
    {
        std::unique_ptr<DelayAudioProcessor> p (make (48000, 256)); PH ph; p->setPlayHead (&ph);
        bool fin = true; float peak = 0;
        const int np = (int) getFactoryPresets().size();
        for (int k = 0; k < 1500; ++k)
        {
            switch (r.nextInt (7))
            {
                case 0: p->applyPreset (r.nextInt (np)); break;
                case 1: p->selectABSlot (r.nextInt (2)); break;
                case 2: p->killEchoes(); break;
                case 3: setp (*p, "BYPASS", (float) r.nextInt (2)); break;
                case 4: if (r.nextInt (20) == 0) p->prepareToPlay (r.nextBool() ? 44100.0 : 96000.0, 256); break;
                case 5: p->reset(); break;
                default: break;
            }
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
            for (int i = 0; i < 256; ++i) { float v = (r.nextFloat() * 2 - 1) * 0.8f; b.setSample (0, i, v); b.setSample (1, i, v); }
            ph.ppq += 256 / 48000.0 * 2.0;
            p->processBlock (b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { float v = b.getSample (c, i); if (! std::isfinite (v)) fin = false; peak = std::max (peak, std::abs (v)); }
        }
        std::printf("   peak %.2f finite %d\n", peak, (int) fin);
        CHECK (fin && peak < 40.0f, "survives constant preset / mode switching");
    }

    std::printf("F) Soak: 10 minutes of audio, randomly re-slammed every 2 s, then silence must decay\n");
    {
        std::unique_ptr<DelayAudioProcessor> p (make (48000, 512)); PH ph; p->setPlayHead (&ph);
        auto s = run (*p, 48000, 600, 0, r, 512, 512, 96000, &ph, false);
        std::printf("   peak %.2f finite %d  %.1fx realtime\n", s.peak, (int) s.finite, s.audioSecs / s.secs);
        CHECK (s.finite && s.peak < 40.0f, "10-minute soak finite and bounded");
        // now a sane setting: feedback 0.7, input silence for 60 s -> output must die out
        setp (*p, "FEEDBACK", 0.7f); setp (*p, "BYPASS", 0.0f); setp (*p, "REPEATS", 0.0f); setp (*p, "TAPE", 0.0f); setp (*p, "BBD", 0.0f); setp (*p, "SAUCE", 0.0f);
        setp (*p, "ELASTIC", 0.0f); setp (*p, "POCKET", 0.0f); setp (*p, "DESTINY", 0.0f);
        run (*p, 48000, 3, 0, r, 512, 512, 0, &ph, false);
        run (*p, 48000, 60, 6, r, 512, 512, 0, &ph, false);                     // let the legitimate echoes (max 2.5 s apart, fb 0.7) die
        auto q = run (*p, 48000, 10, 6, r, 512, 512, 0, &ph, false);
        CHECK (q.peak < 1e-3f, "echoes decay to silence after the input stops (no runaway / no stuck tone)");
        if (q.peak >= 1e-3f)
        {
            juce::MemoryBlock st; p->getStateInformation (st);
            std::printf("   state at failure:\n");
            for (auto* prm : p->getParameters()) std::printf("     %-12s %.3f\n", dynamic_cast<juce::RangedAudioParameter*> (prm)->paramID.toRawUTF8(), prm->getValue());
            {
                std::unique_ptr<DelayAudioProcessor> t (make (48000, 512)); PH ph2; t->setPlayHead (&ph2);
                t->setStateInformation (st.getData(), (int) st.getSize());
                run (*t, 48000, 3, 0, r, 512, 512, 0, &ph2, false);
                auto z = run (*t, 48000, 60, 6, r, 512, 512, 0, &ph2, false);
                std::printf("   fresh processor with the same settings: residual %.2e\n", z.peak);
                p->reset();
                auto z2 = run (*p, 48000, 30, 6, r, 512, 512, 0, &ph, false);
                std::printf("   original processor after reset(): residual %.2e\n", z2.peak);
                // what does the stuck output look like?
                std::unique_ptr<DelayAudioProcessor> u (make (48000, 512)); (void) u;
            }
            // greedy minimisation: reset parameters to default while it still fails
            std::vector<float> vals; for (auto* prm : p->getParameters()) vals.push_back (prm->getValue());
            auto fails_ = [&] (const std::vector<float>& v)
            {
                std::unique_ptr<DelayAudioProcessor> t (make (48000, 512)); PH ph2; t->setPlayHead (&ph2);
                for (size_t n = 0; n < v.size(); ++n) t->getParameters()[(int) n]->setValueNotifyingHost (v[n]);
                run (*t, 48000, 3, 0, r, 512, 512, 0, &ph2, false);
                return run (*t, 48000, 40, 6, r, 512, 512, 0, &ph2, false).peak > 1e-3f;
            };
            for (size_t n = 0; n < vals.size(); ++n)
            {
                auto c = vals; c[n] = p->getParameters()[(int) n]->getDefaultValue();
                if (fails_ (c)) vals = c;
            }
            std::printf("   minimal failing set (non-default):\n");
            for (size_t n = 0; n < vals.size(); ++n)
                if (std::abs (vals[n] - p->getParameters()[(int) n]->getDefaultValue()) > 1e-6f)
                    std::printf("     %-12s %.3f (default %.3f)\n", dynamic_cast<juce::RangedAudioParameter*> (p->getParameters()[(int) n])->paramID.toRawUTF8(), vals[n], p->getParameters()[(int) n]->getDefaultValue());
        }
    }

    std::printf("G) CPU: heaviest realistic configuration (all engines on)\n");
    {
        std::unique_ptr<DelayAudioProcessor> p (make (48000, 512)); PH ph; p->setPlayHead (&ph);
        setp (*p, "FEEDBACK", 0.7f); setp (*p, "TAPE", 1.0f); setp (*p, "BBD", 1.0f); setp (*p, "SHIMMER", 0.6f); setp (*p, "REVERSE", 1.0f);
        setp (*p, "SAUCE", 0.8f); setp (*p, "SWIRL", 0.8f); setp (*p, "WIDER", 0.8f); setp (*p, "RICHER", 0.8f); setp (*p, "GATE", 3.0f); setp (*p, "AUTOPAN", 3.0f);
        setp (*p, "POCKET", 1.0f); setp (*p, "POCKET_AMT", 1.0f); setp (*p, "ELASTIC", 1.0f); setp (*p, "DUCKING", 0.6f); setp (*p, "DESTINY", 1.0f);
        auto s = run (*p, 48000, 30, 0, r, 512, 512, 0, &ph, false);
        std::printf("   %.1f%% of one core at 48k/512 (unoptimised -O1 build; a release build is typically 2-4x lighter)\n", 100.0 * s.secs / s.audioSecs);
        CHECK (s.secs / s.audioSecs < 0.25, "heaviest setting under 25%% of a core even unoptimised");
    }

    std::printf("\n%s (%d failure%s)\n", fails ? "SOME FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
