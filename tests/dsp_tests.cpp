#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Presets.h"
#include <cstdio>
#include <cmath>
#include <array>
#include <functional>
#include <thread>
#include <chrono>
#include "OnsetDetector.h"
#include "SkellUi.h"

#ifdef SKELL_HARNESS   // only the hand-made harness (no CMake / BinaryData); the CMake test targets link the real ones
namespace BinaryData { const char* skell_bg_png = "x"; }
#endif

static const double SR = 48000.0;
static int failures = 0;

#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) ++failures; } while (0)

struct Rig
{
    DelayAudioProcessor proc;
    Rig()
    {
        proc.setRateAndBufferSizeDetails (SR, 512);
        proc.prepareToPlay (SR, 512);
    }
    void set (const char* id, float v)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id));
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    // run input (L,R) through processor; returns output L,R
    void run (const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR)
    {
        const int n = (int) inL.size();
        outL.assign ((size_t) n, 0.0f);  outR.assign ((size_t) n, 0.0f);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 512)
        {
            const int len = std::min (512, n - pos);
            juce::AudioBuffer<float> buf (2, len);
            for (int i = 0; i < len; ++i) { buf.setSample (0, i, inL[(size_t) (pos + i)]); buf.setSample (1, i, inR[(size_t) (pos + i)]); }
            proc.processBlock (buf, midi);
            for (int i = 0; i < len; ++i) { outL[(size_t) (pos + i)] = buf.getSample (0, i); outR[(size_t) (pos + i)] = buf.getSample (1, i); }
        }
    }
};

static std::vector<int> peaks (const std::vector<float>& x, float thresh)
{
    std::vector<int> p;
    for (size_t i = 1; i + 1 < x.size(); ++i)
        if (std::abs (x[i]) > thresh && std::abs (x[i]) >= std::abs (x[i - 1]) && std::abs (x[i]) > std::abs (x[i + 1]))
            if (p.empty() || (int) i - p.back() > 200) p.push_back ((int) i);
    return p;
}

static float rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return (float) std::sqrt (s / (double) (b - a));
}

