# Copyright (C) 2026 lnson
# SPDX-License-Identifier: GPL-3.0-only
#
# Prepares a Microsoft Security Intelligence (WDSI) Software Developer
# submission for the current Windows release binary (`OKeyTre.exe`) to clear
# Microsoft Defender SmartScreen warnings on that exact file hash.
#
# Usage:
#   .\scripts\submit-smartscreen.ps1 [-ExePath <path>] [-Version <ver>] [-NoOpen]

[CmdletBinding()]
param(
    [string]$ExePath,
    [string]$Version,
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$GitHubRepo = "lnson/OKeyTreIME"
$WdsiUrl = "https://www.microsoft.com/en-us/wdsi/filesubmission"

function Get-CurrentWindowsVersion {
    $headerPath = Join-Path $RepoRoot "windows\resources\resource.h"
    if (Test-Path $headerPath) {
        $raw = Get-Content $headerPath -Raw
        if ($raw -match 'OKT_VERSION_STRING\s+"([^"]+)"') {
            return $Matches[1]
        }
    }
    return "0.3.3"
}

$ResolvedVersion = if ($Version) { $Version } else { Get-CurrentWindowsVersion }
$Tag = "v$ResolvedVersion"
$ZipPath = Join-Path $RepoRoot "build\OKeyTre-$Tag-windows-x64.zip"
$StagedExe = Join-Path $RepoRoot "build\windows-release\OKeyTre.exe"
$BazelExe = Join-Path $RepoRoot "bazel-bin\windows\OKeyTre.exe"

if (-not $ExePath) {
    if (Test-Path $StagedExe) {
        $ExePath = $StagedExe
    } elseif (Test-Path $ZipPath) {
        $extractDir = Join-Path $RepoRoot "build\windows-release"
        if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
        Expand-Archive -Path $ZipPath -DestinationPath $extractDir -Force
        if (Test-Path $StagedExe) {
            $ExePath = $StagedExe
        }
    } elseif (Test-Path $BazelExe) {
        $ExePath = $BazelExe
    }
}

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw "No built OKeyTre.exe found. Run '.\scripts\release-windows.ps1' or '.\scripts\build-windows.ps1 -Release' first."
}

$ExeItem = Get-Item $ExePath
$FileVer = $ExeItem.VersionInfo.ProductVersion
if ($FileVer) {
    $ResolvedVersion = $FileVer.Trim()
    $Tag = "v$ResolvedVersion"
}
$ExeSha256 = (Get-FileHash -Algorithm SHA256 $ExeItem.FullName).Hash.ToLowerInvariant()
$ExeSizeKB = [math]::Round($ExeItem.Length / 1KB, 1)

$ZipLine = ""
if (Test-Path $ZipPath) {
    $ZipSha256 = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToLowerInvariant()
    $ZipLine = "Release archive SHA-256 ($(Split-Path -Leaf $ZipPath)): $ZipSha256`r`n"
}

$ReleaseUrl = "https://github.com/$GitHubRepo/releases/tag/$Tag"
$RepoUrl = "https://github.com/$GitHubRepo"

$SubmissionText = @"
Application: OKeyTre (Vietnamese Input Method / IME) v$ResolvedVersion
License: Open-source (GPL-3.0)
Source repository: $RepoUrl
GitHub Release: $ReleaseUrl
Binary: OKeyTre.exe ($ExeSizeKB KB)
OKeyTre.exe SHA-256: $ExeSha256
${ZipLine}
Summary:
OKeyTre is a free, open-source Vietnamese typing input method (Telex / VNI / VIQR) built in native C++23 / Win32. It installs a standard user-mode WH_KEYBOARD_LL keyboard hook, uses SendInput to replace Vietnamese diacritic keystrokes, and uses Windows UI Automation (IUIAutomation) to read the word around the text caret for cursor-aware editing. It performs no network telemetry (only an optional GitHub Releases check for updates) and requires no elevation.

Request:
Please review OKeyTre.exe v$ResolvedVersion as a legitimate open-source Vietnamese IME and clear Microsoft Defender SmartScreen ("unrecognized app") warnings for this binary.
"@

$BuildDir = Join-Path $RepoRoot "build"
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null }
$NotePath = Join-Path $BuildDir "microsoft-submission-$Tag.txt"
Set-Content -Path $NotePath -Value $SubmissionText -Encoding UTF8

try {
    Set-Clipboard -Value $SubmissionText
    $ClipboardCopied = $true
} catch {
    $ClipboardCopied = $false
}

Write-Host ""
Write-Host "================================================================="
Write-Host " Microsoft Security Intelligence (SmartScreen) Submission Ready"
Write-Host "================================================================="
Write-Host "  Binary  : $($ExeItem.FullName) ($ExeSizeKB KB)"
Write-Host "  Version : $ResolvedVersion ($Tag)"
Write-Host "  SHA-256 : $ExeSha256"
Write-Host "  Notes   : $NotePath"
if ($ClipboardCopied) {
    Write-Host "  Status  : Submission description copied to clipboard (Ctrl+V)."
}
Write-Host ""
Write-Host "Steps in the browser:"
Write-Host "  1. Sign in with your Microsoft account and choose 'Software developer'."
Write-Host "  2. Select product: 'Microsoft Defender SmartScreen'."
Write-Host "  3. Drag & drop OKeyTre.exe from the opened Explorer window."
Write-Host "  4. Choose 'Incorrectly detected as malware/PUA' (or unrecognized app)."
Write-Host "  5. Paste the pre-filled description (Ctrl+V) and submit."
Write-Host ""

if (-not $NoOpen) {
    Start-Process $WdsiUrl
    Start-Process explorer.exe -ArgumentList "/select,`"$($ExeItem.FullName)`""
}
