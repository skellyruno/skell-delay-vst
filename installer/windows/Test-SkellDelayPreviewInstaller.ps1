param(
    [Parameter(Mandatory = $true)][string]$PackagePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'The preview installer test requires PowerShell 7 or newer.' }
$PackagePath = [System.IO.Path]::GetFullPath($PackagePath)
$Installer = Join-Path $PackagePath 'Install-SkellDelayPreview.ps1'
if (-not (Test-Path -LiteralPath $Installer -PathType Leaf)) { throw 'Installer script is missing from the staged package.' }

function Get-ModuleHash {
    param([string]$Bundle)
    $module = Join-Path $Bundle 'Contents/x86_64-win/Skell-Delay.vst3'
    return (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TreeDigest {
    param([string]$Bundle)
    $root = [System.IO.Path]::GetFullPath($Bundle)
    $records = foreach ($file in @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | Sort-Object FullName)) {
        $relative = [System.IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$relative|$hash|$($file.Length)"
    }
    $payload = (@($records) -join [System.Environment]::NewLine) + [System.Environment]::NewLine
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [System.Convert]::ToHexString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($payload))).ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Copy-Package {
    param([string]$Source, [string]$Destination)
    New-Item -ItemType Directory -Path $Destination | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $Source -Force)) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse
    }
}

function Invoke-ExpectedFailure {
    param([scriptblock]$Action, [string]$Label)
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Expected rejection did not occur: $Label" }
    Write-Host "PASS: $Label rejected before changing the installed version"
}

$testRoot = Join-Path $env:TEMP "SkellDelayInstallerTest-$([Guid]::NewGuid().ToString('N'))"
$ownerToken = [Guid]::NewGuid().ToString('N')
New-Item -ItemType Directory -Path $testRoot | Out-Null
$marker = Join-Path $testRoot '.skell-delay-installer-test-owner'
[System.IO.File]::WriteAllText($marker, $ownerToken, [System.Text.UTF8Encoding]::new($false))

