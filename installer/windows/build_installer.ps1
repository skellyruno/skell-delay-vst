param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Invalid installer version: $Version"
}
if ([string]::IsNullOrWhiteSpace($env:WIN_CERT_PFX) -or
    [string]::IsNullOrWhiteSpace($env:WIN_CERT_PASSWORD)) {
    throw 'WIN_CERT_PFX and WIN_CERT_PASSWORD are required for a tagged installer build.'
}

$pfx = Join-Path $env:RUNNER_TEMP "skell-delay-release-$PID.pfx"
try {
    $pfxBytes = [Convert]::FromBase64String($env:WIN_CERT_PFX)
    if ($pfxBytes.Length -eq 0) { throw 'The decoded signing certificate is empty.' }
    [IO.File]::WriteAllBytes($pfx, $pfxBytes)

    $bundle = Join-Path (Get-Location) 'build\DelayPlugin_artefacts\Release\VST3\Skell-Delay.vst3'
    if (-not (Test-Path -LiteralPath $bundle -PathType Container)) {
        throw "Missing Windows VST3 bundle: $bundle"
    }
    $modules = @(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.vst3')
    if ($modules.Count -ne 1) {
        throw "Expected exactly one VST3 module to sign; found $($modules.Count)."
    }

    function Sign-File {
        param([Parameter(Mandatory=$true)][string]$Path)
        & signtool sign /f $pfx /p $env:WIN_CERT_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Path
        if ($LASTEXITCODE -ne 0) {
            throw "signtool failed with exit code $LASTEXITCODE for $Path"
        }
    }

    Sign-File -Path $modules[0].FullName

    New-Item -ItemType Directory -Force -Path 'dist' | Out-Null
    & iscc "/DAppVersion=$Version" 'installer\windows\SkellDelay.iss'
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup Compiler failed with exit code $LASTEXITCODE"
    }

    $installers = @(Get-ChildItem -LiteralPath 'dist' -File -Filter '*.exe')
    if ($installers.Count -lt 1) {
        throw 'Inno Setup did not produce a Windows installer in dist/.'
    }
    foreach ($file in $installers) {
        Sign-File -Path $file.FullName
    }
}
finally {
    if (Test-Path -LiteralPath $pfx -PathType Leaf) {
        Remove-Item -LiteralPath $pfx -Force
    }
}
