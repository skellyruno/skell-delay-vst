[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$PackagePath = $PSScriptRoot,
    [string]$InstallRoot = '',
    [string]$BackupRoot = '',
    [string]$RollbackBackup = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Product = 'Skell-Delay'
$Vendor = 'Skelly Audio'
$BundleName = 'Skell-Delay.vst3'
$ModuleRelative = 'Contents/x86_64-win/Skell-Delay.vst3'
$InfoRelative = 'Contents/Resources/moduleinfo.json'
$VersionPattern = '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$'
$CidPattern = '^[0-9A-Fa-f]{32}$'

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'A required path is empty.' }
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-NoReparseAncestors {
    param([Parameter(Mandatory = $true)][string]$Path)
    $cursor = Get-FullPath $Path
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing a reparse-point path component: $cursor"
            }
        }
        $parent = [System.IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { break }
        $cursor = $parent.FullName
    }
}

function Assert-NoReparsePoints {
    param([Parameter(Mandatory = $true)][string]$Root)
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Expected directory does not exist: $Root"
    }
    Assert-NoReparseAncestors $Root
    foreach ($item in @(Get-ChildItem -LiteralPath $Root -Force -Recurse)) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse point is not allowed in a preview package or backup: $($item.FullName)"
        }
    }
}

function Assert-UniqueJsonProperties {
    param(
        [Parameter(Mandatory = $true)][System.Text.Json.JsonElement]$Element,
        [string]$Location = '$'
    )
    if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
        $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw "Duplicate JSON property '$($property.Name)' at $Location"
            }
            Assert-UniqueJsonProperties -Element $property.Value -Location "$Location.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($child in $Element.EnumerateArray()) {
            Assert-UniqueJsonProperties -Element $child -Location "$Location[$index]"
            $index++
        }
    }
}

function Read-StrictJson {
    param([Parameter(Mandatory = $true)][string]$Path)
    $encoding = [System.Text.UTF8Encoding]::new($false, $true)
    $text = $encoding.GetString([System.IO.File]::ReadAllBytes($Path))
    $document = $null
    try {
        $document = [System.Text.Json.JsonDocument]::Parse($text)
        Assert-UniqueJsonProperties -Element $document.RootElement
        $value = ConvertFrom-Json -InputObject $text -AsHashtable -Depth 100 -ErrorAction Stop
        if ($value -isnot [System.Collections.IDictionary]) {
            throw 'JSON root must be an object.'
        }
        return $value
    }
    catch {
        throw "Strict JSON validation failed for '$Path': $($_.Exception.Message)"
    }
    finally {
        if ($null -ne $document) { $document.Dispose() }
    }
}

