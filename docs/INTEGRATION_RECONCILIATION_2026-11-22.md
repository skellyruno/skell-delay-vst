# Skell Delay — safe branch reconciliation (2026-11-22)

## Immutable baselines

- Verified baseline `main`: `00b45315b79e194a3775b0f8a689d13ef93914a0` (Windows DSP-test stack-overflow and legacy-state restoration fix).
- Cumulative release candidate: `55a1643aeb014b21160603fc25c85a8e85903310`, PR #23, built on PR #22 and its predecessor branches.
- This branch is a **new draft integration candidate based on current main**, not a force push or rewrite of the historical release PR stack.

## Code compatibility evidence

- `Source/PluginProcessor.cpp` blob is byte-identical to main (`5f9d5f3ca411bd30472796a4b071c443bfe01862`): the `applyTree(restoredState)` legacy-state default restoration stays intact.
- The cumulative `tests/dsp_tests.cpp` contains every line of the main test suite in order and adds accessibility/category checks. Its heap-backed `Rig` and `static_assert(sizeof(Rig) <= 64)` retain the Windows MSVC stack-overflow fix.
- The release workflow retains `Enable Windows DSP crash dumps`, `Preserve Windows DSP crash dump`, and CTest diagnostics; the CMake test target retains `/Zi` and `/DEBUG:FULL` for Windows DSP diagnosis.
- Mainline audio/preset automation IDs, processor state format, `tests/frozen_param_ids.txt`, and existing user presets were not renamed or removed.
- The candidate also stages the protected VST3-only preview, moduleinfo checks, accessible preset categories, install/update/rollback, and explicit owner legal/signing acceptance gates. Approval remains **NOT_APPROVED**.

## Verification and release blockers

- Historical PR #23 exact-head Windows build passed, **but it was checked against a stacked PR base**. CI on this new direct-main branch is required.
- Before merging: exact-head Windows configure/build, DSP/stress/realtime CTests, pluginval strictness 10, private-artifact guard, installer rollback, real JUCE UI screenshot inspection, FL Studio project/preset state recall, and owner review of dropping AU from the build targets.
- No product upload, signed tag, installer distribution, or commercial approval is authorized by this integration branch.

## Retained cross-platform regression coverage

The cumulative branch's Windows-only `.github/workflows/release.yml` would otherwise remove the prior macOS/Linux compile, DSP/stress/real-time and pluginval tests. `.github/workflows/crossplatform-dsp.yml` retains those original non-Windows test steps, with PR-only cancellation. It cannot sign, notarise, package or upload products. The existing Windows product and installer checks remain in the release workflow. Mac AU packaging has **not** been restored; the VST3-only target change still needs explicit compatibility review before merge.
