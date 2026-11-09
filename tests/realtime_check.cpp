// Real-time safety + CPU check. Counts heap allocations made while processBlock runs, and times the heaviest setup.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#ifdef SKELL_HARNESS   // only the hand-made harness (no CMake / BinaryData); the CMake test targets link the real ones
namespace BinaryData { const char* skell_bg_png = "x"; }
#endif
static std::atomic<long> gAllocs { 0 };
static thread_local bool gCount = false;
void* operator new (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int bad = 0;
    {   // 1) no allocation while processing, with every feature in play and parameters moving
        DelayAudioProcessor p;  const double sr = 48000;  const int bs = 256;
        p.setRateAndBufferSizeDetails (sr, bs);  p.prepareToPlay (sr, bs);
        auto set = [&] (const char* id, float v) { auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        for (const char* id : { "SHIMMER", "SAUCE", "SWIRL", "WIDER", "RICHER", "GRAIN", "SPECTRAL", "ECOLOGY", "ELASTIC", "TELEPATHY", "TAPE", "DUCKING" }) set (id, 0.5f);
        set ("BBD", 1.0f); set ("GATE", 3.0f); set ("AUTOPAN", 2.0f); set ("POCKET", 1.0f); set ("POCKET_AMT", 0.5f); set ("REVERSE", 1.0f);
        juce::Random rn (1);  juce::MidiBuffer midi;
        for (int blk = 0; blk < 4000; ++blk)
        {
            juce::AudioBuffer<float> buf (2, bs);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, 0.4f * std::sin (0.05f * (float) (blk * bs + i)) * (((blk / 30) % 2) ? 1.0f : 0.0f));
            if (blk % 50 == 0) { set ("DELAY_TIME", 50.0f + 800.0f * rn.nextFloat()); set ("STUTTER", rn.nextBool() ? 1.0f : 0.0f); set ("TAPESTOP", rn.nextFloat() < 0.2f ? 1.0f : 0.0f); set ("REVTHROW", rn.nextBool() ? 1.0f : 0.0f); set ("FEEDBACK", 0.7f * rn.nextFloat()); }
            gCount = true;  const long before = gAllocs.load();
            p.processBlock (buf, midi);
            gCount = false;
            if (blk > 5 && gAllocs.load() != before) { ++bad; if (bad < 4) std::printf ("  allocation inside processBlock at block %d\n", blk); }
        }
        std::printf ("%s allocations while processing 4000 blocks with everything on: %d block(s) allocated\n", bad ? "[FAIL]" : "[PASS]", bad);
    }
    {   // 2) CPU: heaviest realistic setup, time per second of audio
        for (int bs : { 64, 512 })
        {
            DelayAudioProcessor p;  const double sr = 48000;
            p.setRateAndBufferSizeDetails (sr, bs);  p.prepareToPlay (sr, bs);
            auto set = [&] (const char* id, float v) { auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            for (const char* id : { "SHIMMER", "SAUCE", "SWIRL", "WIDER", "RICHER", "GRAIN", "SPECTRAL", "ECOLOGY", "ELASTIC", "TELEPATHY", "TAPE" }) set (id, 0.6f);
            set ("BBD", 1.0f); set ("REVERSE", 1.0f); set ("FEEDBACK", 0.6f); set ("POCKET", 1.0f); set ("POCKET_AMT", 0.5f);
            juce::MidiBuffer midi;  const int seconds = 20;  const int blocks = (int) (seconds * sr / bs);
            juce::AudioBuffer<float> buf (2, bs);
            auto t0 = std::chrono::steady_clock::now();
            for (int blk = 0; blk < blocks; ++blk)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, 0.3f * std::sin (0.05f * (float) (blk * bs + i)));
                p.processBlock (buf, midi);
            }
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("CPU, everything on, block %d: %.1f%% of one core (%.2f s to render %d s)\n", bs, 100.0 * sec / seconds, sec, seconds);
        }
    }
    return bad ? 1 : 0;
}
#ifdef SKELL_HARNESS
namespace juce { extern const char* const juce_compilationDate; extern const char* const juce_compilationTime;
                 const char* const juce_compilationDate = __DATE__; const char* const juce_compilationTime = __TIME__; }
#endif
