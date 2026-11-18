# Skell-Delay private Windows VST3 preview installer

This is an **unsigned, private engineering preview**, not a commercial release. It does not imply FL Studio acceptance, owner listening approval, licensing approval or code signing.

## Requirements

- Windows x64.
- PowerShell 7 or newer.
- The complete extracted package directory from the GitHub Actions preview ZIP.
- Keep the installer script, INSTALL_PREVIEW.md, build-info.json, SHA256SUMS.txt and Skell-Delay.vst3 together.

## Preview install

Open PowerShell 7 in the extracted package directory.

First inspect the proposed operation:

    .\Install-SkellDelayPreview.ps1 -WhatIf

Then explicitly install the per-user preview:

    .\Install-SkellDelayPreview.ps1 -Confirm

Default destination:

    %LOCALAPPDATA%\Programs\Common\VST3\Skell-Delay.vst3

No administrator elevation, Program Files writes, registry edits, global plugin-folder changes, DAW project changes or preset changes are performed.

The installer verifies the complete package checksum inventory, strict JSON and duplicate-key rules, product/vendor/version, VST3 processor/controller CIDs, AMD64 PE32+ module, bundle structure and the recorded pluginval strictness-10 result before changing the install directory.

## Update and rollback

Run the same installer with a newer validated preview package. Before replacing an existing installation, it creates and verifies a backup under:

    %LOCALAPPDATA%\Skelly\Skell-Delay\backups

The installer stages the new bundle, verifies its complete file-tree digest, swaps it into place and restores the previous directory if the swap fails.

To roll back, supply the exact backup directory printed by the update result:

    .\Install-SkellDelayPreview.ps1 -RollbackBackup "C:\Users\<you>\AppData\Local\Skelly\Skell-Delay\backups\<backup-folder>" -Confirm

Rollback first verifies the backup's identity and tree digest. It also preserves the currently installed version as another verified backup before restoring the selected version.

The installer refuses malformed/tampered packages, duplicate or unsafe checksum paths, reparse points, an invalid existing install, and unverified backups. It does not delete or overwrite unrelated directories. The only cleanup is a uniquely named temporary swap directory after the new installation has been verified.

## Testing

The Windows release workflow runs installer/windows/Test-SkellDelayPreviewInstaller.ps1 against the actual staged package. It exercises WhatIf, fresh install, idempotence, tamper rejection, update, verified backup, tampered-backup rejection and byte-for-byte rollback.

The normal signed Inno Setup installer remains a separate tag-only path requiring owner-provided signing secrets. This per-user script is the private-preview route.