try {
    $installRoot = Join-Path $testRoot 'user-vst3'
    $backupRoot = Join-Path $testRoot 'backups'
    $target = Join-Path $installRoot 'Skell-Delay.vst3'

    $dryRun = & $Installer -PackagePath $PackagePath -InstallRoot $installRoot -BackupRoot $backupRoot -WhatIf -Confirm:$false
    if ($dryRun.Status -cne 'WhatIf' -or (Test-Path -LiteralPath $installRoot)) {
        throw 'WhatIf created or changed an installation path.'
    }
    Write-Host 'PASS: WhatIf made no filesystem changes'

    $first = & $Installer -PackagePath $PackagePath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($first.Status -cne 'Installed' -or -not (Test-Path -LiteralPath $target -PathType Container)) {
        throw 'Fresh per-user install failed.'
    }
    $originalTree = Get-TreeDigest $target
    $originalModule = Get-ModuleHash $target
    Write-Host 'PASS: fresh per-user install'

    $second = & $Installer -PackagePath $PackagePath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($second.Status -cne 'AlreadyCurrent') { throw 'Repeated install was not idempotent.' }
    Write-Host 'PASS: idempotent install'

    $driveRoot = [System.IO.Path]::GetPathRoot($env:TEMP)
    $rootTarget = Join-Path $driveRoot 'Skell-Delay.vst3'
    $rootExisted = Test-Path -LiteralPath $rootTarget -PathType Container
    $rootDigest = if ($rootExisted) { Get-TreeDigest $rootTarget } else { $null }
    Invoke-ExpectedFailure -Label 'filesystem-root install destination' -Action {
        & $Installer -PackagePath $PackagePath -InstallRoot $driveRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ($rootExisted -and (Get-TreeDigest $rootTarget) -cne $rootDigest) {
        throw 'Filesystem-root rejection changed a pre-existing plugin directory.'
    }
    if (-not $rootExisted -and (Test-Path -LiteralPath $rootTarget)) {
        throw 'Filesystem-root rejection unexpectedly created a plugin directory.'
    }

    $unsafePackage = Join-Path $testRoot 'unsafe-path-package'
    Copy-Package -Source $PackagePath -Destination $unsafePackage
    $unsafeSumPath = Join-Path $unsafePackage 'SHA256SUMS.txt'
    $unsafeLines = @(Get-Content -LiteralPath $unsafeSumPath)
    $unsafeLines = @($unsafeLines | ForEach-Object {
        if ($_ -match '^([0-9a-f]{64})  build-info\.json$') { "$($Matches[1])  ../build-info.json" } else { $_ }
    })
    [System.IO.File]::WriteAllLines($unsafeSumPath, $unsafeLines, [System.Text.Encoding]::ASCII)
    Invoke-ExpectedFailure -Label 'checksum path traversal' -Action {
        & $Installer -PackagePath $unsafePackage -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $originalTree) { throw 'Unsafe checksum path changed the installed bundle.' }

    $tamperedPackage = Join-Path $testRoot 'tampered-package'
    Copy-Package -Source $PackagePath -Destination $tamperedPackage
    $tamperedModule = Join-Path $tamperedPackage 'Skell-Delay.vst3/Contents/x86_64-win/Skell-Delay.vst3'
    $bytes = [System.IO.File]::ReadAllBytes($tamperedModule)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
    [System.IO.File]::WriteAllBytes($tamperedModule, $bytes)
    Invoke-ExpectedFailure -Label 'tampered package checksum' -Action {
        & $Installer -PackagePath $tamperedPackage -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $originalTree) { throw 'Tampered package changed the installed bundle.' }

    $updatePackage = Join-Path $testRoot 'update-package'
    Copy-Package -Source $PackagePath -Destination $updatePackage
    # Simulate a real new plugin package, not just changed build provenance.
    # The installer correctly treats identical VST3 bundle bytes as AlreadyCurrent.
    $moduleRelative = 'Skell-Delay.vst3/Contents/Resources/moduleinfo.json'
    $moduleInfoPath = Join-Path $updatePackage 'Skell-Delay.vst3/Contents/Resources/moduleinfo.json'
    $moduleInfo = Get-Content -LiteralPath $moduleInfoPath -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $moduleInfo['Version'] = '1.0.1'
    foreach ($class in @($moduleInfo['Classes'])) {
        $class['Version'] = '1.0.1'
    }
    $moduleInfoJson = ($moduleInfo | ConvertTo-Json -Depth 100) + [System.Environment]::NewLine
    [System.IO.File]::WriteAllText($moduleInfoPath, $moduleInfoJson, [System.Text.UTF8Encoding]::new($false))
    $newModuleInfoHash = (Get-FileHash -LiteralPath $moduleInfoPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $newModuleInfoSize = [long](Get-Item -LiteralPath $moduleInfoPath).Length

    $manifestPath = Join-Path $updatePackage 'build-info.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable -Depth 100
    $manifest['version'] = '1.0.1'
    $manifest['moduleinfo']['sha256'] = $newModuleInfoHash
    $moduleRecordFound = $false
    foreach ($record in $manifest['bundle_files']) {
        if ([string]$record['path'] -ceq $moduleRelative) {
            $record['sha256'] = $newModuleInfoHash
            $record['size_bytes'] = $newModuleInfoSize
            $moduleRecordFound = $true
        }
    }
    if (-not $moduleRecordFound) { throw 'Update fixture could not find moduleinfo in the bundle manifest.' }
    $updatedJson = ($manifest | ConvertTo-Json -Depth 100) + [System.Environment]::NewLine
    [System.IO.File]::WriteAllText($manifestPath, $updatedJson, [System.Text.UTF8Encoding]::new($false))

    $sumPath = Join-Path $updatePackage 'SHA256SUMS.txt'
    $newManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sumLines = @(Get-Content -LiteralPath $sumPath)
    $sumLines = @($sumLines | ForEach-Object {
        if ($_ -match '^([0-9a-f]{64})  (.+)
    if ($update.Status -cne 'Updated' -or [string]::IsNullOrWhiteSpace($update.BackupPath) -or
        -not (Test-Path -LiteralPath $update.BackupPath -PathType Container)) {
        throw 'Update did not create a verified rollback backup.'
    }
    $updatedTree = Get-TreeDigest $target
    if ($updatedTree -ceq $originalTree) { throw 'Update did not change the installed package tree.' }
    Write-Host 'PASS: update staged, swapped and backed up the previous version'

    $tamperedBackup = Join-Path $testRoot 'tampered-backup'
    Copy-Package -Source $update.BackupPath -Destination $tamperedBackup
    $backupModule = Join-Path $tamperedBackup 'Skell-Delay.vst3/Contents/x86_64-win/Skell-Delay.vst3'
    $backupBytes = [System.IO.File]::ReadAllBytes($backupModule)
    $backupBytes[$backupBytes.Length - 1] = $backupBytes[$backupBytes.Length - 1] -bxor 1
    [System.IO.File]::WriteAllBytes($backupModule, $backupBytes)
    Invoke-ExpectedFailure -Label 'tampered rollback backup' -Action {
        & $Installer -RollbackBackup $tamperedBackup -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $updatedTree) { throw 'Tampered rollback backup changed the installed bundle.' }

    $rollback = & $Installer -RollbackBackup $update.BackupPath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($rollback.Status -cne 'RolledBack' -or (Get-TreeDigest $target) -cne $originalTree -or
        (Get-ModuleHash $target) -cne $originalModule) {
        throw 'Rollback did not restore the original bundle byte-for-byte.'
    }
    if ([string]::IsNullOrWhiteSpace($rollback.PreservedCurrentVersion) -or
        -not (Test-Path -LiteralPath $rollback.PreservedCurrentVersion -PathType Container)) {
        throw 'Rollback did not preserve the version it replaced.'
    }
    Write-Host 'PASS: rollback restored the original bundle and preserved the replaced version'
    Write-Host 'PASS: all private-preview installer integration checks completed'
}
finally {
    $safeToClean = $false
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $safeToClean = ([System.IO.File]::ReadAllText($marker) -ceq $ownerToken)
    }
    if ($safeToClean) {
        $reparse = @(Get-ChildItem -LiteralPath $testRoot -Force -Recurse | Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
        if ($reparse.Count -eq 0) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
        else {
            Write-Warning "Test directory contains a reparse point and was preserved: $testRoot"
        }
    }
    else {
        Write-Warning "Test ownership marker changed; preserving test directory: $testRoot"
    }
}
) {
            $relative = $Matches[2]
            if ($relative -ceq $moduleRelative) { "$newModuleInfoHash  $moduleRelative" }
            elseif ($relative -ceq 'build-info.json') { "$newManifestHash  build-info.json" }
            else { $_ }
        }
        else { $_ }
    })
    [System.IO.File]::WriteAllLines($sumPath, $sumLines, [System.Text.Encoding]::ASCII)

    $update = & $Installer -PackagePath $updatePackage -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($update.Status -cne 'Updated' -or [string]::IsNullOrWhiteSpace($update.BackupPath) -or
        -not (Test-Path -LiteralPath $update.BackupPath -PathType Container)) {
        throw 'Update did not create a verified rollback backup.'
    }
    $updatedTree = Get-TreeDigest $target
    if ($updatedTree -ceq $originalTree) { throw 'Update did not change the installed package tree.' }
    Write-Host 'PASS: update staged, swapped and backed up the previous version'

    $tamperedBackup = Join-Path $testRoot 'tampered-backup'
    Copy-Package -Source $update.BackupPath -Destination $tamperedBackup
    $backupModule = Join-Path $tamperedBackup 'Skell-Delay.vst3/Contents/x86_64-win/Skell-Delay.vst3'
    $backupBytes = [System.IO.File]::ReadAllBytes($backupModule)
    $backupBytes[$backupBytes.Length - 1] = $backupBytes[$backupBytes.Length - 1] -bxor 1
    [System.IO.File]::WriteAllBytes($backupModule, $backupBytes)
    Invoke-ExpectedFailure -Label 'tampered rollback backup' -Action {
        & $Installer -RollbackBackup $tamperedBackup -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $updatedTree) { throw 'Tampered rollback backup changed the installed bundle.' }

    $rollback = & $Installer -RollbackBackup $update.BackupPath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($rollback.Status -cne 'RolledBack' -or (Get-TreeDigest $target) -cne $originalTree -or
        (Get-ModuleHash $target) -cne $originalModule) {
        throw 'Rollback did not restore the original bundle byte-for-byte.'
    }
    if ([string]::IsNullOrWhiteSpace($rollback.PreservedCurrentVersion) -or
        -not (Test-Path -LiteralPath $rollback.PreservedCurrentVersion -PathType Container)) {
        throw 'Rollback did not preserve the version it replaced.'
    }
    Write-Host 'PASS: rollback restored the original bundle and preserved the replaced version'
    Write-Host 'PASS: all private-preview installer integration checks completed'
}
finally {
    $safeToClean = $false
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $safeToClean = ([System.IO.File]::ReadAllText($marker) -ceq $ownerToken)
    }
    if ($safeToClean) {
        $reparse = @(Get-ChildItem -LiteralPath $testRoot -Force -Recurse | Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
        if ($reparse.Count -eq 0) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
        else {
            Write-Warning "Test directory contains a reparse point and was preserved: $testRoot"
        }
    }
    else {
        Write-Warning "Test ownership marker changed; preserving test directory: $testRoot"
    }
}
) {
            $relative = $Matches[2]
            if ($relative -ceq $moduleRelative) { "$newModuleInfoHash  $moduleRelative" }
            elseif ($relative -ceq 'build-info.json') { "$newManifestHash  build-info.json" }
            else { $_ }
        }
        else { $_ }
    })
    [System.IO.File]::WriteAllLines($sumPath, $sumLines, [System.Text.Encoding]::ASCII)

    $update = & $Installer -PackagePath $updatePackage -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($update.Status -cne 'Updated' -or [string]::IsNullOrWhiteSpace($update.BackupPath) -or
        -not (Test-Path -LiteralPath $update.BackupPath -PathType Container)) {
        throw 'Update did not create a verified rollback backup.'
    }
    $updatedTree = Get-TreeDigest $target
    if ($updatedTree -ceq $originalTree) { throw 'Update did not change the installed package tree.' }
    Write-Host 'PASS: update staged, swapped and backed up the previous version'

    $tamperedBackup = Join-Path $testRoot 'tampered-backup'
    Copy-Package -Source $update.BackupPath -Destination $tamperedBackup
    $backupModule = Join-Path $tamperedBackup 'Skell-Delay.vst3/Contents/x86_64-win/Skell-Delay.vst3'
    $backupBytes = [System.IO.File]::ReadAllBytes($backupModule)
    $backupBytes[$backupBytes.Length - 1] = $backupBytes[$backupBytes.Length - 1] -bxor 1
    [System.IO.File]::WriteAllBytes($backupModule, $backupBytes)
    Invoke-ExpectedFailure -Label 'tampered rollback backup' -Action {
        & $Installer -RollbackBackup $tamperedBackup -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $updatedTree) { throw 'Tampered rollback backup changed the installed bundle.' }

    $rollback = & $Installer -RollbackBackup $update.BackupPath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($rollback.Status -cne 'RolledBack' -or (Get-TreeDigest $target) -cne $originalTree -or
        (Get-ModuleHash $target) -cne $originalModule) {
        throw 'Rollback did not restore the original bundle byte-for-byte.'
    }
    if ([string]::IsNullOrWhiteSpace($rollback.PreservedCurrentVersion) -or
        -not (Test-Path -LiteralPath $rollback.PreservedCurrentVersion -PathType Container)) {
        throw 'Rollback did not preserve the version it replaced.'
    }
    Write-Host 'PASS: rollback restored the original bundle and preserved the replaced version'
    Write-Host 'PASS: all private-preview installer integration checks completed'
}
finally {
    $safeToClean = $false
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $safeToClean = ([System.IO.File]::ReadAllText($marker) -ceq $ownerToken)
    }
    if ($safeToClean) {
        $reparse = @(Get-ChildItem -LiteralPath $testRoot -Force -Recurse | Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
        if ($reparse.Count -eq 0) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
        else {
            Write-Warning "Test directory contains a reparse point and was preserved: $testRoot"
        }
    }
    else {
        Write-Warning "Test ownership marker changed; preserving test directory: $testRoot"
    }
}
) {
            $relative = $Matches[2]
            if ($relative -ceq $moduleRelative) { "$newModuleInfoHash  $moduleRelative" }
            elseif ($relative -ceq 'build-info.json') { "$newManifestHash  build-info.json" }
            else { $_ }
        }
        else { $_ }
    })
    [System.IO.File]::WriteAllLines($sumPath, $sumLines, [System.Text.Encoding]::ASCII)

    $update = & $Installer -PackagePath $updatePackage -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($update.Status -cne 'Updated' -or [string]::IsNullOrWhiteSpace($update.BackupPath) -or
        -not (Test-Path -LiteralPath $update.BackupPath -PathType Container)) {
        throw 'Update did not create a verified rollback backup.'
    }
    $updatedTree = Get-TreeDigest $target
    if ($updatedTree -ceq $originalTree) { throw 'Update did not change the installed package tree.' }
    Write-Host 'PASS: update staged, swapped and backed up the previous version'

    $tamperedBackup = Join-Path $testRoot 'tampered-backup'
    Copy-Package -Source $update.BackupPath -Destination $tamperedBackup
    $backupModule = Join-Path $tamperedBackup 'Skell-Delay.vst3/Contents/x86_64-win/Skell-Delay.vst3'
    $backupBytes = [System.IO.File]::ReadAllBytes($backupModule)
    $backupBytes[$backupBytes.Length - 1] = $backupBytes[$backupBytes.Length - 1] -bxor 1
    [System.IO.File]::WriteAllBytes($backupModule, $backupBytes)
    Invoke-ExpectedFailure -Label 'tampered rollback backup' -Action {
        & $Installer -RollbackBackup $tamperedBackup -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    }
    if ((Get-TreeDigest $target) -cne $updatedTree) { throw 'Tampered rollback backup changed the installed bundle.' }

    $rollback = & $Installer -RollbackBackup $update.BackupPath -InstallRoot $installRoot -BackupRoot $backupRoot -Confirm:$false
    if ($rollback.Status -cne 'RolledBack' -or (Get-TreeDigest $target) -cne $originalTree -or
        (Get-ModuleHash $target) -cne $originalModule) {
        throw 'Rollback did not restore the original bundle byte-for-byte.'
    }
    if ([string]::IsNullOrWhiteSpace($rollback.PreservedCurrentVersion) -or
        -not (Test-Path -LiteralPath $rollback.PreservedCurrentVersion -PathType Container)) {
        throw 'Rollback did not preserve the version it replaced.'
    }
    Write-Host 'PASS: rollback restored the original bundle and preserved the replaced version'
    Write-Host 'PASS: all private-preview installer integration checks completed'
}
finally {
    $safeToClean = $false
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $safeToClean = ([System.IO.File]::ReadAllText($marker) -ceq $ownerToken)
    }
    if ($safeToClean) {
        $reparse = @(Get-ChildItem -LiteralPath $testRoot -Force -Recurse | Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
        if ($reparse.Count -eq 0) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
        else {
            Write-Warning "Test directory contains a reparse point and was preserved: $testRoot"
        }
    }
    else {
        Write-Warning "Test ownership marker changed; preserving test directory: $testRoot"
    }
}
