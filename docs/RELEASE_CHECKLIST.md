# Release checklist

> **Evidence status (2026-11-15): BLOCKED for release.** The historical test results below are not proof that current `main` or any open PR passes. A previously reported Windows CI run compiled but failed DSP/stress tests, while Linux/macOS pluginval also failed. Treat all previous test counts and CPU estimates as historical claims until reproduced on a pinned commit. Do not publish an installer or mark a VST3 as validated while these gates fail.

## Required evidence for a release candidate
- Record the exact source commit, CI run URL, OS/architecture, compiler, JUCE version and build configuration.
- Require green Windows CMake/VST3 build **and** all CTest DSP, stress, compatibility and real-time tests; preserve `LastTest.log` as a failure artifact when red.
- Require pluginval on the actual packaged VST3; retain its strictness, host and result. Other-platform failures must be triaged, not silently ignored.
- Verify the packaged `.vst3` contains the expected `Contents/x86_64-win/*.vst3` binary, and record its SHA-256 and installer SHA-256.
- Run FL Studio load/save/reopen, automation recall, preset recall, bypass, mono/stereo, 44.1/48/96 kHz, compact UI/menu and high-DPI interaction checks on Windows. Record screenshots only from the actual compiled editor.
- Preserve the last working binary and immutable source revision before replacing any installed release; mark unavailable backups as **UNAVAILABLE**.
- Block release on failing tests, missing host verification, missing artifact checksums or unresolved licensing/identity decisions.


What was verified automatically (headless, Linux, no audio device) and what still needs a person, a machine or a certificate.

## Verified (see tests/)
- DSP, UI-logic and compatibility tests (`tests/dsp_tests.cpp`, 206 checks), a stress/limit suite (`tests/stress_tests.cpp`, 10 checks), an allocation + CPU check (`tests/realtime_check.cpp`).
- Clean with ASan + UBSan; clean with `-Wall -Wextra -Wshadow`.
- Every factory preset at 22.05 kHz to 192 kHz and block sizes 1 to 9000: finite, bounded, latency under 45 ms.
- No heap allocation in `processBlock` over 4,000 blocks with every feature on and parameters moving.
- Damaged / old / hostile saved states and preset files load safely.
- Parameter ids, ranges and defaults are frozen in `tests/frozen_param_ids.txt`; the test fails if a released one changes.
- About 5% of one core, everything on, optimised processor code (the JUCE objects in that build were -O1).

## Not verified here (needs you)
1. **Listen.** Nothing has been auditioned. Use each preset on real vocals in Ableton, Logic, FL Studio, Reaper, Cubase, Pro Tools (AAX is not built).
2. **Real CMake build on Windows and macOS** (`.github/workflows/release.yml` does it; it has not been run). The CMake `SKELL_BUILD_TESTS` target has also not been built; the same tests were built and run with a hand-made harness.
3. **pluginval, strictness 10**, VST3 and AU (the workflow runs it). Also `auval -v aufx Dlay Skly` on macOS.
4. **Code signing and notarisation** (needs your Apple Developer ID and a Windows signing certificate; see `installer/`).
5. **Decisions in `legal/`**: company name, 4-letter manufacturer code, licence tier, trademark search.
6. **Beta** (see `beta/`).

## Before the first public build
- Confirm `COMPANY_NAME` ("Skelly Audio") and `PLUGIN_MANUFACTURER_CODE` ("Skly") in `CMakeLists.txt`; they cannot change after release without breaking saved projects.
- Bump `VERSION` in `CMakeLists.txt`; the installers read it.
- Run the whole `tests/` folder one more time and keep `frozen_param_ids.txt` in step (add new parameters; never change old ones).
