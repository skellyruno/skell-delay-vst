param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$pfx = "$env:RUNNER_TEMP\cert.pfx"
[IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WIN_CERT_PFX))
$sign = { param($f) & signtool sign /f $pfx /p $env:WIN_CERT_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $f }
Get-ChildItem -Recurse build -Filter "Skell Delay.vst3" | ForEach-Object { Get-ChildItem $_.FullName -Recurse -Filter *.vst3 -File | ForEach-Object { & $sign $_.FullName } }
& iscc "/DAppVersion=$Version" installer\windows\SkellDelay.iss
Get-ChildItem dist -Filter *.exe | ForEach-Object { & $sign $_.FullName }
Remove-Item $pfx