function Get-FileRecords {
    param([Parameter(Mandatory = $true)][string]$Root)
    $rootFull = Get-FullPath $Root
    $files = @(Get-ChildItem -LiteralPath $rootFull -File -Recurse -Force | Sort-Object FullName)
    $records = foreach ($file in $files) {
        $relative = [System.IO.Path]::GetRelativePath($rootFull, $file.FullName).Replace('\', '/')
        [pscustomobject]@{
            Path = $relative
            Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            SizeBytes = [long]$file.Length
        }
    }
    return @($records | Sort-Object Path -CaseSensitive)
}

function Get-TreeDigest {
    param([Parameter(Mandatory = $true)][string]$BundlePath)
    $records = @(Get-FileRecords $BundlePath)
    if ($records.Count -eq 0) { throw "Bundle contains no files: $BundlePath" }
    $lines = @($records | ForEach-Object { "$($_.Path)|$($_.Sha256)|$($_.SizeBytes)" })
    $payload = ($lines -join [System.Environment]::NewLine) + [System.Environment]::NewLine
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.Convert]::ToHexString($sha.ComputeHash($bytes)).ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Assert-ValidBundle {
    param([Parameter(Mandatory = $true)][string]$BundlePath)
    if (-not (Test-Path -LiteralPath $BundlePath -PathType Container)) {
        throw "Missing VST3 bundle: $BundlePath"
    }
    Assert-NoReparsePoints $BundlePath
    $module = Join-Path $BundlePath ($ModuleRelative.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
    $moduleInfo = Join-Path $BundlePath ($InfoRelative.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
    if (-not (Test-Path -LiteralPath $module -PathType Leaf)) { throw "Missing Windows x64 VST3 module: $ModuleRelative" }
    if (-not (Test-Path -LiteralPath $moduleInfo -PathType Leaf)) { throw "Missing VST3 moduleinfo.json: $InfoRelative" }

    $files = @(Get-ChildItem -LiteralPath $BundlePath -File -Recurse -Force)
    $modules = @($files | Where-Object { $_.Extension -ieq '.vst3' })
    if ($modules.Count -ne 1 -or $modules[0].FullName -ne (Get-FullPath $module)) {
        throw 'Bundle must contain exactly one VST3 module at Contents/x86_64-win/Skell-Delay.vst3.'
    }
    $forbidden = @($files | Where-Object { $_.Extension -in @('.exe', '.zip', '.component', '.vst2') })
    if ($forbidden.Count -gt 0) { throw "Unexpected non-VST3 payload in bundle: $($forbidden[0].FullName)" }

    $info = Read-StrictJson $moduleInfo
    if ($info.Name -cne $Product -or $info.'Factory Info'.Vendor -cne $Vendor) {
        throw 'Installed VST3 product/vendor identity does not match Skell-Delay / Skelly Audio.'
    }
    if ([string]$info.Version -notmatch $VersionPattern) { throw "Invalid VST3 version: $($info.Version)" }
    $classes = @($info.Classes)
    if ($classes.Count -ne 2) { throw 'VST3 metadata must contain exactly two classes.' }
    $categories = @($classes | ForEach-Object { [string]$_.Category } | Sort-Object -CaseSensitive)
    if (($categories -join '|') -cne 'Audio Module Class|Component Controller Class') {
        throw 'VST3 component/controller categories are missing or duplicated.'
    }
    $cids = @()
    foreach ($class in $classes) {
        if ($class.Name -cne $Product -or $class.Vendor -cne $Vendor -or [string]$class.Version -cne [string]$info.Version) {
            throw 'VST3 class identity does not match the bundle identity.'
        }
        if ([string]$class.CID -notmatch $CidPattern) { throw 'VST3 class contains an invalid 128-bit CID.' }
        $cids += ([string]$class.CID).ToLowerInvariant()
    }
    if (@($cids | Select-Object -Unique).Count -ne 2) { throw 'VST3 component/controller CIDs must be unique.' }

    $binary = [System.IO.File]::ReadAllBytes($module)
    if ($binary.Length -lt 0x100 -or [System.Text.Encoding]::ASCII.GetString($binary, 0, 2) -cne 'MZ') {
        throw 'VST3 module is not a valid Windows PE image.'
    }
    $peOffset = [System.BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0x40 -or $peOffset + 26 -gt $binary.Length) { throw 'PE header offset is invalid.' }
    if ($binary[$peOffset] -ne 80 -or $binary[$peOffset + 1] -ne 69 -or
        $binary[$peOffset + 2] -ne 0 -or $binary[$peOffset + 3] -ne 0) {
        throw 'PE signature is missing.'
    }
    $machine = [System.BitConverter]::ToUInt16($binary, $peOffset + 4)
    $optionalMagic = [System.BitConverter]::ToUInt16($binary, $peOffset + 24)
    $characteristics = [System.BitConverter]::ToUInt16($binary, $peOffset + 22)
    $sectionCount = [System.BitConverter]::ToUInt16($binary, $peOffset + 6)
    $optionalHeaderSize = [System.BitConverter]::ToUInt16($binary, $peOffset + 20)
    if ($machine -ne 0x8664 -or $optionalMagic -ne 0x20B -or ($characteristics -band 0x2000) -eq 0 -or
        $sectionCount -eq 0 -or $optionalHeaderSize -lt 0x70 -or
        $peOffset + 24 + $optionalHeaderSize -gt $binary.Length) {
        throw 'VST3 module must be a structurally valid AMD64 PE32+ DLL.'
    }

    return [pscustomobject]@{
        Version = [string]$info.Version
        ModuleInfo = $info
        ModulePath = $module
        ModuleInfoPath = $moduleInfo
        BinarySha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
        ModuleInfoSha256 = (Get-FileHash -LiteralPath $moduleInfo -Algorithm SHA256).Hash.ToLowerInvariant()
        ClassIds = @($cids | Sort-Object -CaseSensitive)
        BinarySize = [long](Get-Item -LiteralPath $module).Length
    }
}

function Assert-PreviewPackage {
    param([Parameter(Mandatory = $true)][string]$PackagePath)
    $root = Get-FullPath $PackagePath
    Assert-NoReparsePoints $root
    $expectedTop = @(
        $BundleName,
        'build-info.json',
        'SHA256SUMS.txt',
        'Install-SkellDelayPreview.ps1',
        'INSTALL_PREVIEW.md',
        'THIRD_PARTY_NOTICES.txt',
        'ThirdPartyLicenses'
    ) | Sort-Object -CaseSensitive
    # EULA is included only in a signed release candidate; the manifest check below
    # verifies that its presence agrees with the fail-closed legal approval state.
    if (Test-Path -LiteralPath (Join-Path $root 'EULA.txt') -PathType Leaf) {
        $expectedTop = @($expectedTop + 'EULA.txt' | Sort-Object -CaseSensitive)
    }
    $actualTop = @(Get-ChildItem -LiteralPath $root -Force | ForEach-Object { $_.Name } | Sort-Object -CaseSensitive)
    if (($actualTop -join '|') -cne ($expectedTop -join '|')) {
        throw "Windows VST3 package top-level contents are unexpected. Expected: $($expectedTop -join ', ')"
    }

    $bundle = Join-Path $root $BundleName
    $bundleCheck = Assert-ValidBundle $bundle
    $manifest = Read-StrictJson (Join-Path $root 'build-info.json')
    if ($manifest.schema_version -ne 1 -or $manifest.product -cne $Product -or $manifest.vendor -cne $Vendor -or
        $manifest.version -cne $bundleCheck.Version -or $manifest.format -cne 'VST3' -or
        $manifest.platform -cne 'windows-x64' -or
        $manifest.release_status -notin @('preview-only', 'signed-release-candidate') -or
        $manifest.installer.scope -notin @('per-user private preview', 'signed release candidate')) {
        throw 'Build manifest product, version, platform or release scope is invalid.'
    }
    if ([string]$manifest.source_commit -notmatch '^[0-9a-f]{40}$' -or
        [string]$manifest.ci_checkout_commit -notmatch '^[0-9a-f]{40}$' -or
        [string]$manifest.workflow_run_id -notmatch '^\d+$') {
        throw 'Build manifest provenance is incomplete.'
    }
    if ($manifest.validation.pluginval.strictness_level -ne 10 -or
        $manifest.validation.pluginval.result -cne 'passed' -or
        $manifest.validation.bundle_structure -cne 'passed' -or
        $manifest.validation.metadata_identity -cne 'passed' -or
        $manifest.validation.pe32_plus_amd64 -cne 'passed') {
        throw 'Build manifest does not record all required package validation gates.'
    }
    # Legal payloads are part of the package contract, not optional decoration.
    # Validate exact paths, manifest records and file hashes before any install/backup mutation.
    $noticeRelative = 'THIRD_PARTY_NOTICES.txt'
    $juceLicenseRelative = 'ThirdPartyLicenses/JUCE-LICENSE.md'
    $vst3LicenseRelative = 'ThirdPartyLicenses/VST3-SDK-LICENSE.txt'
    if ($manifest.validation.third_party_notice_packaging -cne 'passed' -or
        $manifest.validation.upstream_license_texts -cne 'included') {
        throw 'Build manifest does not record the required third-party legal payload.'
    }

    $noticeRecord = $manifest.legal.third_party_notices
    if ($noticeRecord.path -cne $noticeRelative -or
        [string]$noticeRecord.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Third-party notice manifest record is missing or invalid.'
    }
    $noticePath = Join-Path $root $noticeRelative
    if (-not (Test-Path -LiteralPath $noticePath -PathType Leaf)) {
        throw 'Third-party notice file is missing from the package.'
    }
    if ((Get-FileHash -LiteralPath $noticePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $noticeRecord.sha256) {
        throw 'Third-party notice hash does not match the build manifest.'
    }

    $licenseRecords = @($manifest.legal.upstream_license_texts)
    if ($licenseRecords.Count -ne 2) {
        throw 'Build manifest must contain exactly the pinned JUCE and VST3 SDK license records.'
    }
    foreach ($relative in @($juceLicenseRelative, $vst3LicenseRelative)) {
        $matches = @($licenseRecords | Where-Object { $_.path -ceq $relative })
        if ($matches.Count -ne 1) {
            throw "Build manifest is missing or duplicates the upstream license record: $relative"
        }
        $record = $matches[0]
        if ([string]$record.sha256 -notmatch '^[0-9a-f]{64}$' -or [long]$record.size_bytes -le 200) {
            throw "Upstream license record is invalid: $relative"
        }
        $licensePath = Join-Path $root ($relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
        if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
            throw "Upstream license file is missing: $relative"
        }
        $licenseItem = Get-Item -LiteralPath $licensePath -Force
        if ([long]$licenseItem.Length -ne [long]$record.size_bytes -or
            (Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256) {
            throw "Upstream license size/hash does not match the build manifest: $relative"
        }
    }

    $legalStatus = [string]$manifest.legal.status
    $hasEula = Test-Path -LiteralPath (Join-Path $root 'EULA.txt') -PathType Leaf
    if ($legalStatus -ceq 'not-approved-for-distribution') {
        if ($manifest.release_status -cne 'preview-only' -or
            $manifest.validation.owner_legal_approval -cne 'not-approved-preview-only' -or
            $null -ne $manifest.legal.eula -or $hasEula -or
            $null -ne $manifest.legal.approval_record_sha256) {
            throw 'Preview package must remain explicitly unapproved and must not contain an EULA.'
        }
    }
    elseif ($legalStatus -ceq 'owner-approved') {
        $eulaRecord = $manifest.legal.eula
        if ($manifest.release_status -cne 'signed-release-candidate' -or
            $manifest.validation.owner_legal_approval -cne 'passed' -or
            -not $hasEula -or $null -eq $eulaRecord -or
            $eulaRecord.path -cne 'EULA.txt' -or
            [string]$eulaRecord.sha256 -notmatch '^[0-9a-f]{64}$' -or
            [string]$manifest.legal.approval_record_sha256 -notmatch '^[0-9a-f]{64}$') {
            throw 'Signed release candidate must contain an owner-approved EULA and approval-record digest.'
        }
        $eulaPath = Join-Path $root 'EULA.txt'
        if ((Get-FileHash -LiteralPath $eulaPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $eulaRecord.sha256) {
            throw 'EULA hash does not match the owner-approved build manifest.'
        }
    }
    else {
        throw "Unknown legal approval status in build manifest: $legalStatus"
    }
    if ($manifest.binary.path -cne "$BundleName/$ModuleRelative" -or
        $manifest.binary.sha256 -cne $bundleCheck.BinarySha256 -or
        [long]$manifest.binary.size_bytes -ne $bundleCheck.BinarySize -or
        $manifest.binary.architecture -cne 'AMD64' -or $manifest.binary.pe_format -cne 'PE32+') {
        throw 'Build manifest binary identity, size or architecture does not match the bundle.'
    }
    if ($manifest.moduleinfo.path -cne "$BundleName/$InfoRelative" -or
        $manifest.moduleinfo.sha256 -cne $bundleCheck.ModuleInfoSha256 -or
        $manifest.moduleinfo.strict_json -ne $true) {
        throw 'Build manifest moduleinfo hash or strict-JSON assertion does not match.'
    }
    $manifestCids = @($manifest.moduleinfo.unique_class_ids | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object -CaseSensitive)
    if (($manifestCids -join '|') -cne ($bundleCheck.ClassIds -join '|')) {
        throw 'Build manifest component/controller CIDs do not match moduleinfo.json.'
    }

    $actualBundleRecords = @(Get-FileRecords $bundle)
    $packageBundleRecords = foreach ($record in $actualBundleRecords) {
        [pscustomobject]@{
            Path = "$BundleName/$($record.Path)"
            Sha256 = $record.Sha256
            SizeBytes = $record.SizeBytes
        }
    }
    $manifestBundleRecords = @($manifest.bundle_files)
    if ($manifestBundleRecords.Count -ne $packageBundleRecords.Count) {
        throw 'Build manifest bundle file inventory is incomplete.'
    }
    $bundleMap = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::Ordinal)
    foreach ($record in $manifestBundleRecords) {
        $path = [string]$record.path
        if ($bundleMap.ContainsKey($path)) { throw "Duplicate bundle manifest path: $path" }
        $bundleMap.Add($path, $record)
    }
    foreach ($record in $packageBundleRecords) {
        if (-not $bundleMap.ContainsKey($record.Path)) { throw "Bundle file missing from build manifest: $($record.Path)" }
        $entry = $bundleMap[$record.Path]
        if ($entry.sha256 -cne $record.Sha256 -or [long]$entry.size_bytes -ne $record.SizeBytes) {
            throw "Bundle file hash/size mismatch: $($record.Path)"
        }
    }

    $installerScript = Join-Path $root 'Install-SkellDelayPreview.ps1'
    $guide = Join-Path $root 'INSTALL_PREVIEW.md'
    if ($manifest.installer.script.path -cne 'Install-SkellDelayPreview.ps1' -or
        $manifest.installer.guide.path -cne 'INSTALL_PREVIEW.md' -or
        $manifest.installer.script.sha256 -cne (Get-FileHash -LiteralPath $installerScript -Algorithm SHA256).Hash.ToLowerInvariant() -or
        $manifest.installer.guide.sha256 -cne (Get-FileHash -LiteralPath $guide -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'Installer or guide checksum does not match the build manifest.'
    }

    $expectedSumPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($record in $packageBundleRecords) { [void]$expectedSumPaths.Add($record.Path) }
    [void]$expectedSumPaths.Add('build-info.json')
    [void]$expectedSumPaths.Add('Install-SkellDelayPreview.ps1')
    [void]$expectedSumPaths.Add('INSTALL_PREVIEW.md')
    [void]$expectedSumPaths.Add($noticeRelative)
    [void]$expectedSumPaths.Add($juceLicenseRelative)
    [void]$expectedSumPaths.Add($vst3LicenseRelative)
    if ($legalStatus -ceq 'owner-approved') { [void]$expectedSumPaths.Add('EULA.txt') }
    $sumMap = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::Ordinal)
    foreach ($line in @(Get-Content -LiteralPath (Join-Path $root 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed SHA256SUMS line: $line" }
        $relative = $Matches[2]
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative.Contains('\') -or
            @($relative.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0) {
            throw "Unsafe checksum path: $relative"
        }
        if ($sumMap.ContainsKey($relative)) { throw "Duplicate checksum path: $relative" }
        $sumMap.Add($relative, $Matches[1])
    }
    if ($sumMap.Count -ne $expectedSumPaths.Count) { throw 'SHA256SUMS does not cover the exact package file inventory.' }
    foreach ($relative in $expectedSumPaths) {
        if (-not $sumMap.ContainsKey($relative)) { throw "SHA256SUMS is missing: $relative" }
        $native = $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        $file = Join-Path $root $native
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Checksum target is missing: $relative" }
        $actualHash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -cne $sumMap[$relative]) { throw "SHA-256 mismatch: $relative" }
    }

    return [pscustomobject]@{
        Root = $root
        BundlePath = $bundle
        Version = $bundleCheck.Version
        BundleTreeSha256 = Get-TreeDigest $bundle
        Manifest = $manifest
    }
}

function Copy-BundleContents {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    New-Item -ItemType Directory -Path $Destination | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $Source -Force)) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse
    }
}

function New-VerifiedBackup {
    param(
        [Parameter(Mandatory = $true)][string]$BundlePath,
        [Parameter(Mandatory = $true)][string]$BackupRoot
    )
    $bundleCheck = Assert-ValidBundle $BundlePath
    Assert-NoReparseAncestors $BackupRoot
    if (-not (Test-Path -LiteralPath $BackupRoot -PathType Container)) {
        New-Item -ItemType Directory -Path $BackupRoot | Out-Null
    }
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $name = "Skell-Delay-$($bundleCheck.Version)-$stamp-$([Guid]::NewGuid().ToString('N'))"
    $temporary = Join-Path $BackupRoot ".$name.tmp"
    $final = Join-Path $BackupRoot $name
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $backupBundle = Join-Path $temporary $BundleName
    Copy-BundleContents -Source $BundlePath -Destination $backupBundle
    $treeDigest = Get-TreeDigest $backupBundle
    $info = [ordered]@{
        schema_version = 1
        product = $Product
        vendor = $Vendor
        version = $bundleCheck.Version
        created_utc = [DateTime]::UtcNow.ToString('o')
        bundle_tree_sha256 = $treeDigest
    }
    $json = ($info | ConvertTo-Json -Depth 10) + [System.Environment]::NewLine
    [System.IO.File]::WriteAllText((Join-Path $temporary 'backup-info.json'), $json, [System.Text.UTF8Encoding]::new($false))
    if ((Get-TreeDigest $backupBundle) -cne $treeDigest) { throw 'Backup verification failed before finalization.' }
    Move-Item -LiteralPath $temporary -Destination $final
    return $final
}

function Assert-VerifiedBackup {
    param([Parameter(Mandatory = $true)][string]$BackupPath)
    $root = Get-FullPath $BackupPath
    Assert-NoReparsePoints $root
    $expected = @($BundleName, 'backup-info.json') | Sort-Object -CaseSensitive
    $actual = @(Get-ChildItem -LiteralPath $root -Force | ForEach-Object { $_.Name } | Sort-Object -CaseSensitive)
    if (($actual -join '|') -cne ($expected -join '|')) { throw 'Rollback backup has unexpected contents.' }
    $info = Read-StrictJson (Join-Path $root 'backup-info.json')
    if ($info.schema_version -ne 1 -or $info.product -cne $Product -or $info.vendor -cne $Vendor) {
        throw 'Rollback backup identity/schema is invalid.'
    }
    $bundle = Join-Path $root $BundleName
    $check = Assert-ValidBundle $bundle
    if ($info.version -cne $check.Version) { throw 'Rollback backup version does not match its VST3 metadata.' }
    $digest = Get-TreeDigest $bundle
    if ($info.bundle_tree_sha256 -cne $digest) { throw 'Rollback backup hash verification failed.' }
    return [pscustomobject]@{ BundlePath = $bundle; Version = $check.Version; BundleTreeSha256 = $digest }
}

function Invoke-InstallSwap {
    param(
        [Parameter(Mandatory = $true)][string]$SourceBundle,
        [Parameter(Mandatory = $true)][string]$InstallRoot,
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$ExpectedDigest
    )
    Assert-NoReparseAncestors $InstallRoot
    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        New-Item -ItemType Directory -Path $InstallRoot | Out-Null
    }
    $token = [Guid]::NewGuid().ToString('N')
    $stage = Join-Path $InstallRoot ".Skell-Delay.stage.$token"
    $previous = Join-Path $InstallRoot ".Skell-Delay.previous.$token"
    Copy-BundleContents -Source $SourceBundle -Destination $stage
    if ((Get-TreeDigest $stage) -cne $ExpectedDigest) {
        throw 'Staged VST3 hash differs from the validated source; installation was not changed.'
    }
    $hadPrevious = Test-Path -LiteralPath $TargetPath -PathType Container
    if ($hadPrevious) { Move-Item -LiteralPath $TargetPath -Destination $previous }
    try {
        Move-Item -LiteralPath $stage -Destination $TargetPath
        if ((Get-TreeDigest $TargetPath) -cne $ExpectedDigest) {
            throw 'Installed VST3 hash differs from the validated source.'
        }
    }
    catch {
        if (Test-Path -LiteralPath $TargetPath) {
            $failed = Join-Path $InstallRoot ".Skell-Delay.failed.$token"
            Move-Item -LiteralPath $TargetPath -Destination $failed
        }
        if ($hadPrevious -and (Test-Path -LiteralPath $previous -PathType Container)) {
            Move-Item -LiteralPath $previous -Destination $TargetPath
        }
        throw
    }
    if ($hadPrevious -and (Test-Path -LiteralPath $previous -PathType Container)) {
        try { Remove-Item -LiteralPath $previous -Recurse -Force }
        catch { Write-Warning "The verified prior copy remains at $previous; cleanup was not required for the successful install." }
    }
}

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable; pass -InstallRoot explicitly.' }
    $InstallRoot = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
}
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable; pass -BackupRoot explicitly.' }
    $BackupRoot = Join-Path $env:LOCALAPPDATA 'Skelly\Skell-Delay\backups'
}
$InstallRoot = Get-FullPath $InstallRoot
$BackupRoot = Get-FullPath $BackupRoot
$TargetPath = Join-Path $InstallRoot $BundleName
$trimChars = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
$installVolume = [System.IO.Path]::GetPathRoot($InstallRoot)
$backupVolume = [System.IO.Path]::GetPathRoot($BackupRoot)
if ($InstallRoot.TrimEnd($trimChars) -ceq $installVolume.TrimEnd($trimChars) -or
    $BackupRoot.TrimEnd($trimChars) -ceq $backupVolume.TrimEnd($trimChars)) {
    throw 'Refusing a filesystem-root install or backup destination.'
}
$installPrefix = $InstallRoot.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar
if ($BackupRoot.TrimEnd($trimChars).Equals($InstallRoot.TrimEnd($trimChars), [System.StringComparison]::OrdinalIgnoreCase) -or
    $BackupRoot.StartsWith($installPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BackupRoot must be separate from and outside the VST3 scan/install directory.'
}
Assert-NoReparseAncestors $InstallRoot
Assert-NoReparseAncestors $BackupRoot
if (Test-Path -LiteralPath $TargetPath) { Assert-NoReparsePoints $TargetPath }

if (-not [string]::IsNullOrWhiteSpace($RollbackBackup)) {
    $backup = Assert-VerifiedBackup $RollbackBackup
    $currentVersion = $null
    $currentDigest = $null
    if (Test-Path -LiteralPath $TargetPath -PathType Container) {
        $current = Assert-ValidBundle $TargetPath
        $currentVersion = $current.Version
        $currentDigest = Get-TreeDigest $TargetPath
    }
    if ($currentDigest -and $currentDigest -ceq $backup.BundleTreeSha256) {
        return [pscustomobject]@{ Status = 'AlreadyCurrent'; InstallPath = $TargetPath; BackupPath = $RollbackBackup; Version = $backup.Version }
    }
    if (-not $PSCmdlet.ShouldProcess($TargetPath, "Rollback to verified Skell-Delay $($backup.Version)")) {
        return [pscustomobject]@{ Status = 'WhatIf'; InstallPath = $TargetPath; BackupPath = $RollbackBackup; Version = $backup.Version }
    }
    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { New-Item -ItemType Directory -Path $InstallRoot | Out-Null }
    $currentBackup = $null
    if ($currentVersion) { $currentBackup = New-VerifiedBackup -BundlePath $TargetPath -BackupRoot $BackupRoot }
    Invoke-InstallSwap -SourceBundle $backup.BundlePath -InstallRoot $InstallRoot -TargetPath $TargetPath -ExpectedDigest $backup.BundleTreeSha256
    return [pscustomobject]@{
        Status = 'RolledBack'
        InstallPath = $TargetPath
        BackupPath = $RollbackBackup
        PreservedCurrentVersion = $currentBackup
        Version = $backup.Version
    }
}

$package = Assert-PreviewPackage $PackagePath
$packagePrefix = $package.Root.TrimEnd($trimChars) + [System.IO.Path]::DirectorySeparatorChar
if ($InstallRoot.TrimEnd($trimChars) -ceq $package.Root.TrimEnd($trimChars) -or
    $InstallRoot.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'InstallRoot must not be the preview package directory or a child of it.'
}
$currentVersion = $null
$currentDigest = $null
if (Test-Path -LiteralPath $TargetPath -PathType Container) {
    $current = Assert-ValidBundle $TargetPath
    $currentVersion = $current.Version
    $currentDigest = Get-TreeDigest $TargetPath
}
if ($currentDigest -and $currentDigest -ceq $package.BundleTreeSha256) {
    return [pscustomobject]@{ Status = 'AlreadyCurrent'; InstallPath = $TargetPath; Version = $package.Version }
}
$action = if ($currentVersion) { 'Update' } else { 'Install' }
if (-not $PSCmdlet.ShouldProcess($TargetPath, "$action private-preview Skell-Delay $($package.Version)")) {
    return [pscustomobject]@{ Status = 'WhatIf'; InstallPath = $TargetPath; Version = $package.Version }
}
if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { New-Item -ItemType Directory -Path $InstallRoot | Out-Null }
$backupPath = $null
if ($currentVersion) { $backupPath = New-VerifiedBackup -BundlePath $TargetPath -BackupRoot $BackupRoot }
Invoke-InstallSwap -SourceBundle $package.BundlePath -InstallRoot $InstallRoot -TargetPath $TargetPath -ExpectedDigest $package.BundleTreeSha256
$status = if ($currentVersion) { 'Updated' } else { 'Installed' }
return [pscustomobject]@{
    Status = $status
    InstallPath = $TargetPath
    BackupPath = $backupPath
    PreviousVersion = $currentVersion
    Version = $package.Version
    BundleTreeSha256 = $package.BundleTreeSha256
}
