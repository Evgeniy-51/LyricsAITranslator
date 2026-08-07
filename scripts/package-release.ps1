# Packages Lyrics AI Translator for foobar2000 user-components install.
# Requires: Go. DLL must be built first (build-release.bat).
#
# Usage:
#   .\scripts\package-release.ps1              # x86
#   .\scripts\package-release.ps1 -Arch x64
#   .\scripts\package-release.ps1 -Arch all

param(
    [ValidateSet("x86", "x64", "all")]
    [string]$Arch = "x86"
)

$ErrorActionPreference = "Stop"
$app = Split-Path $PSScriptRoot -Parent

function Package-Arch([string]$arch) {
    $goArch = if ($arch -eq "x64") { "amd64" } else { "386" }
    $outPlatform = if ($arch -eq "x64") { "x64" } else { "Win32" }
    $dist = Join-Path $app "dist\$arch\lyrics-ai-translator"
    $dllSrc = Join-Path $app "out\$outPlatform\foo_lyrics_ai_translator.dll"

    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    Write-Host "Building lyrics_worker.exe (windows/$goArch)..."
    Push-Location (Join-Path $app "worker")
    $env:GOOS = "windows"
    $env:GOARCH = $goArch
    $env:CGO_ENABLED = "0"
    go build -ldflags="-s -w" -o (Join-Path $dist "lyrics_worker.exe") .\cmd\lyrics_worker
    Pop-Location

    Write-Host "Building lyrics_server.exe (windows/$goArch)..."
    Push-Location (Join-Path $app "lyrics_server")
    $env:GOOS = "windows"
    $env:GOARCH = $goArch
    $env:CGO_ENABLED = "0"
    go build -ldflags="-s -w" -o (Join-Path $dist "lyrics_server.exe") .\cmd\lyrics_server
    Pop-Location

    Copy-Item (Join-Path $app "config.example.json") (Join-Path $dist "config.example.json") -Force

    $installPath = Join-Path $dist "INSTALL.txt"
    $bitness = if ($arch -eq "x64") { "64-bit" } else { "32-bit" }
    @"
Lyrics AI Translator - install package ($bitness foobar2000)

Copy this entire folder to:
  %APPDATA%\foobar2000-v2\user-components\lyrics-ai-translator\
(or your foobar2000 "user-components" directory)

This package is for $bitness foobar2000 only. Do not mix x86/x64 binaries.

Required files in that folder:
  foo_lyrics_ai_translator.dll
  lyrics_worker.exe
  lyrics_server.exe
  config.json  (copy from config.example.json, or configure via Preferences)

See docs/USER.md in the source repository for setup.
"@ | Set-Content $installPath -Encoding UTF8

    if (Test-Path $dllSrc) {
        Copy-Item $dllSrc (Join-Path $dist "foo_lyrics_ai_translator.dll") -Force
        Write-Host "Copied foo_lyrics_ai_translator.dll from out\$outPlatform"
    } else {
        Write-Warning "DLL not found: $dllSrc - run build-release.bat $arch first"
    }

    Write-Host ""
    Write-Host "Package ready ($arch): $dist"
    Get-ChildItem $dist | Format-Table Name, Length, LastWriteTime
}

if ($Arch -eq "all") {
    Package-Arch "x86"
    Package-Arch "x64"
} else {
    Package-Arch $Arch
}
