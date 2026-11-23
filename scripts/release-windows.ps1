# Copyright (C) 2026 lnson
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds, optionally Authenticode-signs, verifies, packages, and publishes
# OKeyTre for Windows (`build\OKeyTre-v<version>-windows-x64.zip`).
#
# Usage:
#   .\scripts\release-windows.ps1 [Version] [options]
#
# Examples:
#   .\scripts\release-windows.ps1                          # Run tests, build static-CRT release zip
#   .\scripts\release-windows.ps1 0.4 -Open                # Bump Windows version to 0.4, build zip, reveal in Explorer
#   .\scripts\release-windows.ps1 -Publish                 # Build & upload/create GitHub Release for current version
#   .\scripts\release-windows.ps1 -PublishOnly             # Upload existing zip to GitHub Release without rebuilding

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Version,
    [string]$FlexBisonDir,
    [string]$SignCertThumbprint,
    [string]$SignPfxPath,
    [string]$SignPfxPassword,
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [switch]$SkipTests,
    [switch]$Open,
    [switch]$Publish,
    [switch]$PublishOnly,
    [switch]$Prerelease
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

$GitHubRepo = "lnson/OKeyTreIME"
$AppDisplayName = "$([char]0x00D4)K$([char]0x00EA)yTre"
$ResourceHeaderPath = Join-Path $RepoRoot "windows\resources\resource.h"
$ManifestPath = Join-Path $RepoRoot "windows\resources\okeytre.manifest"
$AppVersionPath = Join-Path $RepoRoot "windows\app-version.h"

