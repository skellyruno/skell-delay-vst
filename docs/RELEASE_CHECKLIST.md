# Skell-Delay release checklist

This checklist separates automated engineering evidence from owner/host/legal approval. A green GitHub Actions run is not proof of musical quality, DAW acceptance, signing, licensing or commercial readiness.

## Latest verified Windows private-preview CI evidence

Exact tested source: commit [edf71f9](https://github.com/skellyruno/skell-delay-vst/commit/edf71f9cdfce6257402b4ed5044aa019ae888720), on draft PR [#14](https://github.com/skellyruno/skell-delay-vst/pull/14). The exact-head Windows workflow [#44384187310](https://github.com/skellyruno/skell-delay-vst/actions/runs/44384187310) completed successfully on 2026-11-18.

The run passed:

- Python release-package verifier regression tests.
- Windows x64 CMake configure and Release VST3 build.
- CTest DSP, stress and real-time suites.
- pluginval strictness 10.
- VST3 staging and package verification, including strict metadata, AMD64 PE32+, provenance and SHA-256 inventory.
- PowerShell 7 private-preview installer integration against the staged package: WhatIf, fresh per-user install, idempotence, tamper rejection, update, verified backup, tampered-backup rejection and byte-for-byte rollback.
- Upload of the validated private-preview artifact.

Artifact record returned by GitHub Actions:

- Artifact: [#14466232904](https://github.com/skellyruno/skell-delay-vst/actions/runs/44384187310)
- Name: `SkellDelay-Windows-VST3-edf71f9cdfce6257402b4ed5044aa019ae888720`
- Size: 4,749,067 bytes
- GitHub artifact SHA-256: `f94cf91a4d5f38f217ff7fe972c347a8602ed89804a119f8bc8a30a3e7b85634`
- VST3 module: 8,908,800 bytes; SHA-256 `d486f20c0bd64077a70a2392a3c8730b308ec66534bdcd084802013cf427525b`.
- Strict moduleinfo JSON SHA-256: `77289d5d36b77400c8222241923fa0a86b4a256f21275736673703d77223e0f5`.
- Preview installer script SHA-256: `f72529e4b2aef73869039a8bb1c710752727d18442f762bb39cd60c4098ef8ca`.
- Preview install guide SHA-256: `c2b709d826a4f665ff35d4027710fce4a0170c059c03360a60541f48d58ed817`.

The downloaded ZIP was independently checked in this run: archive SHA-256 matched GitHub's artifact digest, ZIP integrity passed, all five `SHA256SUMS.txt` entries matched, and the module had an MZ header with an AMD64 PE32+ header. The package's `build-info.json` explicitly marks it `preview-only`.

**Metadata blocker found in the actual artifact:** `moduleinfo.json` has empty Factory Info `URL` and `E-Mail` values. Do not invent these. The owner must approve the public website and support contact before the metadata is populated.

**Scope of this evidence:** the successful run is on a draft PR branch, not yet on `main`. It proves that the Windows preview build and automated install/update/rollback tests passed on GitHub's runner. It does not prove that a signed installer exists or that the plugin is ready for public sale. The artifact is temporary CI output and must be downloaded and independently retained before its expiry if it is to serve as a durable owner backup.

## Verified source-level and automated test coverage

- DSP, UI-logic and compatibility coverage in `tests/dsp_tests.cpp`, including the frozen parameter-ID regression checks.
- Stress/limit coverage in `tests/stress_tests.cpp`.
- Allocation and real-time checks in `tests/realtime_check.cpp`.
- The Windows workflow runs the CMake test targets and pluginval against the Windows x64 VST3.
- The current CMake product target is VST3 only. AU/macOS and Linux product artifacts are deferred; historical scripts are not supported release paths.
- The current JUCE dependency is pinned to `8.0.17-security`.

Any CPU percentage or latency measured on a hosted CI runner is not a substitute for a repeatable measurement on the owner's target Windows machine and intended DAW/audio interface. Record target-machine sample rate, buffer size, plugin configuration, CPU method and measured result before making a performance claim.

## Still requires owner, host, legal or certificate evidence

1. **Musical listening and A/B:** audition every factory preset and representative delay modes on real sessions/vocals. Record source material, settings, level matching and findings; synthetic tests do not establish sound quality.
2. **FL Studio host acceptance:** scan/load the VST3; exercise automation, preset changes, save/reopen, project reopen and editor open/close; confirm no missing-plugin or state-recall regression. Record FL Studio version and actual evidence.
3. **Target-machine performance:** measure CPU, latency and stability on the intended Windows hardware and audio buffer settings.
4. **Windows signing:** provide and protect the owner-controlled signing certificate; run the approved tag-only signing path; verify Authenticode publisher, timestamp and final installer hash. The successful preview run did not sign an installer.
5. **Vendor contact metadata:** approve a public URL and support e-mail for the VST3 Factory Info fields; both are blank in the verified preview artifact. Do not publish guessed contact details.
5. **Publisher and identity decisions:** confirm the legal/trading company name, four-character manufacturer code `Skly`, unique plugin code `Dlay`, product name and trademark clearance before the first public release. Do not change host identity codes after release.
6. **Third-party licensing:** confirm the applicable JUCE commercial/AGPL tier, Steinberg VST3 SDK terms, and any required attribution or splash-screen obligations. Do not rely on the repository's informal legal summary as legal advice.
7. **Asset rights:** confirm commercial redistribution rights for `Source/Assets/skell_bg.png` and any other artwork or third-party assets.
8. **Customer terms and sales operations:** have the EULA reviewed; decide refund policy, privacy notice, support contact, tax/VAT handling and merchant-of-record. The plugin currently makes no network calls and has no copy protection; do not imply an activation or licensing service exists.
9. **Activation decision:** decide whether a licence file, serial or no activation is appropriate before beta so the tester and customer flow is not changed unexpectedly.
10. **Beta acceptance:** run a controlled beta with documented host versions, install/update/rollback outcomes, crash reports and owner approval.

## Fail-closed signed version-tag gate

The Windows workflow now treats a `vMAJOR.MINOR.PATCH` tag as a signed release-candidate build, not as a private preview.

Before signing, the workflow requires these owner-configured GitHub repository variables:

- `SKELL_FACTORY_INFO_URL` — an approved absolute HTTPS publisher URL.
- `SKELL_SUPPORT_EMAIL` — an approved support email address.
- `SKELL_VENDOR_NAME` — the exact owner-approved publisher/legal trading name. It must match `COMPANY_NAME` in `CMakeLists.txt` and the generated VST3 `Factory Info.Vendor`; the gate does not guess, normalize or silently rewrite the vendor identity.

It also requires the owner-managed secrets `WIN_CERT_PFX` and `WIN_CERT_PASSWORD`. The preparation script verifies that the tag matches the CMake version, requires the owner-approved `SKELL_VENDOR_NAME` to match CMake and VST3 metadata exactly, validates the contact fields and PFX input, applies the approved contact values only to the generated build's `moduleinfo.json`, and refuses to overwrite conflicting non-empty metadata. No contact details or certificate values are hardcoded in source.

The tagged workflow then signs the Windows VST3 module and Inno Setup installer and checks Authenticode validity plus a trusted timestamp before staging the artifact. A missing variable, secret, invalid value, version mismatch or signature failure stops the tag run. The package manifest marks publisher contact as complete only when the tag-preparation gate was actually passed; merely finding non-empty fields in a preview bundle is not enough. Ordinary PR/main preview builds remain unsigned and may keep blank Factory Info contact fields; their manifest reports that blocker. This workflow does not publish a GitHub Release or deploy a storefront.

## Third-party license and redistribution gate

Windows packages include the exact license texts from the pinned JUCE FetchContent source:
ThirdPartyLicenses/JUCE-LICENSE.md and ThirdPartyLicenses/VST3-SDK-LICENSE.txt. The package
also includes a rendered THIRD_PARTY_NOTICES.txt, and every legal file is checksummed.

The checked-in legal/RELEASE_APPROVAL.json is deliberately NOT_APPROVED. The signed
version-tag path fails before changing moduleinfo.json until the owner records evidence
for JUCE licensing, VST3 SDK terms, the compiled dependency inventory, artwork rights,
EULA review and trademark clearance. The EULA must have no template marker or bracketed
placeholder. AGPLv3 selection also requires a source-distribution reference.

Private previews include the notice and upstream license texts, omit the draft EULA, and
are not approved for redistribution or sale. CI does not publish a GitHub Release.

## Before the first public build

- Complete and record every owner/host/legal/certificate gate above.
- Confirm `COMPANY_NAME` (`Skelly Audio`), `PLUGIN_MANUFACTURER_CODE` (`Skly`) and `PLUGIN_CODE` (`Dlay`) in `CMakeLists.txt`; freeze these identity values after release.
- Bump `VERSION` in `CMakeLists.txt` only for an approved release candidate; verify installer version mapping.
- Run the complete Windows release workflow on the exact release tag and inspect the uploaded artifact, signature, package manifest and hashes.
- Preserve the last known-good preview artifact and its checksum separately from the new candidate.
- Keep `tests/frozen_param_ids.txt` synchronized when adding parameters; never rename or change released parameter IDs, ranges or defaults.
- Publish only after the owner explicitly approves the final binary, installer, legal terms and release notes.

## Baseline and compatibility safeguards

- Do not overwrite the last known-good VST3 package or installer when testing a candidate.
- Preserve presets, parameter IDs, saved-state compatibility and FL Studio project recall.
- Keep the private per-user preview installer separate from the signed production Inno Setup installer.
- A preview install is optional and user-confirmed; it is not a public distribution mechanism.


## Owner-controlled product acceptance matrix (additional signed-tag gate)

The signed version-tag path also requires `release/OWNER_ACCEPTANCE.json` to be explicitly completed and approved. The checked-in record intentionally remains `NOT_APPROVED`; CI must not infer owner approval from green tests or fill this file automatically.

Before a signed release candidate can be prepared, the owner must provide an approver, ISO approval date, approval reference, and evidence-backed `PASS` or `APPROVED` status for every required gate: Windows build/download, real musical A/B, FL Studio/host compatibility, preset and saved-state recall, licensing/attribution, updater/rollback, storefront and email signup, release notes, support contact, and pricing/trial decision. The pricing/trial gate also requires the actual approved decision. Every gate must carry a non-placeholder evidence reference.

The preparation script validates this record before it writes publisher URL or support email into generated VST3 metadata. Missing, malformed, placeholder or incomplete approval fails closed without mutating `moduleinfo.json`. Ordinary PR/main preview builds and their existing tests remain available; this additional record is required only by the signed version-tag preparation step. Do not add secrets or guessed business details to this JSON file.
