# Release checklist

What was verified automatically by headless tests (no audio device) and what still needs a Windows host, a person or a certificate.

## Verified (see tests/)
- DSP, UI-logic and compatibility tests (`tests/dsp_tests.cpp`, 206 checks), a stress/limit suite (`tests/stress_tests.cpp`, 10 checks), an allocation + CPU check (`tests/realtime_check.cpp`).
- Clean with ASan + UBSan; clean with `-Wall -Wextra -Wshadow`.
- Every factory preset at 22.05 kHz to 192 kHz and block sizes 1 to 9000: finite, bounded, latency under 45 ms.
- No heap allocation in `processBlock` over 4,000 blocks with every feature on and parameters moving.
- Damaged / old / hostile saved states and preset files load safely.
- Parameter ids, ranges and defaults are frozen in `tests/frozen_param_ids.txt`; the test fails if a released one changes.
- About 5% of one core, everything on, optimised processor code (the JUCE objects in that build were -O1).

## Automated Windows preview delivery gates
- The Windows release workflow stages a flat VST3-only preview package with strict moduleinfo, AMD64 PE32+, provenance and a complete SHA-256 inventory.
- The package includes the PowerShell 7 per-user preview installer and install guide; Windows CI exercises WhatIf, fresh install, idempotence, tamper rejection, update, verified backup and rollback against the staged artifact.
- These installer tests do not substitute for FL Studio scan/load, actual audio listening, user-machine CPU/latency, code signing or owner approval.

## Not verified here (needs you)
1. **Listen.** Nothing has been auditioned. Use each preset on real vocals in Ableton, Logic, FL Studio, Reaper, Cubase, Pro Tools (AAX is not built).
2. **Real CMake build on Windows x64** (`.github/workflows/release.yml` does it). The current product target is Windows VST3 only; macOS/AU and Linux product builds are intentionally deferred. The CMake `SKELL_BUILD_TESTS` target must pass in the Windows workflow.
3. **pluginval, strictness 10**, on the Windows VST3 bundle (the workflow runs it).
4. **Windows code signing** (requires an owner-provided Windows signing certificate; see `installer/`).
5. **Decisions in `legal/`**: company name, 4-letter manufacturer code, licence tier, trademark search.
6. **Beta** (see `beta/`).

## Before the first public build
- Confirm `COMPANY_NAME` ("Skelly Audio") and `PLUGIN_MANUFACTURER_CODE` ("Skly") in `CMakeLists.txt`; they cannot change after release without breaking saved projects.
- Bump `VERSION` in `CMakeLists.txt`; the installers read it.
- Run the whole `tests/` folder one more time and keep `frozen_param_ids.txt` in step (add new parameters; never change old ones).