# Refresh PATH from Registry so tools installed after opening this shell are found.
$regPath = @()
$machinePath = [Environment]::GetEnvironmentVariable("PATH", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($FlexBisonDir) { $regPath += $FlexBisonDir }
if ($env:PATH) { $regPath += $env:PATH }
if ($machinePath) { $regPath += $machinePath }
if ($userPath) { $regPath += $userPath }
$wingetLinks = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links"
if (Test-Path $wingetLinks) { $regPath += $wingetLinks }
$bazeliskPkg = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\Bazel.Bazelisk*") -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if ($bazeliskPkg) { $regPath += $bazeliskPkg.FullName }
$env:PATH = ($regPath -join ";")

if (-not $env:BAZEL_SH) {
    $userBazelSh = [Environment]::GetEnvironmentVariable("BAZEL_SH", "User")
    if ($userBazelSh -and (Test-Path $userBazelSh)) {
        $env:BAZEL_SH = $userBazelSh
    } elseif (Test-Path "C:\Program Files\Git\bin\bash.exe") {
        $env:BAZEL_SH = "C:\Program Files\Git\bin\bash.exe"
    }
}

function Find-Tool([string[]]$Names) {
    foreach ($name in $Names) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    return $null
}

function Find-WindowsSdkTool([string]$ExeName) {
    $onPath = Find-Tool @($ExeName)
    if ($onPath) { return $onPath }
    $found = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\$ExeName" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

function Get-CurrentWindowsVersion {
    $raw = Get-Content $ResourceHeaderPath -Raw
    if ($raw -match 'OKT_VERSION_STRING\s+"([^"]+)"') {
        return $Matches[1]
    }
    throw "Unable to parse OKT_VERSION_STRING from $ResourceHeaderPath"
}

function Update-WindowsVersion([string]$TargetVersion) {
    if ($TargetVersion -notmatch '^([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?(?:-[0-9A-Za-z.-]+)?$') {
        throw "Invalid version '$TargetVersion' (expected e.g. 0.3, 0.3.1, or 0.4-beta.1)"
    }
    $major = [int]$Matches[1]
    $minor = if ($Matches[2]) { [int]$Matches[2] } else { 0 }
    $patch = if ($Matches[3]) { [int]$Matches[3] } else { 0 }
    $quad = "$major.$minor.$patch.0"

    $resContent = Get-Content $ResourceHeaderPath -Raw
    $resContent = [regex]::Replace($resContent, '#define OKT_VERSION_MAJOR \d+', "#define OKT_VERSION_MAJOR $major")
    $resContent = [regex]::Replace($resContent, '#define OKT_VERSION_MINOR \d+', "#define OKT_VERSION_MINOR $minor")
    $resContent = [regex]::Replace($resContent, '#define OKT_VERSION_PATCH \d+', "#define OKT_VERSION_PATCH $patch")
    $resContent = [regex]::Replace($resContent, '#define OKT_VERSION_STRING "[^"]+"', "#define OKT_VERSION_STRING `"$TargetVersion`"")
    Set-Content -Path $ResourceHeaderPath -Value $resContent -NoNewline -Encoding UTF8

    $manifestContent = Get-Content $ManifestPath -Raw
    $manifestContent = [regex]::Replace($manifestContent, 'version="\d+\.\d+\.\d+\.\d+"', "version=`"$quad`"", 1)
    Set-Content -Path $ManifestPath -Value $manifestContent -NoNewline -Encoding UTF8

    $appVerContent = Get-Content $AppVersionPath -Raw
    $appVerContent = [regex]::Replace($appVerContent, 'kCurrentAppVersionString\s*=\s*"[^"]+"', "kCurrentAppVersionString = `"$TargetVersion`"")
    Set-Content -Path $AppVersionPath -Value $appVerContent -NoNewline -Encoding UTF8
}

$CurrentVersion = Get-CurrentWindowsVersion
if ($Version) {
    $Normalized = $Version -replace '^[vV]', ''
    if ($Normalized -ne $CurrentVersion) {
        Write-Host "==> Updating Windows version: $CurrentVersion -> $Normalized"
        Update-WindowsVersion $Normalized
    }
    $ResolvedVersion = $Normalized
} else {
    $ResolvedVersion = $CurrentVersion
}

$Tag = "v$ResolvedVersion"
$StageDir = Join-Path $RepoRoot "build\windows-release"
$ZipPath = Join-Path $RepoRoot "build\OKeyTre-$Tag-windows-x64.zip"
$ExePath = Join-Path $RepoRoot "bazel-bin\windows\OKeyTre.exe"
$StagedExe = Join-Path $StageDir "OKeyTre.exe"
$IsSigned = $false

if ($PublishOnly) {
    $Publish = $true
    if (-not (Test-Path $ZipPath)) {
        throw "Expected existing archive at '$ZipPath', but it was not found."
    }
    Write-Host "==> Reusing existing archive: $ZipPath"
} else {
    $bazel = Find-Tool @("bazel", "bazelisk")
    if (-not $bazel) {
        throw "Bazel / Bazelisk was not found on PATH. Install via: winget install Bazel.Bazelisk"
    }
    $flex = Find-Tool @("win_flex", "flex")
    $bison = Find-Tool @("win_bison", "bison")
    if (-not $flex -or -not $bison) {
        throw "Flex and Bison (win_flex/win_bison or flex/bison) must be on PATH, or pass -FlexBisonDir <path>."
    }

    # Warn early if OKeyTre.exe is currently running from bazel-bin / bazel-out.
    $runningBazelExe = Get-Process -Name "OKeyTre" -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and ($_.Path -like "*bazel-bin*" -or $_.Path -like "*bazel-out*")
    }
    if ($runningBazelExe) {
        throw "OKeyTre.exe is currently running from bazel-bin (PID $($runningBazelExe.Id)). Quit it from the system tray before building a release."
    }

    Write-Host "==> Preparing OKeyTre $Tag (Windows x64, static VC++ runtime)"

    if (-not $SkipTests) {
        Write-Host "==> Running C++ engine, compiler, and Windows logic unit tests..."
        & $bazel test //OKeyTre/ime/core/... //imec/... //invc/... //windows/...
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    Write-Host "==> Building optimized standalone OKeyTre.exe (static CRT)..."
    & $bazel build --config=release --features=static_link_msvcrt //:OKeyTreWindows
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not (Test-Path $ExePath)) {
        throw "Expected built executable at '$ExePath', but it was not found."
    }

    # Verify static CRT (no dynamic MSVCP140 / VCRUNTIME140 dependency).
    $dumpbin = (Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1).FullName
    if ($dumpbin) {
        $deps = & $dumpbin /dependents $ExePath | Out-String
        if ($deps -match 'MSVCP140\.dll|VCRUNTIME140') {
            throw "Built OKeyTre.exe unexpectedly depends on dynamic VC++ runtime DLLs:`n$deps"
        }
        Write-Host "    Verified static CRT    : OK (no VC++ Redistributable required)"
    }

    # Verify embedded version resource matches target version.
    $fileVer = (Get-Item $ExePath).VersionInfo.ProductVersion
    if ($fileVer -ne $ResolvedVersion) {
        throw "Embedded ProductVersion '$fileVer' does not match expected '$ResolvedVersion'."
    }
    Write-Host "    Verified binary version: $fileVer"

    Write-Host "==> Staging release files..."
    if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
    Copy-Item -Force $ExePath $StagedExe
    Copy-Item -Force (Join-Path $RepoRoot "LICENSE") (Join-Path $StageDir "LICENSE.txt")

    if ($SignCertThumbprint -or $SignPfxPath) {
        $signtool = Find-WindowsSdkTool "signtool.exe"
        if (-not $signtool) {
            throw "signtool.exe not found in Windows SDK."
        }
        Write-Host "==> Signing OKeyTre.exe with Authenticode..."
        $signArgs = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256")
        if ($SignCertThumbprint) {
            $signArgs += @("/sha1", $SignCertThumbprint)
        } else {
            $signArgs += @("/f", $SignPfxPath)
            if ($SignPfxPassword) { $signArgs += @("/p", $SignPfxPassword) }
        }
        $signArgs += $StagedExe
        & $signtool @signArgs
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $signtool verify /pa /v $StagedExe | Out-Null
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $IsSigned = $true
    }

    Write-Host "==> Packaging portable archive: $ZipPath"
    if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
    Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal
}

$ZipInfo = Get-Item $ZipPath
$ZipSizeKB = [math]::Round($ZipInfo.Length / 1KB, 1)
$ZipSha256 = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToLowerInvariant()

Write-Host ""
Write-Host "Built Windows release archive successfully!"
Write-Host "  File   : $ZipPath ($ZipSizeKB KB)"
Write-Host "  Tag    : $Tag"
Write-Host "  Signed : $(if ($IsSigned) { 'yes (Authenticode)' } else { 'no (unsigned)' })"
Write-Host "  SHA256 : $ZipSha256"

if ($Publish) {
    Write-Host ""
    $dirty = git status --porcelain
    if ($dirty) {
        throw "Working tree has uncommitted changes. Commit and 'git push' first, then re-run with '-PublishOnly' (or re-run full script)."
    }

    $headSha = (git rev-parse HEAD).Trim()
    $remoteContains = git branch -r --contains $headSha | Where-Object { $_ -match 'origin/' }
    if (-not $remoteContains) {
        throw "Current commit ($headSha) has not been pushed to 'origin' yet. Run 'git push' first."
    }

    $assetName = Split-Path -Leaf $ZipPath
    $releaseNotes = @"
### Installation (Windows 10/11 x64)
1. Download and extract ``$assetName``.
2. Run **OKeyTre.exe** (if Windows SmartScreen appears on first launch, choose **More info -> Run anyway**).
3. Use **Left Ctrl + Right Ctrl** to toggle Vietnamese On/Off and **Left Shift + Right Shift** to restore raw typed words.
"@

    $gh = Find-Tool @("gh")
    if ($gh) {
        Write-Host "==> Publishing $assetName to GitHub Release $Tag ($GitHubRepo)..."
        & $gh release view $Tag --repo $GitHubRepo 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "    Release $Tag already exists - uploading/updating asset..."
            & $gh release upload $Tag $ZipPath --repo $GitHubRepo --clobber
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } else {
            $ghArgs = @("release", "create", $Tag, $ZipPath, "--repo", $GitHubRepo, "--target", $headSha, "--title", "$AppDisplayName $Tag", "--notes", $releaseNotes)
            if ($Prerelease) { $ghArgs += "--prerelease" } else { $ghArgs += "--latest" }
            & $gh @ghArgs
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        Write-Host "Published to GitHub Releases: https://github.com/$GitHubRepo/releases/tag/$Tag"
    } elseif ($env:GITHUB_TOKEN) {
        Write-Host "==> Publishing $assetName via GitHub REST API ($GitHubRepo)..."
        $headers = @{
            "Accept"               = "application/vnd.github+json"
            "Authorization"        = "Bearer $($env:GITHUB_TOKEN)"
            "X-GitHub-Api-Version" = "2022-11-28"
            "User-Agent"           = "OKeyTre-Release-Script"
        }

        $release = $null
        try {
            $release = Invoke-RestMethod -Method Get -Uri "https://api.github.com/repos/$GitHubRepo/releases/tags/$Tag" -Headers $headers
            Write-Host "    Release $Tag already exists - replacing existing asset if present..."
            foreach ($existingAsset in $release.assets) {
                if ($existingAsset.name -eq $assetName) {
                    Invoke-RestMethod -Method Delete -Uri "https://api.github.com/repos/$GitHubRepo/releases/assets/$($existingAsset.id)" -Headers $headers | Out-Null
                }
            }
        } catch {
            $payload = @{
                tag_name         = $Tag
                target_commitish = $headSha
                name             = "$AppDisplayName $Tag"
                body             = $releaseNotes
                draft            = $false
                prerelease       = [bool]$Prerelease
            } | ConvertTo-Json
            $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$GitHubRepo/releases" -Headers $headers -Body $payload -ContentType "application/json"
        }

        $uploadBase = ($release.upload_url -split '\{')[0]
        $uploadUri = "${uploadBase}?name=$([uri]::EscapeDataString($assetName))"
        Invoke-RestMethod -Method Post -Uri $uploadUri -Headers $headers -InFile $ZipPath -ContentType "application/zip" | Out-Null
        Write-Host "Published to GitHub Releases: $($release.html_url)"
    } else {
        throw "-Publish requires either GitHub CLI ('winget install GitHub.cli && gh auth login') or a GITHUB_TOKEN environment variable."
    }
}

if ($Open) {
    Start-Process explorer.exe -ArgumentList "/select,`"$ZipPath`""
}
