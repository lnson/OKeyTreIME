# Copyright (C) 2026 lnson
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds, tests, and packages the native Windows OKeyTre desktop application
# (`build\OKeyTre-v<version>-windows-x64.zip`).
#
# Prerequisites on Windows:
#   - Visual Studio 2022 (or Build Tools) with "Desktop development with C++" + Windows 10/11 SDK
#   - Bazel or Bazelisk (`winget install Bazel.Bazelisk`)
#   - Flex & Bison (`winget install --id=lexxmark.winflexbison` or MSYS2 flex/bison on PATH)
#
# Usage:
#   .\scripts\build-windows.ps1 [-Release] [-SkipTests] [-Package] [-Run] [-FlexBisonDir <path>]

[CmdletBinding()]
param(
    [switch]$Release,
    [switch]$SkipTests,
    [switch]$Package,
    [switch]$Run,
    [string]$FlexBisonDir
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
Set-Location $RepoRoot

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

$bazel = Find-Tool @("bazel", "bazelisk")
if (-not $bazel) {
    throw "Bazel / Bazelisk was not found on PATH. Install via: winget install Bazel.Bazelisk"
}

$flex = Find-Tool @("win_flex", "flex")
$bison = Find-Tool @("win_bison", "bison")
if (-not $flex -or -not $bison) {
    throw "Flex and Bison (win_flex/win_bison or flex/bison) must be on PATH, or pass -FlexBisonDir <path>."
}

Write-Host "==> Using Bazel : $bazel"
Write-Host "==> Using Flex  : $flex"
Write-Host "==> Using Bison : $bison"

if (-not $SkipTests) {
    Write-Host "==> Running C++ engine, compiler, and Windows logic unit tests..."
    & $bazel test //OKeyTre/ime/core/... //imec/... //invc/... //windows/...
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$buildArgs = @("build")
if ($Release) {
    $buildArgs += "--config=release"
    $buildArgs += "--features=static_link_msvcrt"
}
$buildArgs += "//:OKeyTreWindows"

Write-Host "==> Building Windows application ($($buildArgs -join ' '))..."
& $bazel @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exePath = Join-Path $RepoRoot "bazel-bin\windows\OKeyTre.exe"
if (-not (Test-Path $exePath)) {
    throw "Expected output binary was not found at: $exePath"
}

Write-Host "==> Built: $exePath"

if ($Package) {
    $versionHeader = Get-Content (Join-Path $RepoRoot "windows\resources\resource.h") -Raw
    $version = "0.3.1"
    if ($versionHeader -match 'OKT_VERSION_STRING\s+"([^"]+)"') {
        $version = $Matches[1]
    }

    $stageDir = Join-Path $RepoRoot "build\windows-release"
    $zipPath = Join-Path $RepoRoot "build\OKeyTre-v$version-windows-x64.zip"

    if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
    Copy-Item -Force $exePath (Join-Path $stageDir "OKeyTre.exe")
    Copy-Item -Force (Join-Path $RepoRoot "LICENSE") (Join-Path $stageDir "LICENSE.txt")

    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path "$stageDir\*" -DestinationPath $zipPath -CompressionLevel Optimal

    $hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
    Write-Host ""
    Write-Host "Packaged Windows release archive:"
    Write-Host "  File   : $zipPath"
    Write-Host "  SHA256 : $hash"
}

if ($Run) {
    Write-Host "==> Launching OKeyTre.exe..."
    Start-Process -FilePath $exePath
}
