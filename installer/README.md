# Installers and signing

Nothing here has been run yet. It needs your accounts, and the first run will want small fixes.

## Secrets (GitHub repository settings -> Secrets)
| Secret | What |
|---|---|
| `APPLE_CERT_P12`, `APPLE_CERT_PASSWORD` | Base64 of your "Developer ID Application" + "Developer ID Installer" certificates (.p12) and its password |
| `APPLE_APP_IDENTITY` | e.g. `Developer ID Application: Your Name (TEAMID)` |
| `APPLE_INSTALLER_IDENTITY` | e.g. `Developer ID Installer: Your Name (TEAMID)` |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | For `notarytool` (app-specific password) |
| `WIN_CERT_PFX`, `WIN_CERT_PASSWORD` | Base64 of an OV/EV code-signing certificate (.pfx) and its password |

Push a tag like `v1.0.0` to build, test, sign and package. Without the secrets the workflow still builds and tests.

## Windows
`windows/SkellDelay.iss` (Inno Setup) installs the VST3 to `C:\Program Files\Common Files\VST3`. `windows/build_installer.ps1` signs the VST3, builds the installer and signs it.

## macOS
`macos/build_pkg.sh` signs the VST3 and AU, builds a `.pkg`, notarises and staples it. Installs to `/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`.

## Licence keys
There is no copy protection yet. If you sell directly, decide first (see `legal/README.md`) whether you want activation at all; most small plugin makers use a simple licence file or serial and keep it light.
