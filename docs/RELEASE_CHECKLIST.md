# Release checklist

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
