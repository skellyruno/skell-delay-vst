# Windows VST3 installer and signing

The current Skell-Delay product target is **Windows x64 VST3 only**. The release workflow builds, validates and uploads the VST3 preview. A signed installer is optional and is produced only for an approved version tag when the owner-provided Windows signing secrets are configured.

## Windows

- Inno Setup definition: `windows/SkellDelay.iss`
- Build/sign script: `windows/build_installer.ps1`
- Install destination: `C:\\Program Files\\Common Files\\VST3`
- Required secrets for signing: `WIN_CERT_PFX` and `WIN_CERT_PASSWORD`

Without those secrets, the workflow still compiles, runs DSP/realtime tests, validates the Windows VST3 with pluginval strictness 10, stages provenance/checksums and uploads a preview-only VST3 package. It does not claim that the package is signed.

## Deferred platform files

The existing `macos/` scripts are preserved as historical source, but the current CMake target and release workflow do not build AU, macOS or Linux product artifacts. Do not treat those scripts as supported release paths or publish those formats without a separately approved platform decision.

## Licensing and activation

There is no copy protection yet. Before direct sales, decide whether activation is needed; a simple licence file or serial may be sufficient. Confirm the JUCE licence tier, VST3 SDK terms, artwork rights and EULA before commercial distribution.
