#pragma once

#include <vector>

// Factory presets. Every parameter not listed in a preset is reset to its default first,
// so each preset is a complete, predictable starting point.
//
// Sync note index: 0 = 1/64, 1 = 1/32, 2 = 1/16, 3 = 1/8, 4 = 1/4, 5 = 1/2, 6 = 1/1
// FEEL: 0 straight, 1 dotted, 2 triplet     FB_MODE: 0 normal, 1 inverted, 2 ping pong
// SHIM_PITCH: 0 = +12, 1 = +7, 2 = -12, 3 = -7
struct PresetValue { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetValue> values; };

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets =
    {
        { "Init", {} },

        { "Slapback", {
            { "DELAY_TIME", 95.0f }, { "FEEDBACK", 0.12f }, { "HICUT", 7000.0f }, { "LOCUT", 120.0f },
            { "TAPE", 0.25f }, { "WET", 0.5f } } },

        { "Tape Echo", {
            { "DELAY_TIME", 320.0f }, { "FEEDBACK", 0.48f }, { "LOCUT", 120.0f }, { "HICUT", 5500.0f },
            { "TAPE", 0.65f }, { "RICHER", 0.2f }, { "WET", 0.45f } } },

        { "Dotted Eighth", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 3.0f }, { "FEEL", 1.0f }, { "FEEDBACK", 0.42f },
            { "HICUT", 9000.0f }, { "WIDER", 0.2f }, { "WET", 0.5f } } },

        { "Ping Pong Bounce", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 3.0f }, { "FB_MODE", 2.0f }, { "FEEDBACK", 0.55f },
            { "HICUT", 10000.0f }, { "LOCUT", 100.0f }, { "WET", 0.5f } } },

        { "Triplet Ping Pong", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 3.0f }, { "FEEL", 2.0f }, { "FB_MODE", 2.0f },
            { "FEEDBACK", 0.5f }, { "HICUT", 9000.0f }, { "WIDER", 0.3f }, { "WET", 0.5f } } },

        { "Dub Echo", {
            { "DELAY_TIME", 375.0f }, { "FEEDBACK", 0.72f }, { "LOCUT", 250.0f }, { "HICUT", 3200.0f },
            { "TAPE", 0.8f }, { "SWIRL", 0.3f }, { "WET", 0.55f } } },

        { "Vocal Ducker", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 4.0f }, { "FEEDBACK", 0.38f }, { "DUCKING", 0.8f }, { "DUCK_REL", 350.0f },
            { "HICUT", 8000.0f }, { "LOCUT", 200.0f }, { "WET", 0.55f } } },

        { "Space Echo", {
            { "DELAY_TIME", 400.0f }, { "FEEDBACK", 0.6f }, { "HICUT", 6000.0f }, { "TAPE", 0.5f },
            { "SWIRL", 0.6f }, { "SAUCE", 0.3f }, { "WET", 0.5f } } },

        { "Lo-Fi Crunch", {
            { "DELAY_TIME", 280.0f }, { "FEEDBACK", 0.5f }, { "CHEAP", 1.0f }, { "TAPE", 0.55f },
            { "LOCUT", 200.0f }, { "HICUT", 3500.0f }, { "WET", 0.45f } } },

        { "Ambient Wash", {
            { "DELAY_TIME", 520.0f }, { "FEEDBACK", 0.75f }, { "SAUCE", 0.7f }, { "WIDER", 0.6f },
            { "RICHER", 0.4f }, { "HICUT", 6500.0f }, { "WET", 0.5f } } },

        { "Shimmer Cloud", {
            { "DELAY_TIME", 480.0f }, { "FEEDBACK", 0.68f }, { "SHIMMER", 0.55f }, { "SHIM_PITCH", 0.0f },
            { "SAUCE", 0.6f }, { "WIDER", 0.5f }, { "HICUT", 9000.0f }, { "TAPE", 0.15f }, { "WET", 0.5f } } },

        { "Fifth Rise", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 4.0f }, { "FEEDBACK", 0.6f }, { "SHIMMER", 0.4f },
            { "SHIM_PITCH", 1.0f }, { "SAUCE", 0.4f }, { "HICUT", 8500.0f }, { "WET", 0.5f } } },

        { "Haunted Drift", {
            { "DELAY_TIME", 700.0f }, { "FEEDBACK", 0.66f }, { "SHIMMER", 0.4f }, { "SHIM_PITCH", 2.0f },
            { "DESTINY", 1.0f }, { "RICHER", 0.5f }, { "TAPE", 0.6f }, { "SAUCE", 0.4f }, { "HICUT", 5000.0f },
            { "WET", 0.5f } } },

        { "Reverse Verb", {
            { "REVERSE", 1.0f }, { "DELAY_TIME", 600.0f }, { "FEEDBACK", 0.45f }, { "SAUCE", 0.5f },
            { "HICUT", 8000.0f }, { "WET", 0.6f } } },

        { "Reverse Ping Pong", {
            { "REVERSE", 1.0f }, { "SYNC", 1.0f }, { "SYNC_DIV", 3.0f }, { "FB_MODE", 2.0f },
            { "FEEDBACK", 0.42f }, { "HICUT", 9000.0f }, { "WIDER", 0.3f }, { "WET", 0.55f } } },

        { "Doubler", {
            { "DELAY_TIME", 28.0f }, { "FEEDBACK", 0.0f }, { "WIDER", 0.5f }, { "RICHER", 0.4f },
            { "WET", 0.5f } } },

        { "Analog BBD", {
            { "DELAY_TIME", 300.0f }, { "FEEDBACK", 0.5f }, { "BBD", 1.0f }, { "TAPE", 0.2f }, { "LOCUT", 100.0f },
            { "RICHER", 0.25f }, { "WET", 0.5f } } },

        { "Gated Echo", {
            { "SYNC", 1.0f }, { "SYNC_DIV", 3.0f }, { "FEEDBACK", 0.6f }, { "GATE", 5.0f }, { "GATE_DEPTH", 1.0f },
            { "HICUT", 9000.0f }, { "SAUCE", 0.25f }, { "WET", 0.55f } } },

        { "Runaway Dream", {
            { "DELAY_TIME", 800.0f }, { "FEEDBACK", 0.9f }, { "LOCUT", 150.0f }, { "HICUT", 4000.0f },
            { "TAPE", 0.4f }, { "DUCKING", 0.5f }, { "SAUCE", 0.3f }, { "WET", 0.5f } } },
    };
    return presets;
}