static void impulse (std::vector<float>& L, std::vector<float>& R, int n, float l, float r)
{
    L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f); L[10] = l; R[10] = r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::vector<float> iL, iR, oL, oR;

    std::printf("1) Normal feedback, 100 ms, fb 0.5, impulse L+R\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.5f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 30000, 0.5f, 0.5f);
        r.run (iL, iR, oL, oR);
        auto pl = peaks (oL, 0.01f), pr = peaks (oR, 0.01f);
        std::printf("   L peaks:"); for (int p : pl) std::printf(" %d(%.3f)", p, oL[(size_t) p]); std::printf("\n");
        CHECK (pl.size() >= 3 && std::abs (pl[0] - (10 + 4800)) <= 3, "first echo at 100 ms (4800 samples)");
        CHECK (pl.size() >= 2 && std::abs (oL[(size_t) pl[1]] / oL[(size_t) pl[0]] - 0.5f) < 0.03f, "second echo is half the first (fb 0.5)");
        CHECK (pr.size() == pl.size(), "R mirrors L");
    }

    std::printf("2) Ping Pong: mono impulse should alternate L, R, L\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.6f); r.set ("DRY", 0.0f); r.set ("FB_MODE", 2.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 30000, 0.5f, 0.5f);
        r.run (iL, iR, oL, oR);
        auto pl = peaks (oL, 0.01f), pr = peaks (oR, 0.01f);
        std::printf("   L:"); for (int p : pl) std::printf(" %d", p); std::printf("   R:"); for (int p : pr) std::printf(" %d", p); std::printf("\n");
        CHECK (!pl.empty() && std::abs (pl[0] - 4810) <= 3, "echo 1 lands on LEFT at T");
        CHECK (!pr.empty() && std::abs (pr[0] - 9610) <= 3, "echo 2 lands on RIGHT at 2T");
        CHECK (pl.size() >= 2 && std::abs (pl[1] - 14410) <= 3, "echo 3 back on LEFT at 3T");
    }

    std::printf("3) Inverted: left-only input echoes on the RIGHT\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("FB_MODE", 1.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 12000, 0.5f, 0.0f);
        r.run (iL, iR, oL, oR);
        CHECK (peaks (oR, 0.01f).size() == 1 && peaks (oL, 0.01f).empty(), "echo only in R");
    }

    std::printf("4) Tempo sync (no host => 120 bpm): 1/4 = 500 ms, dotted = 750, triplet = 333.3\n");
    {
        Rig r; r.set ("SYNC", 1.0f); r.set ("SYNC_DIV", 4.0f);
        r.set ("FEEL", 0.0f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (std::abs (r.proc.getEffectiveDelayMs() - 500.0f) < 0.5f, "1/4 straight = 500 ms");
        r.set ("FEEL", 1.0f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (std::abs (r.proc.getEffectiveDelayMs() - 750.0f) < 0.5f, "1/4 dotted = 750 ms");
        r.set ("FEEL", 2.0f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (std::abs (r.proc.getEffectiveDelayMs() - 333.33f) < 0.5f, "1/4 triplet = 333.3 ms");
        r.set ("SYNC_DIV", 6.0f); r.set ("FEEL", 0.0f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (std::abs (r.proc.getEffectiveDelayMs() - 2000.0f) < 0.5f, "1/1 = 2000 ms");
    }

    std::printf("5) Free time range: 0.01 ms floor and 2500 ms ceiling\n");
    {
        Rig r; r.set ("DELAY_TIME", 2500.0f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (std::abs (r.proc.getEffectiveDelayMs() - 2500.0f) < 0.5f, "2500 ms accepted");
        r.set ("DELAY_TIME", 0.01f); r.run (std::vector<float>(512, 0.f), std::vector<float>(512, 0.f), oL, oR);
        CHECK (r.proc.getEffectiveDelayMs() > 0.05f && r.proc.getEffectiveDelayMs() < 0.2f, "tiny time clamped to a safe minimum (>= 3 samples)");
    }

    std::printf("6) Ducker: wet drops while the dry signal plays\n");
    {
        const int n = 48000;
        iL.assign ((size_t) n, 0.f); iR.assign ((size_t) n, 0.f);
        for (int i = 0; i < n; ++i) { float s = 0.4f * std::sin (6.2831853f * 440.f * (float) i / (float) SR); iL[(size_t) i] = iR[(size_t) i] = s; }
        float wetNoDuck, wetDuck;
        { Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.5f); r.set ("DRY", 0.0f); r.set ("DUCKING", 0.0f); r.set ("SMOOTH", 0.0f); r.proc.prepareToPlay (SR, 512); r.run (iL, iR, oL, oR); wetNoDuck = rms (oL, 20000, 40000); }
        { Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.5f); r.set ("DRY", 0.0f); r.set ("DUCKING", 1.0f); r.set ("SMOOTH", 0.0f); r.proc.prepareToPlay (SR, 512); r.run (iL, iR, oL, oR); wetDuck = rms (oL, 20000, 40000); }
        std::printf("   wet rms no-duck %.4f  duck=1 %.4f\n", wetNoDuck, wetDuck);
        CHECK (wetDuck < wetNoDuck * 0.05f, "100% ducking removes the wet signal under a loud input");
        // trails return after input stops
        std::vector<float> sil (48000, 0.f);
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.8f); r.set ("DRY", 0.0f); r.set ("DUCKING", 1.0f); r.set ("SMOOTH", 0.0f); r.proc.prepareToPlay (SR, 512);
        std::vector<float> inL = iL, inR = iR; inL.insert (inL.end(), sil.begin(), sil.end()); inR.insert (inR.end(), sil.begin(), sil.end());
        r.run (inL, inR, oL, oR);
        std::printf("   during input %.4f  after stop(0.3-0.6s) %.4f\n", rms (oL, 30000, 46000), rms (oL, 48000 + 14400, 48000 + 28800));
        CHECK (rms (oL, 48000 + 14400, 48000 + 28800) > rms (oL, 30000, 46000) * 5.0f, "echoes swell back once input stops");
    }

    std::printf("7) CHEAP: output is quantised to 11 bit\n");
    {
        Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("CHEAP", 1.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        iL.assign (6000, 0.f); iR = iL; for (int i = 0; i < 6000; ++i) iL[(size_t) i] = iR[(size_t) i] = 0.3f * std::sin (0.05f * (float) i);
        r.run (iL, iR, oL, oR);
        bool onGrid = true; int checked = 0;
        for (size_t i = 3000; i < 6000; ++i) { float q = oL[i] * 1024.0f; if (std::abs (q - std::round (q)) > 1e-3f) onGrid = false; ++checked; }
        CHECK (onGrid, "all samples are multiples of 1/1024");
    }

    std::printf("8) Stress: everything maxed, feedback 100%%, noise, 15 s -> finite and bounded\n");
    {
        Rig r;
        const char* ids[] = { "WIDER", "RICHER", "TAPE", "SWIRL", "SAUCE", "DUCKING" };
        for (auto* id : ids) r.set (id, 1.0f);
        r.set ("FEEDBACK", 1.0f); r.set ("DELAY_TIME", 120.0f); r.set ("DESTINY", 1.0f); r.set ("CHEAP", 1.0f); r.set ("FB_MODE", 2.0f);
        r.set ("ATTACK", 1.0f); r.set ("SUSTAIN", 1.0f); r.set ("HICUT", 3000.0f); r.set ("LOCUT", 300.0f);
        juce::Random rnd (1234);
        float maxAbs = 0; bool finite = true;
        for (int blk = 0; blk < 15 * 48000 / 512; ++blk)
        {
            iL.assign (512, 0.f); iR.assign (512, 0.f);
            for (int i = 0; i < 512; ++i) { iL[(size_t) i] = (rnd.nextFloat() * 2 - 1) * 0.9f; iR[(size_t) i] = (rnd.nextFloat() * 2 - 1) * 0.9f; }
            r.run (iL, iR, oL, oR);
            for (int i = 0; i < 512; ++i) { if (! std::isfinite (oL[(size_t) i]) || ! std::isfinite (oR[(size_t) i])) finite = false; maxAbs = std::max ({ maxAbs, std::abs (oL[(size_t) i]), std::abs (oR[(size_t) i]) }); }
        }
        std::printf("   max |out| = %.4f\n", maxAbs);
        CHECK (finite, "no NaN / inf");
        CHECK (maxAbs <= 1.001f, "safety limiter holds output <= 1.0");
    }

    std::printf("9) Hi-cut darkens repeats, Lo-cut thins them\n");
    {
        auto energy = [&] (float lo, float hi, bool bright)
        {
            Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("LOCUT", lo); r.set ("HICUT", hi); r.set ("SMOOTH", 0.0f);
            r.proc.prepareToPlay (SR, 512);
            iL.assign (12000, 0.f); iR = iL;
            for (int i = 0; i < 12000; ++i) iL[(size_t) i] = iR[(size_t) i] = 0.3f * std::sin (6.2831853f * (bright ? 8000.f : 60.f) * (float) i / (float) SR);
            r.run (iL, iR, oL, oR); return rms (oL, 6000, 12000);
        };
        float open = energy (20, 20000, true), closed = energy (20, 2000, true);
        float openLow = energy (20, 20000, false), cutLow = energy (500, 20000, false);
        std::printf("   8k: open %.3f  hicut 2k %.3f   60Hz: open %.3f  locut 500 %.3f\n", open, closed, openLow, cutLow);
        CHECK (closed < open * 0.2f, "Hi-Cut 2 kHz attenuates 8 kHz");
        CHECK (cutLow < openLow * 0.2f, "Lo-Cut 500 Hz attenuates 60 Hz");
    }

    std::printf("10) Pan moves the delayed signal only; Mono sums it\n");
    {
        Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("PAN", 1.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 6000, 0.5f, 0.5f); r.run (iL, iR, oL, oR);
        CHECK (peaks (oL, 0.01f).empty() && ! peaks (oR, 0.01f).empty(), "PAN hard right silences the left delay");
        Rig r2; r2.set ("DELAY_TIME", 50.0f); r2.set ("FEEDBACK", 0.0f); r2.set ("DRY", 0.0f); r2.set ("MONO", 1.0f); r2.set ("SMOOTH", 0.0f);
        r2.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 6000, 0.6f, 0.0f); r2.run (iL, iR, oL, oR);
        CHECK (std::abs (oL[2410] - oR[2410]) < 1e-4f, "MONO makes L == R");
    }

    std::printf("11) Flavour knobs audibly change the wet signal and stay finite (tape / swirl / sauce / wider / richer / attack)\n");
    {
        auto wetRms = [&] (const char* id, float v)
        {
            Rig r; r.set ("DELAY_TIME", 80.0f); r.set ("FEEDBACK", 0.4f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
            if (id) r.set (id, v);
            r.proc.prepareToPlay (SR, 512);
            iL.assign (48000, 0.f); iR = iL;
            for (int i = 0; i < 12000; ++i) iL[(size_t) i] = iR[(size_t) i] = 0.3f * std::sin (6.2831853f * 300.f * (float) i / (float) SR) * std::exp (-(float) (i % 6000) / 800.f);
            r.run (iL, iR, oL, oR);
            double diff = 0; for (size_t i = 0; i < oL.size(); ++i) diff += std::abs (oL[i]); return (float) diff;
        };
        float base = wetRms (nullptr, 0.f);
        for (auto* id : { "TAPE", "SWIRL", "SAUCE", "WIDER", "RICHER", "ATTACK", "SUSTAIN" })
        {
            float v = wetRms (id, (std::string (id) == "ATTACK" || std::string (id) == "SUSTAIN") ? -1.0f : 1.0f);
            std::printf("   %-8s |out| sum %.2f (base %.2f)\n", id, v, base);
            CHECK (std::isfinite (v) && std::abs (v - base) > 1e-3f * base, id);
        }
    }

    std::printf("12) Knob moves must not click (zipper): WET 0 -> 1 and DRY 1 -> 0 in one step on a steady tone\n");
    {
        auto maxJump = [&] (const char* id, float from, float to, bool wetPath)
        {
            Rig r; r.set ("DELAY_TIME", 30.0f); r.set ("FEEDBACK", 0.0f); r.set ("SMOOTH", 0.0f);
            r.set ("DRY", wetPath ? 0.0f : 1.0f); r.set ("WET", wetPath ? 1.0f : 0.0f);
            r.set (id, from); r.proc.prepareToPlay (SR, 512);
            std::vector<float> L (48000), R (48000);
            for (size_t i = 0; i < 48000; ++i) L[i] = R[i] = 0.5f * std::sin (6.2831853f * 220.f * (float) i / (float) SR);
            std::vector<float> a, b, c, d;
            std::vector<float> L1 (L.begin(), L.begin() + 24000), R1 (R.begin(), R.begin() + 24000);
            std::vector<float> L2 (L.begin() + 24000, L.end()), R2 (R.begin() + 24000, R.end());
            r.run (L1, R1, a, b); r.set (id, to); r.run (L2, R2, c, d);
            // largest difference between consecutive samples around the change, relative to the tone's own max slope
            float worst = 0.f;
            for (size_t i = 23990; i < 24000 + 700; ++i) { float x0 = (i < 24000 ? a[i] : c[i - 24000]); float x1 = (i + 1 < 24000 ? a[i + 1] : c[i + 1 - 24000]); worst = std::max (worst, std::abs (x1 - x0)); }
            return worst;
        };
        const float toneSlope = 0.5f * 6.2831853f * 220.f / (float) SR;   // ~0.0144 per sample
        float wetJump = maxJump ("WET", 0.0f, 1.0f, true);
        float dryJump = maxJump ("DRY", 1.0f, 0.0f, false);
        std::printf("   max step: WET %.4f  DRY %.4f  (tone's own max slope %.4f)\n", wetJump, dryJump, toneSlope);
        CHECK (wetJump < toneSlope * 2.5f, "WET change is smooth");
        CHECK (dryJump < toneSlope * 2.5f, "DRY change is smooth");
    }

    std::printf("13) Dry path must be untouched (no limiter colouring on a hot dry signal)\n");
    {
        Rig r; r.set ("WET", 0.0f); r.set ("DRY", 1.0f);
        iL.assign (4096, 0.f); iR = iL; for (size_t i = 0; i < 4096; ++i) iL[i] = iR[i] = ((i / 64) % 2 ? 1.0f : -1.0f);
        r.run (iL, iR, oL, oR);
        float worst = 0; for (size_t i = 0; i < 4096; ++i) worst = std::max (worst, std::abs (oL[i] - iL[i]));
        std::printf("   max |out - in| on a full-scale square = %.4f\n", worst);
        CHECK (worst < 1e-6f, "dry passes bit-exact");
    }

    std::printf("14) DC at 100%% feedback must not build up forever\n");
    {
        Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 1.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        std::vector<float> dc (48000, 0.05f), z (48000 * 20, 0.0f);
        dc.insert (dc.end(), z.begin(), z.end());
        std::vector<float> dcR = dc; r.run (dc, dcR, oL, oR);
        float tailMax = 0; for (size_t i = oL.size() - 24000; i < oL.size(); ++i) tailMax = std::max (tailMax, std::abs (oL[i]));
        std::printf("   level 19-20 s after the DC stops: %.4f\n", tailMax);
        CHECK (tailMax < 0.02f, "DC does not latch into the loop");
    }

    std::printf("15) Very large host blocks (8192 / 16384) work\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.5f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("SAUCE", 0.5f);
        r.proc.prepareToPlay (SR, 512);
        juce::MidiBuffer midi; juce::AudioBuffer<float> buf (2, 16384); buf.clear(); buf.setSample (0, 10, 0.5f); buf.setSample (1, 10, 0.5f);
        r.proc.processBlock (buf, midi);
        bool finite = true; float e = 0; for (int i = 0; i < 16384; ++i) { float v = buf.getSample (0, i); if (! std::isfinite (v)) finite = false; e = std::max (e, std::abs (v)); }
        CHECK (finite && e > 0.05f, "16384-sample block processed, echoes present");
    }

    std::printf("16) reset() clears old trails\n");
    {
        Rig r; r.set ("DELAY_TIME", 500.0f); r.set ("FEEDBACK", 0.9f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        impulse (iL, iR, 4000, 0.8f, 0.8f); r.run (iL, iR, oL, oR);
        r.proc.reset();
        std::vector<float> z (48000, 0.f); r.run (z, z, oL, oR);
        float e = 0; for (float v : oL) e = std::max (e, std::abs (v));
        CHECK (e < 1e-6f, "silent after reset()");
    }

    std::printf("17) Big time jump cross-fades (no pitch-sweep): 220 Hz tone, 100 -> 400 ms\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 50.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = 48000;
        std::vector<float> t (N);
        for (int i = 0; i < N; ++i) t[i] = 0.5f * std::sin (2.0 * 3.14159265358979 * 220.0 * i / SR);
        r.run (t, t, oL, oR);            // fill the line
        r.set ("DELAY_TIME", 400.0f);
        r.run (t, t, oL, oR);            // during + after the jump
        // zero-crossing rate over the first 150 ms after the jump (a glide would sweep it well below 220 Hz)
        int zc = 0; const int M = (int) (0.15 * SR);
        for (int i = 1; i < M; ++i) if ((oL[i-1] < 0) != (oL[i] < 0)) ++zc;
        double hz = zc / 2.0 / 0.15;
        std::printf("   apparent pitch right after jump: %.1f Hz\n", hz);
        CHECK (hz > 200.0 && hz < 240.0, "pitch preserved through time jump");
    }

    std::printf("18) Reverse: a noise burst comes back time-reversed under the hann window\n");
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("REVERSE", 1.0f);
        r.proc.prepareToPlay (SR, 512);
        const int S = 4800, N = 3 * S;
        std::vector<float> x ((size_t) N, 0.0f);
        unsigned seed = 12345;
        for (int i = 0; i < S; ++i) { seed = seed * 1664525u + 1013904223u; x[(size_t) i] = ((seed >> 8) / 8388608.0f - 1.0f) * 0.4f; }
        r.run (x, x, oL, oR);
        double sxy = 0, sxx = 0, syy = 0;
        for (int t = S + 60; t < 2 * S - 60; ++t)
        {
            const double p = (t - S) / (double) S;
            const double w = std::sin (3.14159265358979 * p) * std::sin (3.14159265358979 * p);
            const double ref = w * x[(size_t) (2 * S - t)];
            sxy += ref * oL[(size_t) t]; sxx += ref * ref; syy += (double) oL[(size_t) t] * oL[(size_t) t];
        }
        const double corr = sxy / std::sqrt (sxx * syy);
        std::printf("   correlation with windowed time-reversed input: %.4f\n", corr);
        CHECK (corr > 0.97, "reverse output matches the time-reversed, windowed input");
        // no reverse -> not correlated
        Rig q; q.set ("DELAY_TIME", 100.0f); q.set ("FEEDBACK", 0.0f); q.set ("DRY", 0.0f); q.set ("SMOOTH", 0.0f);
        q.proc.prepareToPlay (SR, 512);
        q.run (x, x, oL, oR);
        double sxy2 = 0, sxx2 = 0, syy2 = 0;
        for (int t = S + 60; t < 2 * S - 60; ++t)
        {
            const double p = (t - S) / (double) S;
            const double w = std::sin (3.14159265358979 * p) * std::sin (3.14159265358979 * p);
            const double ref = w * x[(size_t) (2 * S - t)];
            sxy2 += ref * oL[(size_t) t]; sxx2 += ref * ref; syy2 += (double) oL[(size_t) t] * oL[(size_t) t];
        }
        CHECK (std::abs (sxy2 / std::sqrt (sxx2 * syy2 + 1e-12)) < 0.1, "forward mode is not reversed (control)");
    }

    std::printf("19) Reverse on/off and sync/time jumps mid-stream stay finite and bounded\n");
    {
        Rig r; r.set ("SYNC", 1.0f); r.set ("SYNC_DIV", 3.0f); r.set ("FEEDBACK", 0.8f); r.set ("DRY", 0.5f);
        r.proc.prepareToPlay (SR, 512);
        const int N = 48000;
        std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.5f * std::sin (2 * 3.14159265358979 * 330.0 * i / SR);
        float worst = 0; bool fin = true;
        for (int k = 0; k < 8; ++k)
        {
            r.set ("REVERSE", (k & 1) ? 0.0f : 1.0f); r.set ("SYNC_DIV", (float) (k % 7)); r.set ("SHIMMER", k % 3 ? 0.5f : 0.0f);
            r.run (t, t, oL, oR);
            for (size_t i = 0; i < oL.size(); ++i) { worst = std::max ({ worst, std::abs (oL[i]), std::abs (oR[i]) }); fin = fin && std::isfinite (oL[i]) && std::isfinite (oR[i]); }
        }
        std::printf("   worst |sample| %.3f\n", worst);
        CHECK (fin && worst < 2.0f, "finite and bounded");
    }

    std::printf("20) Tape wow / flutter / drift modulates the pitch of the repeats\n");
    {
        auto phaseStd = [&] (float tape)
        {
            Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("TAPE", tape);
            r.proc.prepareToPlay (SR, 512);
            const int N = 4 * 48000;
            std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * 1000.0 * i / SR);
            r.run (t, t, oL, oR);
            std::vector<double> ph;
            const int W = 240;
            for (int s = 9600; s + W < N; s += W)
            {
                double re = 0, im = 0;
                for (int i = 0; i < W; ++i) { const double a = 2 * 3.14159265358979 * 1000.0 * (s + i) / SR; re += oL[(size_t) (s + i)] * std::cos (a); im -= oL[(size_t) (s + i)] * std::sin (a); }
                ph.push_back (std::atan2 (im, re));
            }
            for (size_t i = 1; i < ph.size(); ++i) while (ph[i] - ph[i-1] > 3.14159) ph[i] -= 2*3.14159265358979, (void) 0;
            for (size_t i = 1; i < ph.size(); ++i) while (ph[i] - ph[i-1] < -3.14159) ph[i] += 2*3.14159265358979;
            double m = 0; for (double v : ph) m += v; m /= (double) ph.size();
            double v2 = 0; for (double v : ph) v2 += (v - m) * (v - m);
            return std::sqrt (v2 / (double) ph.size());
        };
        const double a = phaseStd (0.0f), b = phaseStd (1.0f);
        std::printf("   phase wobble (rad): tape 0 -> %.3f   tape 1 -> %.3f\n", a, b);
        CHECK (a < 0.05 && b > 0.5, "TAPE adds wow/flutter in the loop");
    }

    std::printf("21) Tape in the loop darkens and compresses each repeat more than the first\n");
    {
        auto echoPeaks = [&] (float tape)
        {
            Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.7f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("TAPE", tape);
            r.proc.prepareToPlay (SR, 512);
            const int N = 48000;
            std::vector<float> t ((size_t) N, 0.0f); for (int i = 0; i < 4800; ++i) t[(size_t) i] = 0.6f * std::sin (2 * 3.14159265358979 * 6000.0 * i / SR);
            r.run (t, t, oL, oR);
            return std::array<float, 3> { rms (oL, 4800, 9600), rms (oL, 14400, 19200), rms (oL, 24000, 28800) };
        };
        auto c = echoPeaks (0.0f), d = echoPeaks (1.0f);
        std::printf("   6 kHz echo rms  tape0: %.3f %.3f %.3f   tape1: %.3f %.3f %.3f\n", c[0], c[1], c[2], d[0], d[1], d[2]);
        CHECK (d[2] / d[0] < 0.7f * (c[2] / c[0]), "later repeats lose more highs with tape");
    }

    std::printf("22) Shimmer shifts the repeats (+12 -> octave up, -12 -> octave down)\n");
    {
        auto zcFreq = [&] (const std::vector<float>& x, int a, int b)
        {
            int zc = 0; for (int i = a + 1; i < b; ++i) if ((x[(size_t) i - 1] < 0) != (x[(size_t) i] < 0)) ++zc;
            return zc / 2.0 / ((b - a) / SR);
        };
        for (int pitch : { 0, 2 })
        {
            Rig r; r.set ("DELAY_TIME", 200.0f); r.set ("FEEDBACK", 0.7f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
            r.set ("SHIMMER", 1.0f); r.set ("SHIM_PITCH", (float) pitch);
            r.proc.prepareToPlay (SR, 512);
            const int N = 48000;
            std::vector<float> t ((size_t) N, 0.0f); for (int i = 0; i < 7200; ++i) t[(size_t) i] = 0.5f * std::sin (2 * 3.14159265358979 * 440.0 * i / SR);
            r.run (t, t, oL, oR);
            const double f1 = zcFreq (oL, (int) (0.22 * SR), (int) (0.30 * SR));     // first echo (unshifted)
            const double f2 = zcFreq (oL, (int) (0.46 * SR), (int) (0.54 * SR));     // second echo (shifted once)
            std::printf("   %s: echo1 %.0f Hz, echo2 %.0f Hz (ratio %.2f)\n", kShimNames[pitch], f1, f2, f2 / f1);
            const double want = kShimRatio[pitch];
            CHECK (std::abs (f2 / f1 - want) < 0.12 * want, "second repeat is shifted by the chosen interval");
        }
    }

    std::printf("23) Factory presets: all load, stay finite, bounded; program / A-B plumbing\n");
    {
        Rig r;
        const int np = r.proc.getNumPrograms();
        CHECK (np >= 15, "has factory presets");
        std::vector<float> n1 ((size_t) 24000), n2 ((size_t) 24000);
        unsigned seed = 99;
        for (size_t i = 0; i < n1.size(); ++i) { seed = seed * 1664525u + 1013904223u; n1[i] = ((seed >> 8) / 8388608.0f - 1.0f) * 0.25f * (i % 12000 < 3000 ? 1.0f : 0.0f);
                                                  seed = seed * 1664525u + 1013904223u; n2[i] = ((seed >> 8) / 8388608.0f - 1.0f) * 0.25f * (i % 12000 < 3000 ? 1.0f : 0.0f); }
        bool allOk = true;
        for (int p = 0; p < np; ++p)
        {
            r.proc.setCurrentProgram (p);
            r.proc.prepareToPlay (SR, 512);
            r.run (n1, n2, oL, oR);
            r.run (n1, n2, oL, oR);
            float worst = 0; bool fin = true;
            for (size_t i = 0; i < oL.size(); ++i) { worst = std::max ({ worst, std::abs (oL[i]), std::abs (oR[i]) }); fin = fin && std::isfinite (oL[i]) && std::isfinite (oR[i]); }
            const bool ok = fin && worst < 1.6f && r.proc.getCurrentProgram() == p;
            if (! ok) std::printf("   preset %d (%s): worst %.3f finite %d\n", p, r.proc.getProgramName (p).toRawUTF8(), worst, (int) fin);
            allOk = allOk && ok;
        }
        CHECK (allOk, "every preset loads, is finite and bounded, and reports its index");

        r.proc.setCurrentProgram (0);
        CHECK (std::abs (r.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.5f) < 1e-4f
            && r.proc.apvts.getRawParameterValue ("SYNC")->load() < 0.5f, "Init restores defaults (clears the previous preset)");
        r.proc.setCurrentProgram (4);   // Ping Pong Bounce: sync + ping pong
        CHECK (r.proc.apvts.getRawParameterValue ("SYNC")->load() > 0.5f && r.proc.apvts.getRawParameterValue ("FB_MODE")->load() > 1.5f, "preset values applied");

        r.set ("FEEDBACK", 0.3f);
        r.proc.selectABSlot (1);
        CHECK (std::abs (r.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.3f) < 1e-4f, "B starts as a copy of A");
        r.set ("FEEDBACK", 0.8f);
        r.proc.selectABSlot (0);
        CHECK (std::abs (r.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.3f) < 1e-4f, "A recalled");
        r.proc.selectABSlot (1);
        CHECK (std::abs (r.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.8f) < 1e-4f, "B recalled");
    }

    std::printf("24) Ducker: sidechain input and release time\n");
    {
        auto runSc = [&] (bool scLoud, float rel, bool useSc, bool longMain = false)
        {
            DelayAudioProcessor p;
            juce::AudioProcessor::BusesLayout lay;
            lay.inputBuses.add (juce::AudioChannelSet::stereo());
            lay.inputBuses.add (useSc ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            lay.outputBuses.add (juce::AudioChannelSet::stereo());
            const bool okLayout = p.setBusesLayout (lay);
            if (! okLayout) std::printf("   (layout refused)\n");
            p.setRateAndBufferSizeDetails (SR, 512);
            auto set = [&] (const char* id, float v) { auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            set ("DELAY_TIME", 100.0f); set ("FEEDBACK", 0.0f); set ("DRY", 0.0f); set ("SMOOTH", 0.0f); set ("DUCKING", 1.0f); set ("DUCK_REL", rel);
            p.prepareToPlay (SR, 512);
            const int N = 24000;
            std::vector<float> out;
            juce::MidiBuffer midi;
            for (int pos = 0; pos < N; pos += 512)
            {
                const int len = std::min (512, N - pos);
                juce::AudioBuffer<float> buf (useSc ? 4 : 2, len);
                buf.clear();
                for (int i = 0; i < len; ++i)
                {
                    const int t = pos + i;
                    const float tone = 0.5f * std::sin (2 * 3.14159265358979 * 440.0 * t / SR);
                    const bool burst = longMain || t < 4800;                 // main input: 100 ms tone burst, so echo lands at 100-200 ms
                    buf.setSample (0, i, burst ? tone : 0.0f); buf.setSample (1, i, burst ? tone : 0.0f);
                    if (useSc && scLoud && (longMain ? t < 6000 : (t > 4000 && t < 12000))) { buf.setSample (2, i, tone); buf.setSample (3, i, tone); }
                }
                p.processBlock (buf, midi);
                for (int i = 0; i < len; ++i) out.push_back (buf.getSample (0, i));
            }
            return out;
        };
        auto e = [&] (const std::vector<float>& x, int a, int b) { return rms (x, (size_t) a, (size_t) b); };
        auto quiet = runSc (false, 250.0f, true);    // sidechain connected but silent: nothing ducks the echo
        auto loud  = runSc (true,  250.0f, true);    // sidechain loud around the echo: ducked
        auto noSc  = runSc (false, 250.0f, false);   // no sidechain: the input burst itself ducks the early echo
        std::printf("   echo rms 5000-9000: sidechain silent %.3f   sidechain loud %.3f   no sidechain %.3f\n", e (quiet, 5000, 9000), e (loud, 5000, 9000), e (noSc, 5000, 9000));
        CHECK (e (quiet, 5000, 9000) > 0.2f, "silent sidechain does not duck");
        CHECK (e (loud, 5000, 9000) < 0.25f * e (quiet, 5000, 9000), "loud sidechain ducks the delay");

        auto fast = runSc (true, 20.0f, true, true), slow = runSc (true, 1000.0f, true, true);
        std::printf("   after the sidechain stops, wet rms 7000-8500: release 20 ms %.3f   release 1000 ms %.3f\n", e (fast, 7000, 8500), e (slow, 7000, 8500));
        CHECK (e (fast, 7000, 8500) > 3.0f * e (slow, 7000, 8500), "release time controls how fast the delay returns");
    }

    std::printf("25) Mod rate / shape control Wider & Richer\n");
    {
        auto wobble = [&] (float rate, int shape, std::vector<float>* keep = nullptr)
        {
            Rig r; r.set ("DELAY_TIME", 50.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
            r.set ("WIDER", 1.0f); r.set ("MOD_RATE", rate); r.set ("MOD_SHAPE", (float) shape);
            r.proc.prepareToPlay (SR, 512);
            const int N = 8 * 48000;
            std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * 1000.0 * i / SR);
            r.run (t, t, oL, oR);
            if (keep) *keep = oL;
            std::vector<double> ph; const int W = 240;
            for (int s0 = 9600; s0 + W < N; s0 += W)
            {
                double re = 0, im = 0;
                for (int i = 0; i < W; ++i) { const double a = 2 * 3.14159265358979 * 1000.0 * (s0 + i) / SR; re += oL[(size_t) (s0 + i)] * std::cos (a); im -= oL[(size_t) (s0 + i)] * std::sin (a); }
                ph.push_back (std::atan2 (im, re));
            }
            for (size_t i = 1; i < ph.size(); ++i) { while (ph[i] - ph[i-1] > 3.14159) ph[i] -= 2 * 3.14159265358979; while (ph[i] - ph[i-1] < -3.14159) ph[i] += 2 * 3.14159265358979; }
            double m = 0; for (double v : ph) m += v; m /= (double) ph.size();
            int cross = 0; for (size_t i = 1; i < ph.size(); ++i) if ((ph[i-1] - m < 0) != (ph[i] - m < 0)) ++cross;
            return cross;
        };
        const int c1 = wobble (1.0f, 0), c4 = wobble (4.0f, 0);
        std::printf("   phase crossings over 8 s: rate 1x -> %d   rate 4x -> %d\n", c1, c4);
        CHECK (c4 > 2.5 * c1 && c1 >= 2, "MOD_RATE speeds the modulation up");
        std::vector<float> sine, tri, rnd;
        wobble (1.0f, 0, &sine); wobble (1.0f, 1, &tri); wobble (1.0f, 2, &rnd);
        double dTri = 0, dRnd = 0; for (size_t i = 0; i < sine.size(); ++i) { dTri += std::abs (sine[i] - tri[i]); dRnd += std::abs (sine[i] - rnd[i]); }
        bool fin = true; for (float v : rnd) fin = fin && std::isfinite (v) && std::abs (v) < 1.0f;
        CHECK (dTri / (double) sine.size() > 0.01 && dRnd / (double) sine.size() > 0.01, "triangle and random shapes sound different from sine");
        CHECK (fin, "random shape finite and bounded");
    }

    std::printf("26) BBD: darker with longer delay, noise breathes with the signal, silent when idle\n");
    {
        auto toneRms = [&] (float hz, bool bbd, float ms)
        {
            Rig r; r.set ("DELAY_TIME", ms); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("BBD", bbd ? 1.0f : 0.0f);
            r.proc.prepareToPlay (SR, 512);
            const int N = 48000;
            std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * hz * i / SR);
            r.run (t, t, oL, oR);
            return rms (oL, 30000, 46000);
        };
        const float off8 = toneRms (8000.0f, false, 300.0f), on8 = toneRms (8000.0f, true, 300.0f);
        const float off200 = toneRms (200.0f, false, 300.0f), on200 = toneRms (200.0f, true, 300.0f);
        const float on8short = toneRms (8000.0f, true, 20.0f);
        std::printf("   8 kHz: off %.3f  bbd@300ms %.3f  bbd@20ms %.3f    200 Hz: off %.3f  bbd %.3f\n", off8, on8, on8short, off200, on200);
        CHECK (on8 < 0.4f * off8, "BBD darkens highs at a long delay time (some aliasing grit remains by design)");
        CHECK (on8short > 2.0f * on8, "... but is brighter at a short delay time (higher clock)");
        CHECK (std::abs (on200 / off200 - 1.0f) < 0.15f, "lows pass through");
        Rig r; r.set ("DELAY_TIME", 300.0f); r.set ("FEEDBACK", 0.8f); r.set ("DRY", 0.0f); r.set ("BBD", 1.0f); r.proc.prepareToPlay (SR, 512);
        std::vector<float> z (96000, 0.0f); r.run (z, z, oL, oR);
        float e = 0; for (float v : oL) e = std::max (e, std::abs (v));
        CHECK (e < 1e-3f, "idle BBD is (nearly) silent");
    }

    std::printf("27) Rhythmic gate chops the echoes in time\n");
    {
        Rig r; r.set ("DELAY_TIME", 20.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("GATE", 2.0f);   // 1/16: open on even steps
        r.proc.prepareToPlay (SR, 512);
        const int N = 48000;
        std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
        r.run (t, t, oL, oR);
        // 120 bpm -> a 1/16 step is 6000 samples
        const float open = rms (oL, 1000, 5500), closed = rms (oL, 7000, 11500), open2 = rms (oL, 13000, 17500);
        std::printf("   step rms: open %.3f  closed %.4f  open %.3f\n", open, closed, open2);
        CHECK (open > 0.2f && open2 > 0.2f && closed < 0.03f * open, "echoes pass on open steps and are cut on closed ones");
        r.set ("GATE_DEPTH", 0.5f);
        r.run (t, t, oL, oR);
        CHECK (rms (oL, 7000, 11500) > 0.2f * open, "depth < 1 lets some signal through");
    }

    std::printf("28) Soft bypass and kill\n");
    {
        // bypass from the start: dry passes, nothing enters the delay
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.6f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f); r.set ("BYPASS", 1.0f);
        r.proc.prepareToPlay (SR, 512);
        std::vector<float> t (24000); for (size_t i = 0; i < t.size(); ++i) t[i] = (i < 4800) ? 0.5f * std::sin (2 * 3.14159265358979 * 440.0 * i / SR) : 0.0f;
        r.run (t, t, oL, oR);
        float dev = 0; for (size_t i = 0; i < 4800; ++i) dev = std::max (dev, std::abs (oL[i] - t[i]));
        CHECK (dev < 1e-5f, "bypassed: output equals the input");
        CHECK (rms (oL, 6000, 24000) < 1e-4f, "bypassed: no echoes are created");

        // bypass after the burst: the tail keeps ringing
        Rig q; q.set ("DELAY_TIME", 100.0f); q.set ("FEEDBACK", 0.8f); q.set ("DRY", 0.0f); q.set ("SMOOTH", 0.0f);
        q.proc.prepareToPlay (SR, 512);
        q.run (t, t, oL, oR);
        q.set ("BYPASS", 1.0f);
        std::vector<float> z (24000, 0.0f); q.run (z, z, oL, oR);
        std::printf("   tail after bypass: rms %.3f\n", rms (oL, 0, 12000));
        CHECK (rms (oL, 0, 12000) > 0.05f, "bypass keeps the tails");

        // kill
        Rig k; k.set ("DELAY_TIME", 100.0f); k.set ("FEEDBACK", 0.9f); k.set ("DRY", 0.0f); k.set ("SMOOTH", 0.0f);
        k.proc.prepareToPlay (SR, 512);
        k.run (t, t, oL, oR);
        k.proc.killEchoes();
        k.run (z, z, oL, oR);
        std::printf("   after kill: rms 0-10ms %.3f   100-500ms %.6f\n", rms (oL, 0, 480), rms (oL, 4800, 24000));
        CHECK (rms (oL, 4800, 24000) < 1e-5f, "kill empties the delay");
        float step = 0; for (size_t i = 1; i < 4800; ++i) step = std::max (step, std::abs (oL[i] - oL[i-1]));
        CHECK (step < 0.1f, "kill fades without a click");
        k.run (t, t, oL, oR);
        k.run (z, z, oL, oR);
        CHECK (rms (oL, 4800, 9600) > 0.1f, "echoes work again after a kill");
    }

    std::printf("29) User presets: save, list, load, delete\n");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("skell_test_presets");
        dir.deleteRecursively();
        DelayAudioProcessor::setUserPresetDirectory (dir);
        Rig r;
        r.set ("FEEDBACK", 0.33f); r.set ("TAPE", 0.7f);
        CHECK (r.proc.saveUserPreset ("Dub/My Echo"), "save with a category");
        CHECK (r.proc.saveUserPreset ("Plain"), "save without a category");
        auto files = r.proc.getUserPresetFiles();
        CHECK (files.size() == 2, "both appear in the list");
        r.set ("FEEDBACK", 0.9f); r.set ("TAPE", 0.0f);
        const auto mine = dir.getChildFile ("Dub").getChildFile ("My Echo.skellpreset");
        CHECK (r.proc.loadUserPreset (mine), "load");
        CHECK (std::abs (r.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.33f) < 1e-4f
            && std::abs (r.proc.apvts.getRawParameterValue ("TAPE")->load() - 0.7f) < 1e-4f, "values restored");
        CHECK (r.proc.getCurrentPresetName() == "My Echo" && r.proc.getCurrentUserPresetPath() == "Dub/My Echo.skellpreset", "name and path tracked");
        r.proc.setCurrentProgram (2);
        CHECK (r.proc.getCurrentUserPresetPath().isEmpty() && r.proc.getCurrentPresetName() == r.proc.getProgramName (2), "factory preset clears the user path");
        CHECK (r.proc.deleteUserPreset (mine) && r.proc.getUserPresetFiles().size() == 1, "delete");
        CHECK (! r.proc.saveUserPreset ("  / "), "empty names are refused");
        dir.deleteRecursively();
        DelayAudioProcessor::setUserPresetDirectory (juce::File());
    }

    std::printf("30) Auto-pan sweeps the echoes left/right in time\n");
    {
        Rig r; r.set ("DELAY_TIME", 20.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
        r.set ("AUTOPAN", 3.0f); r.set ("AP_DEPTH", 1.0f);          // 1/4 note cycle = 24000 samples at 120 bpm
        r.proc.prepareToPlay (SR, 512);
        const int N = 48000;
        std::vector<float> t ((size_t) N); for (int i = 0; i < N; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
        r.run (t, t, oL, oR);
        const float l1 = rms (oL, 2000, 4500), r1 = rms (oR, 2000, 4500);        // first quarter: LFO positive -> right
        const float l2 = rms (oL, 14500, 17000), r2 = rms (oR, 14500, 17000);    // third quarter: LFO negative -> left
        std::printf("   early: L %.3f R %.3f    late: L %.3f R %.3f\n", l1, r1, l2, r2);
        CHECK (r1 > 2.0f * l1 && l2 > 2.0f * r2, "echoes swing right then left");
        Rig q; q.set ("DELAY_TIME", 20.0f); q.set ("FEEDBACK", 0.0f); q.set ("DRY", 0.0f); q.set ("SMOOTH", 0.0f); q.set ("AUTOPAN", 3.0f); q.set ("AP_DEPTH", 0.0f);
        q.proc.prepareToPlay (SR, 512);
        q.run (t, t, oL, oR);
        CHECK (std::abs (rms (oL, 2000, 4500) - rms (oR, 2000, 4500)) < 1e-4f, "depth 0 leaves the image centred");
        for (int shape = 1; shape <= 2; ++shape)
        {
            Rig s; s.set ("DELAY_TIME", 20.0f); s.set ("DRY", 0.0f); s.set ("AUTOPAN", 4.0f); s.set ("MOD_SHAPE", (float) shape);
            s.proc.prepareToPlay (SR, 512);
            s.run (t, t, oL, oR);
            bool fin = true; for (size_t i = 0; i < oL.size(); ++i) fin = fin && std::isfinite (oL[i]) && std::isfinite (oR[i]);
            CHECK (fin, "triangle / random shapes are finite");
        }
    }

    std::printf("31) Limited repeats: exactly N echoes, then silence\n");
    {
        const int N = 96000;
        std::vector<float> imp ((size_t) N, 0.0f); for (int i = 0; i < 10; ++i) imp[(size_t) (100 + i)] = 0.5f;
        auto count = [&] (float repeatsIdx, std::vector<float>& o)
        {
            Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.9f); r.set ("DRY", 0.0f); r.set ("SMOOTH", 0.0f);
            r.set ("REPEATS", repeatsIdx);
            r.proc.prepareToPlay (SR, 512);
            std::vector<float> o2;
            r.run (imp, imp, o, o2);
            return (int) peaks (o, 0.01f).size();
        };
        std::vector<float> o;
        const int c3 = count (3.0f, o);
        std::printf("   REPEATS=3 -> %d echoes\n", c3);
        CHECK (c3 == 3, "3 echoes");
        CHECK (rms (o, 20000, 90000) < 1e-4f, "silent after the last echo");
        const int c1 = count (1.0f, o);  CHECK (c1 == 1, "1 echo");
        const int c8 = count (8.0f, o);  CHECK (c8 == 8, "8 echoes");
        const int c0 = count (0.0f, o);  std::printf("   Off -> %d echoes\n", c0);
        CHECK (c0 > 8, "Off keeps repeating");
        // sustained input keeps the loop open, tail then stops
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.9f); r.set ("DRY", 0.0f); r.set ("REPEATS", 2.0f);
        r.proc.prepareToPlay (SR, 512);
        std::vector<float> t ((size_t) N, 0.0f); for (int i = 0; i < 24000; ++i) t[(size_t) i] = 0.3f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
        std::vector<float> a, b; r.run (t, t, a, b);
        CHECK (rms (a, 8000, 20000) > 0.05f && rms (a, 60000, 95000) < 1e-4f, "sustained sound rings, then stops after N echoes");
    }

    std::printf("32) Tape hiss is gated by the echoes\n");
    {
        Rig r; r.set ("TAPE", 1.0f); r.set ("DRY", 0.0f); r.set ("FEEDBACK", 0.0f); r.set ("DELAY_TIME", 100.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = 96000;
        std::vector<float> t ((size_t) N, 0.0f); for (int i = 0; i < 2000; ++i) t[(size_t) i] = 0.4f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
        std::vector<float> a, b; r.run (t, t, a, b);
        const float during = rms (a, 4900, 6700), silent = rms (a, 60000, 95000);
        std::printf("   during echo %.5f  in silence %.7f\n", during, silent);
        CHECK (silent < 3e-5f, "no audible hiss on silence (was ~1e-3 ungated)");
        CHECK (during > 0.05f, "echo still plays with tape on");
    }

    std::printf("33) Tempo-elastic: delay follows performance speed\n");
    {
        auto runRate = [&] (float perSec, float amount)
        {
            Rig r; r.set ("DELAY_TIME", 200.0f); r.set ("ELASTIC", amount); r.set ("DRY", 0.0f);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (14 * SR);
            std::vector<float> in ((size_t) N, 0.0f);
            const int period = (int) (SR / perSec);
            for (int s = 0; s < N; s += period)
                for (int i = 0; i < (int) (0.02 * SR) && s + i < N; ++i)
                    in[(size_t) (s + i)] = 0.5f * std::sin (2 * 3.14159265358979 * 200.0 * i / SR);
            std::vector<float> a, b; r.run (in, in, a, b);
            return r.proc.getEffectiveDelayMs();
        };
        const float fast = runRate (8.0f, 1.0f), slow = runRate (1.5f, 1.0f), neutral = runRate (4.0f, 1.0f), off = runRate (8.0f, 0.0f);
        std::printf("   fast(8/s) %.1f ms  neutral(4/s) %.1f ms  slow(1.5/s) %.1f ms  off %.1f ms\n", fast, neutral, slow, off);
        CHECK (fast < 130.0f && fast > 95.0f, "fast delivery tightens to ~0.5x (limit)");
        CHECK (neutral > 170.0f && neutral < 230.0f, "neutral delivery leaves time alone");
        CHECK (slow > 330.0f && slow <= 401.0f, "slow delivery widens, limited to 2x");
        CHECK (std::abs (off - 200.0f) < 0.5f, "amount 0 = no change");
    }

    std::printf("34) Pocket meter: measures words against the grid and nudges them\n");
    {
        struct PH : juce::AudioPlayHead
        {
            double bpm = 120.0, ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true); return p;
            }
        };
        auto trial = [&] (double lateMs, float amt, float& measured, float& residualMs)
        {
            Rig r; PH ph; r.proc.setPlayHead (&ph);
            r.set ("POCKET", 1.0f); r.set ("POCKET_AMT", amt); r.set ("DRY", 1.0f); r.set ("WET", 0.0f);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (8 * SR);
            std::vector<float> in ((size_t) N, 0.0f);
            std::vector<int> starts;
            for (double beat = 1.0; beat < 15.0; beat += 1.0)     // one word per beat, 1/16 grid points
            {
                const int s = (int) ((beat * 0.5 + lateMs * 0.001) * SR);
                if (s + 5000 > N) break;
                starts.push_back (s);
                for (int i = 0; i < 4800; ++i) in[(size_t) (s + i)] = 0.5f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
            }
            std::vector<float> out ((size_t) N, 0.0f);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < N; pos += 512)
            {
                const int len = std::min (512, N - pos);
                ph.ppq = pos / SR * 2.0;                     // 120 bpm = 2 beats per second
                juce::AudioBuffer<float> buf (2, len);
                for (int i = 0; i < len; ++i) { buf.setSample (0, i, in[(size_t) (pos + i)]); buf.setSample (1, i, in[(size_t) (pos + i)]); }
                r.proc.processBlock (buf, midi);
                for (int i = 0; i < len; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
            }
            measured = r.proc.getPocketError (1);
            const int lat = r.proc.getLatencySamples();
            double sum = 0; int cnt = 0;
            for (size_t k = 2; k < starts.size(); ++k)
            {
                const int grid = (int) ((double) starts[k] - lateMs * 0.001 * SR);
                int o = grid; while (o < N && std::abs (out[(size_t) o]) < 0.1f) ++o;
                sum += ((double) (o - grid - lat)) / SR * 1000.0; ++cnt;
            }
            residualMs = cnt ? (float) (sum / cnt) : 999.0f;
        };
        float m, res;
        trial (20.0, 0.0f, m, res);
        std::printf("   late 20 ms, meter only: measured %.1f ms, output offset %.1f ms\n", m, res);
        CHECK (std::abs (m - 20.0f) < 4.0f, "late word measured about +20 ms");
        CHECK (std::abs (res - 20.0f) < 4.0f, "meter-only leaves the audio untouched");
        trial (20.0, 1.0f, m, res);
        std::printf("   late 20 ms, nudge 100%%: output offset %.1f ms\n", res);
        CHECK (std::abs (res) < 4.0f, "late word pulled onto the grid");
        trial (-15.0, 1.0f, m, res);
        std::printf("   early 15 ms, nudge 100%%: measured %.1f, output offset %.1f ms\n", m, res);
        CHECK (std::abs (m + 15.0f) < 4.0f && std::abs (res) < 4.0f, "early word measured negative and pushed onto the grid");
        trial (20.0, 0.5f, m, res);
        std::printf("   late 20 ms, nudge 50%%: output offset %.1f ms\n", res);
        CHECK (std::abs (res - 10.0f) < 4.0f, "50% nudge moves it halfway");
    }

    std::printf("35) Fuzz: random params, rates, block sizes; NaN recovery; state round trip\n");
    {
        juce::Random rnd (1234);
        bool allFinite = true; float worst = 0.0f; int blocks = 0;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        for (int round = 0; round < 12; ++round)
        {
            DelayAudioProcessor p; const double rate = rates[round % 5];
            p.setRateAndBufferSizeDetails (rate, 512); p.prepareToPlay (rate, 512);
            struct PH : juce::AudioPlayHead
            {
                double ppq = 0.0, bpm = 120.0; bool playing = true;
                juce::Optional<PositionInfo> getPosition() const override { PositionInfo q; q.setBpm (bpm); q.setPpqPosition (ppq); q.setIsPlaying (playing); return q; }
            } ph;
            p.setPlayHead (&ph);
            double samplePos = 0.0;
            for (int step = 0; step < 40; ++step)
            {
                for (auto* prm : p.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                        if (rp->paramID != "BYPASS" || rnd.nextInt (4) == 0)
                            rp->setValueNotifyingHost (rnd.nextFloat());
                ph.bpm = 40.0 + rnd.nextFloat() * 220.0; ph.playing = rnd.nextInt (5) != 0;
                const int sizes[] = { 1, 7, 64, 333, 512, 4097, 9000 };
                const int len = sizes[rnd.nextInt (7)];
                juce::AudioBuffer<float> buf (2, len); juce::MidiBuffer midi;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < len; ++i)
                        buf.setSample (c, i, rnd.nextInt (3) == 0 ? 0.0f : (rnd.nextFloat() * 2.0f - 1.0f) * 0.8f);
                ph.ppq = samplePos / rate * ph.bpm / 60.0; samplePos += len;
                p.processBlock (buf, midi); ++blocks;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < len; ++i)
                    {
                        const float v = buf.getSample (c, i);
                        if (! std::isfinite (v)) allFinite = false;
                        worst = std::max (worst, std::abs (v));
                    }
            }
        }
        std::printf("   %d blocks, worst peak %.2f\n", blocks, worst);
        CHECK (allFinite, "fuzz output finite");
        CHECK (worst < 16.0f, "fuzz output bounded");

        // NaN / inf on the input must not poison the delay forever
        Rig r; r.set ("FEEDBACK", 0.8f); r.set ("DELAY_TIME", 120.0f); r.set ("TAPE", 0.5f); r.set ("SHIMMER", 0.4f);
        const int N = (int) (6 * SR);
        std::vector<float> in ((size_t) N, 0.0f);
        for (int i = 0; i < 4000; ++i) in[(size_t) (20000 + i)] = (i % 2) ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        for (int i = 0; i < 24000; ++i) in[(size_t) (150000 + i)] = 0.3f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
        std::vector<float> a, b; r.run (in, in, a, b);
        bool fin = true; for (int i = 260000; i < N; ++i) if (! std::isfinite (a[(size_t) i])) fin = false;
        std::printf("   NaN burst: tail finite = %d, level after = %.3f\n", (int) fin, rms (a, 160000, 170000));
        CHECK (fin, "delay recovers from NaN/inf input");
        CHECK (rms (a, 160000, 170000) > 0.02f, "and still produces echoes afterwards");

        // state round trip incl. the newest parameters
        Rig s1; s1.set ("ELASTIC", 0.7f); s1.set ("POCKET", 1.0f); s1.set ("POCKET_AMT", 0.3f); s1.set ("REPEATS", 4.0f);
        juce::MemoryBlock mb; s1.proc.getStateInformation (mb);
        Rig s2; s2.proc.setStateInformation (mb.getData(), (int) mb.getSize());
        auto gv = [] (Rig& q, const char* id) { return q.proc.apvts.getRawParameterValue (id)->load(); };
        CHECK (std::abs (gv (s2, "ELASTIC") - 0.7f) < 1e-3f && gv (s2, "POCKET") > 0.5f && std::abs (gv (s2, "POCKET_AMT") - 0.3f) < 1e-3f && std::abs (gv (s2, "REPEATS") - 4.0f) < 1e-3f, "state restores new params");
    }

    std::printf("36) Odd buffers: mono buffer, empty buffer, re-prepare mid-stream\n");
    {
        Rig r; r.set ("POCKET", 1.0f); r.set ("POCKET_AMT", 0.5f); r.set ("TAPE", 0.5f); r.set ("SHIMMER", 0.3f); r.set ("REVERSE", 1.0f);
        juce::MidiBuffer midi; bool fin = true;
        for (int k = 0; k < 40; ++k)
        {
            juce::AudioBuffer<float> mono (1, 300);
            for (int i = 0; i < 300; ++i) mono.setSample (0, i, 0.4f * std::sin (0.05f * (float) (i + k * 300)));
            r.proc.processBlock (mono, midi);
            for (int i = 0; i < 300; ++i) if (! std::isfinite (mono.getSample (0, i))) fin = false;
            juce::AudioBuffer<float> none (2, 0);  r.proc.processBlock (none, midi);
            if (k == 20) { r.proc.prepareToPlay (96000.0, 256); }
            juce::AudioBuffer<float> st (2, 256);
            for (int i = 0; i < 256; ++i) { st.setSample (0, i, 0.3f); st.setSample (1, i, -0.3f); }
            r.proc.processBlock (st, midi);
            for (int i = 0; i < 256; ++i) if (! std::isfinite (st.getSample (0, i)) || ! std::isfinite (st.getSample (1, i))) fin = false;
        }
        CHECK (fin, "mono / empty / re-prepared buffers stay finite");
    }

    std::printf("37) Reverse never runs away (regression: heads drifting out of step doubled the loop gain)\n");
    {
        bool ok = true;
        for (float dt : { 60.0f, 125.0f, 500.0f })
        {
            Rig r; r.set ("DELAY_TIME", dt); r.set ("FEEDBACK", 0.7f); r.set ("REVERSE", 1.0f); r.set ("WET", 1.0f);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (30 * SR);
            std::vector<float> in ((size_t) N, 0.0f); juce::Random rn (7);
            for (int i = 0; i < (int) (0.4 * SR); ++i) in[(size_t) i] = (rn.nextFloat() * 2 - 1) * 0.3f;
            std::vector<float> a, b; r.run (in, in, a, b);
            const float tail = rms (a, (int) (24 * SR), N);
            std::printf("   dt %.0f ms: tail rms after 24 s = %.2e\n", dt, tail);
            if (tail > 1e-4f) ok = false;
        }
        // changing the time while reversing (this is what pulled the heads apart)
        Rig r; r.set ("FEEDBACK", 0.7f); r.set ("REVERSE", 1.0f); r.set ("WET", 1.0f); r.set ("SMOOTH", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        juce::Random rn (9); juce::MidiBuffer midi; float tailPk = 0.0f;
        for (int blk = 0; blk < (int) (40 * SR / 512); ++blk)
        {
            if (blk % 20 == 0 && blk < 400) r.set ("DELAY_TIME", 50.0f + rn.nextFloat() * 600.0f);
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i) { const float x = blk < 400 ? (rn.nextFloat() * 2 - 1) * 0.3f : 0.0f; buf.setSample (0, i, x); buf.setSample (1, i, x); }
            r.proc.processBlock (buf, midi);
            if (blk > (int) (35 * SR / 512)) for (int i = 0; i < 512; ++i) tailPk = std::max (tailPk, std::abs (buf.getSample (0, i)));
        }
        std::printf("   after 400 random time changes, tail peak at 35 s: %.2e\n", tailPk);
        CHECK (ok && tailPk < 1e-3f, "reverse echoes decay normally, also after repeated time changes");
    }

    std::printf("38) Telepathy: a faint ghost appears just before the next word, only once a rhythm is known\n");
    {
        auto render = [&] (float amt, std::vector<float>& out)
        {
            Rig r; r.set ("DELAY_TIME", 5.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f); r.set ("TELEPATHY", amt);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (6 * SR); std::vector<float> in ((size_t) N, 0.0f);
            const double starts[] = { 0.5, 0.75, 1.0, 1.375, 3.0, 3.25, 3.5, 3.875 };
            for (double s : starts)
                for (int i = 0; i < (int) (0.06 * SR); ++i) in[(size_t) (s * SR) + (size_t) i] = 0.5f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR) * std::exp (-(float) i / (0.02f * (float) SR));
            std::vector<float> b; r.run (in, in, out, b);
        };
        std::vector<float> on, off, diff; render (1.0f, on); render (0.0f, off);
        for (size_t k = 0; k < on.size(); ++k) diff.push_back (on[k] - off[k]);     // what telepathy adds
        auto win = [&] (const std::vector<float>& v, double a, double b2) { return rms (v, (int) (a * SR), (int) (b2 * SR)); };
        const float g1 = win (diff, 0.0, 2.5);
        const float g2 = win (diff, 3.12, 3.24), g3 = win (diff, 3.37, 3.49), g4 = win (diff, 3.60, 3.86);
        std::printf("   added in first phrase: %.6f   second phrase before onset 2/3/4: %.5f %.5f %.5f\n", g1, g2, g3, g4);
        CHECK (g1 < 1e-5f, "no ghost before there is any rhythm to learn from");
        CHECK (g2 > 5e-4f && g3 > 5e-4f && g4 > 5e-4f, "ghosts appear before each predicted onset in the second phrase");
        CHECK (g2 < 0.1f && g3 < 0.1f && g4 < 0.1f, "and the ghosts are faint");
        CHECK (win (diff, 3.2495, 3.2545) < 1e-4f, "ghost stops before the real word arrives");
        CHECK (win (off, 3.25, 3.31) > 0.05f && std::abs (win (on, 3.25, 3.31) - win (off, 3.25, 3.31)) < 0.02f, "the real word comes through unchanged");
        float peakAdded = 0.0f; for (float v : diff) peakAdded = std::max (peakAdded, std::abs (v));
        CHECK (peakAdded < 0.25f, "ghost never louder than a quarter of the word");
    }

    std::printf("39) Echo ecology: organic tails, bloom in a quiet room, eaten when something plays\n");
    {
        float lastDecayRatio = 0.0f;
        auto tailRms = [&] (float eco, bool keepPlaying, float& peak)
        {
            Rig r; r.set ("DELAY_TIME", 250.0f); r.set ("FEEDBACK", 0.6f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f); r.set ("ECOLOGY", eco);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (16 * SR); std::vector<float> in ((size_t) N, 0.0f);
            for (int i = 0; i < (int) (0.5 * SR); ++i) in[(size_t) (SR + i)] = 0.4f * std::sin (2 * 3.14159265358979 * 300.0 * i / SR);
            if (keepPlaying) for (int i = (int) (2.0 * SR); i < N; ++i) in[(size_t) i] = 0.25f * std::sin (2 * 3.14159265358979 * 220.0 * i / SR);
            std::vector<float> a, b; r.run (in, in, a, b);
            peak = 0.0f; for (float v : a) peak = std::max (peak, std::abs (v));
            const float early = rms (a, (int) (4 * SR), (int) (6 * SR));
            lastDecayRatio = rms (a, (int) (11 * SR), (int) (13 * SR)) / std::max (1e-12f, rms (a, (int) (8 * SR), (int) (10 * SR)));
            return early;
        };
        float pk = 0.0f, pkMax = 0.0f;
        const int runs = 8;
        float base = tailRms (0.0f, false, pk), eco = 0.0f, lo = 1e9f, hi = 0.0f;
        const float baseRatio = lastDecayRatio; float ecoRatio = 0.0f;
        for (int k = 0; k < runs; ++k) { const float v = tailRms (0.5f, false, pk); eco += v / runs; ecoRatio += lastDecayRatio / runs; lo = std::min (lo, v); hi = std::max (hi, v); pkMax = std::max (pkMax, pk); }
        std::printf("   quiet room: tail rms 4-6 s off %.5f / ecology %.5f; how much of the tail survives each 2 s in the quiet: off %.4f, ecology %.4f\n", base, eco, baseRatio, ecoRatio);
        CHECK (std::isfinite (eco) && pkMax < 2.0f, "finite and bounded");
        CHECK (ecoRatio > baseRatio * 1.3f, "once the room is quiet the echoes bloom (they fade more slowly)");
        CHECK ((hi - lo) / hi > 0.05f, "tails are organic: no two runs are the same");
        float busyOff = tailRms (0.0f, true, pk), busyOn = 0.0f;
        for (int k = 0; k < runs; ++k) busyOn += tailRms (0.5f, true, pk) / runs;
        std::printf("   something playing, wet rms 4-6 s: off %.4f, ecology mean %.4f\n", busyOff, busyOn);
        CHECK (busyOn < busyOff * 0.95f, "echoes get eaten while something else is playing");
        float full = 0.0f; for (int k = 0; k < 4; ++k) { tailRms (1.0f, k & 1, pk); full = std::max (full, pk); }
        CHECK (full < 2.0f, "full ecology stays bounded");
        // off must be exactly neutral
        float p1, p2; const float a1 = tailRms (0.0f, false, p1), a2 = tailRms (0.0f, false, p2);
        CHECK (a1 == a2, "ecology off is deterministic and untouched");
    }


    // ---- helpers for the second batch of tests
    const double TWO_PI = 2 * 3.14159265358979;
    auto runSegs = [&] (Rig& r, const std::vector<float>& in, const std::vector<std::pair<int, std::function<void()>>>& events, std::vector<float>& out)
    {
        out.assign (in.size(), 0.0f);
        size_t pos = 0;
        std::vector<std::pair<int, std::function<void()>>> ev = events;
        ev.push_back ({ (int) in.size(), [] {} });
        for (auto& e : ev)
        {
            const size_t end = (size_t) e.first;
            if (end > pos)
            {
                std::vector<float> a (in.begin() + (long) pos, in.begin() + (long) end), o, o2;
                r.run (a, a, o, o2);
                std::copy (o.begin(), o.end(), out.begin() + (long) pos);
                pos = end;
            }
            e.second();
        }
    };
    auto zeroCrossHz = [&] (const std::vector<float>& x, double a, double b)
    {
        int c = 0; for (size_t i = (size_t) (a * SR) + 1; i < (size_t) (b * SR); ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++c;
        return 0.5 * c / (b - a);
    };

    std::printf("40) Spectral-flux onset detector: finds soft starts on a noisy bed, ignores steady sounds, known lag\n");
    {
        auto burst = [&] (std::vector<float>& x, double t0, float amp, double f)
        {
            for (int i = 0; i < (int) (0.12 * SR); ++i)
            {
                const float e = std::min (1.0f, (float) i / (0.004f * (float) SR)) * std::exp (-(float) i / (0.08f * (float) SR));
                x[(size_t) (t0 * SR) + (size_t) i] += amp * e * (0.6f * std::sin ((float) (TWO_PI * f * i / SR)) + 0.4f * std::sin ((float) (TWO_PI * 2.1 * f * i / SR)));
            }
        };
        const int N = (int) (6 * SR);
        juce::Random rnd (7);
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            std::vector<float> x ((size_t) N, 0.0f);
            const float bed = scenario == 0 ? 0.0f : (scenario == 1 ? 0.02f : 0.05f);
            for (auto& v : x) v = bed * (rnd.nextFloat() * 2 - 1);
            const double t[] = { 0.5, 1.0, 1.4, 2.2, 3.0, 3.4, 4.1, 5.0 };
            const float amp = scenario == 2 ? 0.25f : 0.4f;
            for (double ti : t) burst (x, ti, amp, 250.0 + 40.0 * (double) scenario);
            FluxOnsetDetector det; det.prepare (SR);
            std::vector<unsigned char> fl ((size_t) N);
            det.process (x.data(), N, fl.data());
            int found = 0, extra = 0; double lagSum = 0;
            std::vector<int> hit; for (int i = 0; i < N; ++i) if (fl[(size_t) i]) hit.push_back (i);
            for (double ti : t) { for (int h : hit) if (h >= (int) (ti * SR) && h < (int) ((ti + 0.05) * SR)) { ++found; lagSum += (h - ti * SR) * 1000.0 / SR; break; } }
            extra = (int) hit.size() - found;
            std::printf("   bed %.2f: found %d/8, extra %d, mean lag %.1f ms\n", bed, found, extra, found ? lagSum / found : 0.0);
            CHECK (found == 8, "every onset found");
            CHECK (extra == 0, "no false onsets");
            CHECK (found && std::abs (lagSum / found - FluxOnsetDetector::kLagMs) < 5.0, "lag matches the compensation constant");
        }
        std::vector<float> steady ((size_t) N), noise ((size_t) N);
        for (int i = 0; i < N; ++i) { steady[(size_t) i] = 0.5f * std::sin ((float) (TWO_PI * 440.0 * i / SR)); noise[(size_t) i] = 0.3f * (rnd.nextFloat() * 2 - 1); }
        FluxOnsetDetector d1, d2; d1.prepare (SR); d2.prepare (SR);
        std::vector<unsigned char> f1 ((size_t) N), f2 ((size_t) N);
        d1.process (steady.data(), N, f1.data());  d2.process (noise.data(), N, f2.data());
        int c1 = 0, c2 = 0; for (int i = (int) SR; i < N; ++i) { c1 += f1[(size_t) i]; c2 += f2[(size_t) i]; }
        CHECK (c1 == 0 && c2 == 0, "steady tone and steady noise never trigger");
    }

    std::printf("41) Granular echoes\n");
    {
        auto render = [&] (float g, float fb, int seconds, std::vector<float>& out)
        {
            Rig r; r.set ("DELAY_TIME", 200.0f); r.set ("FEEDBACK", fb); r.set ("DRY", 0.0f); r.set ("WET", 1.0f); r.set ("GRAIN", g);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (seconds * SR); std::vector<float> in ((size_t) N, 0.0f), b;
            for (int i = 0; i < (int) (0.4 * SR); ++i) in[(size_t) (0.1 * SR) + (size_t) i] = 0.4f * std::sin ((float) (TWO_PI * 330.0 * i / SR));
            r.run (in, in, out, b);
        };
        std::vector<float> off, on, a1, a2;
        render (0.0f, 0.3f, 3, off);  render (0.8f, 0.3f, 3, on);  render (0.0f, 0.3f, 3, a1);  render (0.0f, 0.3f, 3, a2);
        const float eo = rms (off, (int) (0.3 * SR), (int) (1.5 * SR)), en = rms (on, (int) (0.3 * SR), (int) (1.5 * SR));
        double diff = 0; for (size_t i = 0; i < off.size(); ++i) diff += std::abs (on[i] - off[i]);
        std::printf("   echo rms off %.4f grain %.4f\n", eo, en);
        CHECK (en > eo * 0.4f && en < eo * 1.6f, "grain keeps roughly the same level");
        CHECK (diff / (double) off.size() > 0.01, "grain changes the echoes");
        CHECK (a1 == a2, "grain off is deterministic and untouched");
        std::vector<float> long1; render (1.0f, 0.9f, 40, long1);
        float pk = 0; for (float v : long1) pk = std::max (pk, std::abs (v));
        CHECK (std::isfinite (pk) && pk < 2.0f, "full grain with 90 % feedback stays bounded");
    }

    std::printf("42) Spectral delay: lows later, highs earlier\n");
    {
        auto firstAbove = [&] (const std::vector<float>& x, float thr, int from) { for (size_t i = (size_t) from; i < x.size(); ++i) if (std::abs (x[i]) > thr) return (int) i; return -1; };
        auto render = [&] (float sp, double f, std::vector<float>& out)
        {
            Rig r; r.set ("DELAY_TIME", 400.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f); r.set ("SPECTRAL", sp);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (2 * SR); std::vector<float> in ((size_t) N, 0.0f), b;
            for (int i = 0; i < (int) (0.05 * SR); ++i) in[(size_t) (0.1 * SR) + (size_t) i] = 0.4f * std::sin ((float) (TWO_PI * f * i / SR)) * std::sin ((float) (3.14159265 * i / (0.05 * SR)));
            r.run (in, in, out, b);
        };
        std::vector<float> lo0, hi0, lo1, hi1;
        render (0.0f, 150.0, lo0); render (0.0f, 4000.0, hi0); render (1.0f, 150.0, lo1); render (1.0f, 4000.0, hi1);
        const int l0 = firstAbove (lo0, 0.2f, 0), h0 = firstAbove (hi0, 0.2f, 0), l1 = firstAbove (lo1, 0.2f, 0), h1 = firstAbove (hi1, 0.2f, 0);
        std::printf("   spectral 0: low at %d high at %d   spectral 1: low at %d high at %d (samples)\n", l0, h0, l1, h1);
        CHECK (l0 > 0 && std::abs (l0 - h0) < 200, "off: all frequencies arrive together");
        CHECK (l1 > h1 + (int) (0.15 * SR), "full: low band arrives well after the high band");
        CHECK (rms (lo1, 0, (int) (2 * SR)) > 0.4f * rms (lo0, 0, (int) (2 * SR)) && rms (hi1, 0, (int) (2 * SR)) > 0.4f * rms (hi0, 0, (int) (2 * SR)), "both bands keep their level");
    }

    std::printf("43) Stutter / 44) Tape stop / 45) Reverse throw performance buttons\n");
    {
        Rig r; r.set ("DELAY_TIME", 400.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = (int) (3 * SR); std::vector<float> in ((size_t) N, 0.0f), out;
        juce::Random rn (3);
        for (int i = 0; i < (int) (0.3 * SR); ++i) in[(size_t) (0.1 * SR) + (size_t) i] = 0.4f * (rn.nextFloat() * 2 - 1);
        runSegs (r, in, { { (int) (0.55 * SR), [&] { r.set ("STUTTER", 1.0f); } }, { (int) (1.6 * SR), [&] { r.set ("STUTTER", 0.0f); } } }, out);
        const float held = rms (out, (int) (1.0 * SR), (int) (1.5 * SR)), after = rms (out, (int) (2.0 * SR), (int) (3.0 * SR));
        float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
        std::printf("   stutter held rms %.4f, after release %.5f, peak %.3f\n", held, after, pk);
        CHECK (held > 0.05f, "stutter keeps looping a slice after the echo would have ended");
        CHECK (after < 0.002f, "and stops cleanly on release");
        CHECK (pk < 1.0f, "stutter stays bounded");
    }
    {
        Rig r; r.set ("DELAY_TIME", 100.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = (int) (5 * SR); std::vector<float> in ((size_t) N), out;
        for (int i = 0; i < N; ++i) in[(size_t) i] = 0.4f * std::sin ((float) (TWO_PI * 1000.0 * i / SR));
        runSegs (r, in, { { (int) (1.0 * SR), [&] { r.set ("TAPESTOP", 1.0f); } }, { (int) (3.0 * SR), [&] { r.set ("TAPESTOP", 0.0f); } } }, out);
        const double f0 = zeroCrossHz (out, 0.5, 0.9), f1 = zeroCrossHz (out, 1.15, 1.35);
        const float stopped = rms (out, (int) (2.0 * SR), (int) (2.9 * SR)), back = rms (out, (int) (4.0 * SR), (int) (5.0 * SR));
        float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
        std::printf("   tape stop: pitch %.0f Hz -> %.0f Hz, stopped rms %.5f, recovered rms %.4f (steady %.4f), f after %.0f\n", f0, f1, stopped, back, rms (out, (int) (0.5 * SR), (int) (0.9 * SR)), zeroCrossHz (out, 4.2, 4.8));
        CHECK (f0 > 950 && f0 < 1050, "normal pitch before");
        CHECK (f1 < f0 * 0.9, "pitch falls as the tape slows");
        CHECK (stopped < 0.005f, "silence once stopped");
        CHECK (std::abs (back - rms (out, (int) (0.5 * SR), (int) (0.9 * SR))) < 0.03f && std::abs (zeroCrossHz (out, 4.2, 4.8) - 1000) < 40, "spins back to normal pitch and level");
        CHECK (pk < 1.0f, "tape stop stays bounded");
    }
    {
        auto render = [&] (bool throwIt, std::vector<float>& out)
        {
            Rig r; r.set ("DELAY_TIME", 300.0f); r.set ("FEEDBACK", 0.0f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f);
            r.proc.prepareToPlay (SR, 512);
            const int N = (int) (3 * SR); std::vector<float> in ((size_t) N, 0.0f);
            for (double t = 0.1; t < 2.9; t += 0.5)
                for (int i = 0; i < (int) (0.3 * SR); ++i) in[(size_t) (t * SR) + (size_t) i] = 0.4f * std::exp (-(float) i / (0.08f * (float) SR)) * std::sin ((float) (TWO_PI * 400.0 * i / SR));
            runSegs (r, in, { { (int) (1.2 * SR), [&] { if (throwIt) r.set ("REVTHROW", 1.0f); } }, { (int) (1.25 * SR), [&] { r.set ("REVTHROW", 0.0f); } } }, out);
        };
        std::vector<float> a, b; render (false, a); render (true, b);
        auto dr = [&] (double t0, double t1) { double s = 0; for (size_t i = (size_t) (t0 * SR); i < (size_t) (t1 * SR); ++i) s += (double) (a[i] - b[i]) * (a[i] - b[i]); return (float) std::sqrt (s / ((t1 - t0) * SR)); };
        std::printf("   reverse throw: difference rms 1.2-1.5 s %.4f, before %.6f, after 1.8-3.0 s %.6f\n", dr (1.2, 1.5), dr (0.0, 1.19), dr (1.8, 3.0));
        CHECK (dr (1.2, 1.5) > 0.01f, "throw changes the output while it plays");
        CHECK (dr (0.0, 1.19) < 1e-6f && dr (1.8, 3.0) < 1e-6f, "and leaves everything before and after untouched");
        float pk = 0; for (float v : b) pk = std::max (pk, std::abs (v));
        CHECK (pk < 1.0f, "reverse throw stays bounded");
    }

    std::printf("46) Echo trail telemetry / 47) Learn tempo from the performance\n");
    {
        Rig r; r.set ("DELAY_TIME", 200.0f); r.set ("FEEDBACK", 0.5f); r.set ("DRY", 0.0f); r.set ("WET", 1.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = (int) (3 * SR); std::vector<float> in ((size_t) N, 0.0f), a, b;
        for (int i = 0; i < (int) (0.05 * SR); ++i) in[(size_t) i] = 0.8f * std::sin ((float) (TWO_PI * 500.0 * i / SR));
        r.run (in, in, a, b);
        std::printf("   trail (newest first):"); for (int k = 0; k < 8; ++k) std::printf(" %.3f", r.proc.getTrailLevel (k)); std::printf("\n");
        CHECK (r.proc.getTrailCount() >= 10, "one trail entry per delay period");
        const float l5 = r.proc.getTrailLevel (0);
        CHECK (l5 < 0.01f, "silence at the end of the trail");
        float first = 0, second = 0; int cnt = r.proc.getTrailCount();
        // find the loudest entries in order from the start of the burst
        std::vector<float> lv; for (int k = 0; k < std::min (cnt, 16); ++k) lv.push_back (r.proc.getTrailLevel (std::min (cnt, 16) - 1 - k));
        for (size_t k = 0; k + 1 < lv.size(); ++k) if (lv[k] > 0.05f && lv[k + 1] > 0.02f) { first = lv[k]; second = lv[k + 1]; break; }
        CHECK (first > 0 && std::abs (second / first - 0.5f) < 0.2f, "each trail entry is about half the one before (feedback 0.5)");
    }
    {
        Rig r; r.set ("DELAY_TIME", 200.0f); r.set ("DRY", 0.0f);
        r.proc.prepareToPlay (SR, 512);
        const int N = (int) (9 * SR); std::vector<float> in ((size_t) N, 0.0f), a, b;
        juce::Random rn (5);
        for (double t = 0.5; t < 3.2; t += 0.25)
            for (int i = 0; i < (int) (0.08 * SR); ++i) in[(size_t) (t * SR) + (size_t) i] = 0.4f * (rn.nextFloat() * 2 - 1) * std::exp (-(float) i / (0.03f * (float) SR));
        std::vector<float> first (in.begin(), in.begin() + (long) (3.4 * SR)), rest (in.begin() + (long) (3.4 * SR), in.end());
        r.run (first, first, a, b);
        const float learned = r.proc.getLearnedIntervalMs();
        std::printf("   learned interval %.0f ms (played every 250 ms)\n", learned);
        CHECK (std::abs (learned - 250.0f) < 40.0f, "learns the spacing of the performance");
        r.run (rest, rest, a, b);
        CHECK (r.proc.getLearnedIntervalMs() == 0.0f, "forgets it after a few seconds of silence");
    }

    std::printf("48) New features beaten with random settings and hostile input\n");
    {
        Rig r; r.proc.prepareToPlay (SR, 512);
        juce::Random rn (11);
        const char* ids[] = { "DELAY_TIME", "FEEDBACK", "GRAIN", "SPECTRAL", "REVERSE", "SHIMMER", "TAPE", "ECOLOGY", "TELEPATHY", "ELASTIC", "SMOOTH", "WET" };
        float pk = 0; bool finite = true;
        for (int round = 0; round < 200; ++round)
        {
            for (const char* id : ids) { auto* p = dynamic_cast<juce::RangedAudioParameter*> (r.proc.apvts.getParameter (id)); p->setValueNotifyingHost (rn.nextFloat()); }
            r.set ("STUTTER", rn.nextFloat() < 0.3f ? 1.0f : 0.0f);  r.set ("TAPESTOP", rn.nextFloat() < 0.3f ? 1.0f : 0.0f);  r.set ("REVTHROW", rn.nextFloat() < 0.5f ? 1.0f : 0.0f);
            r.set ("FEEDBACK", std::min (0.95f, rn.nextFloat()));
            std::vector<float> in (4800), a, b;
            for (auto& v : in) v = 0.5f * (rn.nextFloat() * 2 - 1);
            r.run (in, in, a, b);
            for (float v : a) { if (! std::isfinite (v)) finite = false; pk = std::max (pk, std::abs (v)); }
        }
        std::printf("   peak over 200 random rounds: %.3f\n", pk);
        CHECK (finite && pk < 8.0f, "always finite and bounded");
    }


    std::printf("49) Typed values for knobs\n");
    {
        SkellSlider pct, hz, ms, pan, sync;
        pct.setRange (0.0, 1.0, 0.01);  pct.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)); };
        hz.setRange (20.0, 20000.0, 1.0);  hz.setSkewFactorFromMidPoint (1000.0);
        hz.textFromValueFunction = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + "k" : juce::String (juce::roundToInt (v)); };
        ms.setRange (1.0, 2000.0, 0.01);  ms.textFromValueFunction = [] (double v) { return v < 100.0 ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
        pan.setRange (-1.0, 1.0, 0.01);
        pan.textFromValueFunction = [] (double v) { const int p = juce::roundToInt (std::abs (v) * 100.0); return p == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (p); };
        sync.setRange (0.0, 6.0, 1.0);
        sync.textFromValueFunction = [] (double v) { static const char* n[] = { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }; return juce::String (n[juce::jlimit (0, 6, juce::roundToInt (v))]); };
        double v = 0;
        CHECK (skellTypedToValue (pct, "35", v) && std::abs (v - 0.35) < 1e-6, "35 -> 35 %");
        CHECK (skellTypedToValue (pct, " 80% ", v) && std::abs (v - 0.8) < 1e-6, "80% with a percent sign and spaces");
        CHECK (skellTypedToValue (pct, "250", v) && v == 1.0, "values beyond the range are clamped");
        CHECK (skellTypedToValue (hz, "1.5k", v) && std::abs (v - 1500.0) < 1.0, "1.5k -> 1500 Hz");
        CHECK (skellTypedToValue (hz, "440 Hz", v) && std::abs (v - 440.0) < 1.0, "440 Hz");
        CHECK (skellTypedToValue (ms, "375 ms", v) && std::abs (v - 375.0) < 0.5, "375 ms");
        CHECK (skellTypedToValue (ms, "62.5", v) && std::abs (v - 62.5) < 0.2, "62.5 (no unit)");
        CHECK (skellTypedToValue (pan, "L30", v) && std::abs (v + 0.3) < 1e-3, "L30");
        CHECK (skellTypedToValue (pan, "r100", v) && std::abs (v - 1.0) < 1e-3, "r100");
        CHECK (skellTypedToValue (pan, "c", v) && v == 0.0, "c");
        CHECK (skellTypedToValue (sync, "1/16", v) && v == 2.0, "1/16 matches the note value");
        CHECK (skellTypedToValue (sync, "1/2", v) && v == 5.0, "1/2");
        CHECK (! skellTypedToValue (pct, "banana", v) && ! skellTypedToValue (pct, "", v), "nonsense is refused");
        pct.setValue (0.5);
        pct.setTooltip ("Feedback");  pct.unit = "%";
        CHECK (pct.getTooltip() == "Feedback\n50%", "tooltip = description + current value with unit");
    }

    std::printf("50) Undo / redo, A/B, presets, randomise, interface settings\n");
    {
        auto pump = [] (int ms)
        {
            std::thread t ([ms] { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); juce::MessageManager::getInstance()->stopDispatchLoop(); });
            juce::MessageManager::getInstance()->runDispatchLoop();
            t.join();
        };
        auto get = [] (Rig& r, const char* id) { return r.proc.apvts.getRawParameterValue (id)->load(); };
        {
            Rig r; r.proc.apvts.state.setProperty ("x", 0, nullptr);
            pump (200);
            r.proc.newUndoStep();  r.set ("FEEDBACK", 0.8f);  r.proc.syncStateToTree();  pump (50);
            r.proc.newUndoStep();  r.set ("FEEDBACK", 0.3f);  r.proc.syncStateToTree();  pump (50);
            CHECK (std::abs (get (r, "FEEDBACK") - 0.3f) < 1e-3f, "value set");
            r.proc.undoManager.undo();  pump (200);
            CHECK (std::abs (get (r, "FEEDBACK") - 0.8f) < 1e-3f, "undo goes back one step");
            r.proc.undoManager.redo();  pump (200);
            CHECK (std::abs (get (r, "FEEDBACK") - 0.3f) < 1e-3f, "redo comes forward again");
            r.proc.newUndoStep();  r.proc.applyPreset (6);  r.proc.syncStateToTree();  pump (50);   // Dub Echo
            const float dubTime = get (r, "DELAY_TIME");
            r.proc.undoManager.undo();  pump (300);
            CHECK (std::abs (dubTime - 375.0f) < 1.0f && std::abs (get (r, "FEEDBACK") - 0.3f) < 1e-3f && r.proc.getCurrentPresetName() != "Dub Echo", "a whole preset change undoes in one step");
        }
        {
            Rig r;
            pump (150);
            r.set ("DELAY_TIME", 111.0f);  r.set ("WET", 0.2f);  pump (150);
            r.proc.selectABSlot (1);                       // B starts as a copy of A
            r.set ("DELAY_TIME", 555.0f);  r.set ("WET", 0.9f);  pump (150);
            r.proc.selectABSlot (0);  pump (150);
            CHECK (std::abs (get (r, "DELAY_TIME") - 111.0f) < 0.5f && std::abs (get (r, "WET") - 0.2f) < 1e-3f, "A recalls its own settings");
            r.proc.selectABSlot (1);  pump (150);
            CHECK (std::abs (get (r, "DELAY_TIME") - 555.0f) < 0.5f && std::abs (get (r, "WET") - 0.9f) < 1e-3f, "B recalls its own settings");
        }
        {
            Rig r;
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("skell-test-presets-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
            DelayAudioProcessor::setUserPresetDirectory (dir.getChildFile ("Presets"));
            r.proc.saveUserPreset ("Mine/Sparkly");
            auto entries = r.proc.getPresetEntries();
            int factory = 0, user = 0; for (auto& e : entries) (e.user ? user : factory)++;
            CHECK (factory == (int) getFactoryPresets().size() && factory >= 23 && user == 1, "browser lists every factory preset and the saved user preset");
            bool dubTag = false, userTag = false;
            for (auto& e : entries) { if (e.name == "Dub Echo" && e.tags.contains ("dub")) dubTag = true; if (e.name == "Sparkly" && e.tags.contains ("user") && e.tags.contains ("mine")) userTag = true; }
            CHECK (dubTag && userTag, "tags: factory tags, and 'user' + folder for saved ones");
            const auto key = "F:Dub Echo";
            CHECK (! DelayAudioProcessor::isFavourite (key), "not a favourite at first");
            DelayAudioProcessor::setFavourite (key, true);
            CHECK (DelayAudioProcessor::isFavourite (key), "favourite remembered (file on disk)");
            DelayAudioProcessor::setFavourite (key, false);
            CHECK (! DelayAudioProcessor::isFavourite (key), "and forgotten");
            // load by entry
            r.proc.newUndoStep();
            for (auto& e : entries) if (e.name == "Dub Echo") r.proc.loadPresetEntry (e);
            CHECK (std::abs (get (r, "DELAY_TIME") - 375.0f) < 1.0f, "loading a browser entry applies the preset");
            for (auto& e : entries) if (e.user) r.proc.loadPresetEntry (e);
            CHECK (r.proc.getCurrentPresetName() == "Sparkly", "loading a user entry works");
            dir.deleteRecursively();
            DelayAudioProcessor::setUserPresetDirectory (juce::File());
        }
        {
            Rig r;
            r.proc.applyPreset (1);  pump (150);
            const float t0 = get (r, "DELAY_TIME"), w0 = get (r, "WET"), f0 = get (r, "FEEDBACK"), tape0 = get (r, "TAPE");
            r.proc.randomise (0.0f, false, false);  pump (50);
            CHECK (get (r, "DELAY_TIME") == t0 && get (r, "WET") == w0 && get (r, "FEEDBACK") == f0 && get (r, "TAPE") == tape0, "amount 0 changes nothing");
            float maxFb = 0, minWet = 1, maxWet = 0; bool locked = true, moved = false;
            for (int i = 0; i < 300; ++i)
            {
                r.proc.applyPreset (i % 2 == 0 ? 0 : 10);  pump (5);
                const float tk = get (r, "DELAY_TIME"), wk = get (r, "WET");
                r.proc.randomise (1.0f, true, true);
                locked = locked && get (r, "DELAY_TIME") == tk && get (r, "WET") == wk;
                r.proc.randomise (1.0f, false, false);
                maxFb = std::max (maxFb, get (r, "FEEDBACK"));
                minWet = std::min (minWet, get (r, "WET"));  maxWet = std::max (maxWet, get (r, "WET"));
                if (get (r, "DELAY_TIME") != tk) moved = true;
            }
            std::printf("   300 randomisations: max feedback %.3f, wet %.2f..%.2f\n", maxFb, minWet, maxWet);
            CHECK (maxFb <= 0.7001f, "randomise never lets feedback run away");
            CHECK (minWet >= 0.249f && maxWet <= 0.601f, "wet stays audible but not drowned");
            CHECK (locked, "KEEP TIME and KEEP MIX are respected");
            CHECK (moved, "and it does change things when not locked");
        }
        {
            Rig r;
            r.proc.setUiWidth (900);  r.proc.setHighContrast (true);  r.proc.setLargeText (true);
            juce::MemoryBlock mb;  r.proc.getStateInformation (mb);
            Rig r2;  r2.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (r2.proc.getUiWidth() == 900 && r2.proc.getHighContrast() && r2.proc.getLargeText(), "window size, high contrast and large text are saved with the plugin state");
            Rig r3;
            CHECK (r3.proc.getUiWidth() == 720 && ! r3.proc.getHighContrast() && ! r3.proc.getLargeText(), "defaults for a new instance");
            // an old state without the UI settings still loads
            auto tree = r.proc.apvts.copyState();  tree.removeChild (tree.getChildWithName ("UI"), nullptr);
            std::unique_ptr<juce::XmlElement> xml (tree.createXml());  juce::MemoryBlock mb2;  r.proc.copyXmlToBinary (*xml, mb2);
            Rig r4;  r4.proc.setStateInformation (mb2.getData(), (int) mb2.getSize());
            CHECK (r4.proc.getUiWidth() == 720, "state saved before these settings existed still loads");
        }
    }


    std::printf("51) Compatibility: parameter ids, ranges and defaults never change\n");
    {
        juce::File idFile ("frozen_param_ids.txt");      // run from the tests folder, or set SKELL_TESTS_DIR
        if (auto* d = std::getenv ("SKELL_TESTS_DIR")) idFile = juce::File (d).getChildFile ("frozen_param_ids.txt");
        else if (! idFile.existsAsFile()) idFile = juce::File ("../skell/skell-delay-vst-main/tests/frozen_param_ids.txt");
        juce::StringArray lines; lines.addLines (idFile.loadFileAsString());
        lines.removeEmptyStrings();
        Rig r; int checked = 0; bool ok = ! lines.isEmpty();
        for (auto& l : lines)
        {
            auto t = juce::StringArray::fromTokens (l, " ", "");
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (r.proc.apvts.getParameter (t[0]));
            if (p == nullptr) { std::printf("   MISSING %s\n", t[0].toRawUTF8()); ok = false; continue; }
            auto rg = p->getNormalisableRange();
            if (std::abs (rg.start - t[1].getFloatValue()) > 1e-3f || std::abs (rg.end - t[2].getFloatValue()) > 1e-3f
                || std::abs (p->convertFrom0to1 (p->getDefaultValue()) - t[3].getFloatValue()) > 1e-3f) { std::printf("   CHANGED %s\n", t[0].toRawUTF8()); ok = false; }
            ++checked;
        }
        std::printf("   %d parameters checked\n", checked);
        CHECK (ok, "every released parameter id still exists with the same range and default (add new ones, never change old ones)");
        CHECK (r.proc.getParameters().size() >= checked, "no parameter was removed");
    }

    std::printf("52) Damaged, old and hostile saved states\n");
    {
        Rig r; r.set ("FEEDBACK", 0.77f);
        juce::MemoryBlock good; r.proc.getStateInformation (good);
        auto load = [&] (const void* d, int n) { Rig x; x.proc.setStateInformation (d, n); return x.proc.apvts.getRawParameterValue ("FEEDBACK")->load(); };
        CHECK (std::abs (load (good.getData(), (int) good.getSize()) - 0.77f) < 1e-3f, "a normal state round-trips");
        CHECK (std::abs (load (nullptr, 0) - 0.5f) < 1e-3f && std::abs (load (good.getData(), 0) - 0.5f) < 1e-3f && std::abs (load (good.getData(), -5) - 0.5f) < 1e-3f, "empty / null / negative size is ignored");
        juce::MemoryBlock junk; junk.setSize (5000); juce::Random jr (4); for (size_t i = 0; i < junk.getSize(); ++i) junk[i] = (char) jr.nextInt (256);
        CHECK (std::abs (load (junk.getData(), (int) junk.getSize()) - 0.5f) < 1e-3f, "random bytes are ignored");
        juce::MemoryBlock cut; cut.append (good.getData(), good.getSize() / 2);
        load (cut.getData(), (int) cut.getSize());
        CHECK (true, "a truncated state does not crash");
        {   // an old state: only a few parameters, no UI, no version
            juce::XmlElement x ("Parameters");  auto* c = x.createNewChildElement ("PARAM");  c->setAttribute ("id", "FEEDBACK");  c->setAttribute ("value", 0.6);
            juce::MemoryBlock mb; DelayAudioProcessor::copyXmlToBinary (x, mb);
            Rig y; y.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (std::abs (y.proc.apvts.getRawParameterValue ("FEEDBACK")->load() - 0.6f) < 1e-3f && std::abs (y.proc.apvts.getRawParameterValue ("WET")->load() - 0.5f) < 0.51f, "an old state with few parameters loads, the rest keep sane values");
            std::vector<float> in (48000, 0.3f), a, b; y.run (in, in, a, b);
            bool fin = true; for (float v : a) if (! std::isfinite (v)) fin = false;
            CHECK (fin, "and plays normally");
        }
        {   // nan / inf / absurd values in a state
            juce::XmlElement x ("Parameters");
            for (auto* id : { "FEEDBACK", "DELAY_TIME", "HICUT", "WET", "GRAIN", "SMOOTH" })
            { auto* c = x.createNewChildElement ("PARAM");  c->setAttribute ("id", id);  c->setAttribute ("value", std::string (id) == "WET" ? "inf" : (std::string (id) == "FEEDBACK" ? "nan" : "1e30")); }
            juce::MemoryBlock mb; DelayAudioProcessor::copyXmlToBinary (x, mb);
            Rig y; y.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            std::vector<float> in (96000), a, b; for (size_t i = 0; i < in.size(); ++i) in[i] = 0.4f * std::sin (0.05f * (float) i);
            y.run (in, in, a, b);
            bool fin = true; float pk = 0; for (float v : a) { if (! std::isfinite (v)) fin = false; pk = std::max (pk, std::abs (v)); }
            std::printf("   hostile state: peak %.3f\n", pk);
            CHECK (fin && pk < 8.0f, "NaN, inf and 1e30 in a saved state cannot break the engine");
        }
    }

    std::printf("53) Corrupt preset files and an unwritable preset folder\n");
    {
        Rig r;
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("skell-bad-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        DelayAudioProcessor::setUserPresetDirectory (dir);
        dir.createDirectory();
        dir.getChildFile ("empty.skellpreset").replaceWithText ("");
        dir.getChildFile ("trunc.skellpreset").replaceWithText ("<Parameters><PARAM id=\"FEEDBACK\" val");
        dir.getChildFile ("wrongtag.skellpreset").replaceWithText ("<Other><PARAM id=\"FEEDBACK\" value=\"0.9\"/></Other>");
        dir.getChildFile ("junk.skellpreset").replaceWithText (juce::String::fromUTF8 ("\xff\xfe\x00\x01 not xml at all"));
        dir.getChildFile ("nan.skellpreset").replaceWithText ("<Parameters><PARAM id=\"WET\" value=\"nan\"/><PARAM id=\"DELAY_TIME\" value=\"-5e9\"/></Parameters>");
        bool anyCrash = false; int loaded = 0;
        for (auto& e : r.proc.getPresetEntries()) if (e.user) { if (r.proc.loadPresetEntry (e), true) ++loaded; }
        std::vector<float> in (48000, 0.3f), a, b;  r.run (in, in, a, b);
        bool fin = true; for (float v : a) if (! std::isfinite (v)) fin = false;
        CHECK (! anyCrash && fin, "loading every kind of broken preset file never crashes or poisons the output");
        dir.deleteRecursively();
        const auto blocker = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("skell-blocker-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        blocker.replaceWithText ("a file where the folder should be");
        DelayAudioProcessor::setUserPresetDirectory (blocker.getChildFile ("Presets"));
        CHECK (! r.proc.saveUserPreset ("Cannot save"), "saving into an unwritable location fails cleanly (returns false)");
        CHECK (r.proc.getPresetEntries().size() == getFactoryPresets().size(), "and the browser still lists the factory presets");
        blocker.deleteFile();
        DelayAudioProcessor::setUserPresetDirectory (juce::File());
        DelayAudioProcessor::setFavourite ("F:Test", true);  DelayAudioProcessor::setFavourite ("F:Test", false);
    }

    std::printf("54) Every preset at every sample rate and block size (offline 'host matrix')\n");
    {
        const double rates[] = { 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
        const int blocks[] = { 1, 7, 64, 480, 1024, 4097, 9000 };
        float worst = 0; bool fin = true; int runs = 0; int maxLat = 0;
        for (double sr : rates)
        {
            DelayAudioProcessor p;
            const int np = (int) getFactoryPresets().size();
            for (int pi = 0; pi < np; ++pi)
            {
                const int bs = blocks[(size_t) (pi + (int) (sr / 1000)) % 7];
                p.setRateAndBufferSizeDetails (sr, bs);  p.prepareToPlay (sr, bs);
                p.applyPreset (pi);
                for (const char* id : { "GRAIN", "SPECTRAL", "POCKET", "TELEPATHY", "ELASTIC" })
                    if (pi % 3 == 0) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (0.6f); }
                const int total = (int) (sr * 1.5);
                juce::MidiBuffer midi; juce::Random rn (pi);
                for (int pos = 0; pos < total; pos += bs)
                {
                    const int len = std::min (bs, total - pos);
                    juce::AudioBuffer<float> buf (2, len);
                    for (int i = 0; i < len; ++i) { const float v = ((pos + i) % (int) (sr * 0.3) < 2000) ? 0.6f * std::sin (0.07f * (float) (pos + i)) : 0.0f; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                    p.processBlock (buf, midi);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) fin = false; worst = std::max (worst, std::abs (v)); }
                }
                maxLat = std::max (maxLat, (int) std::lround (1000.0 * p.getLatencySamples() / sr));
                ++runs;
            }
        }
        std::printf("   %d runs, worst peak %.3f, largest reported latency %d ms\n", runs, worst, maxLat);
        CHECK (fin && worst < 8.0f, "finite and bounded for every preset, rate and block size");
        CHECK (maxLat <= 45, "reported latency stays under 45 ms at every sample rate");
    }


    std::printf("55) Smart features say why they are idle\n");
    {
        Rig r; r.set ("ELASTIC", 0.8f); r.set ("TELEPATHY", 0.8f); r.set ("POCKET", 1.0f); r.set ("DRY", 0.0f);
        CHECK (r.proc.getSmartStatus (2).problem && r.proc.getSmartStatus (2).text.contains ("host"), "Pocket without a playing host says it needs the transport");
        CHECK (r.proc.getSmartStatus (0).text.contains ("waiting") && r.proc.getSmartStatus (1).text.contains ("waiting"), "Elastic and Telepathy wait when nothing is played");
        const int N = (int) (5 * SR); std::vector<float> in ((size_t) N, 0.0f), a, b; juce::Random rn (5);
        for (double t = 0.3; t < 4.2; t += 0.25)
            for (int i = 0; i < (int) (0.08 * SR); ++i) in[(size_t) (t * SR) + (size_t) i] = 0.4f * (rn.nextFloat() * 2 - 1) * std::exp (-(float) i / (0.03f * (float) SR));
        r.run (in, in, a, b);
        const auto e = r.proc.getSmartStatus (0), t = r.proc.getSmartStatus (1);
        std::printf("   after a steady rhythm: Elastic '%s', Telepathy '%s'\n", e.text.toRawUTF8(), t.text.toRawUTF8());
        CHECK (e.text.contains ("following") || e.text.contains ("limit"), "Elastic reports that it is following the performance");
        CHECK (t.text.contains ("predicting") && ! t.problem, "Telepathy reports that it is predicting");
        std::vector<float> quiet ((size_t) (4 * SR), 0.0f); r.run (quiet, quiet, a, b);
        CHECK (r.proc.getSmartStatus (1).text.contains ("waiting"), "and goes back to waiting when the playing stops");
        Rig off; CHECK (off.proc.getSmartStatus (0).text.isEmpty() && off.proc.getSmartStatus (1).text.isEmpty() && off.proc.getSmartStatus (2).text.isEmpty(), "nothing is said about features that are off");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

#ifdef SKELL_HARNESS
namespace juce { extern const char* const juce_compilationDate; extern const char* const juce_compilationTime;
                 const char* const juce_compilationDate = __DATE__; const char* const juce_compilationTime = __TIME__; }
#endif
