@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Usage:
rem   build-release.bat              -> x86
rem   build-release.bat x86
rem   build-release.bat x64
rem   build-release.bat all
rem   build-release.bat x86 smoke
rem   build-release.bat all smoke

set "ARCH_ARG=%~1"
set "SMOKE_ARG=%~2"
set "SMOKE_BUILD=false"

if /I "%ARCH_ARG%"=="smoke" (
  set "ARCH_ARG=x86"
  set "SMOKE_ARG=smoke"
)
if "%ARCH_ARG%"=="" set "ARCH_ARG=x86"
if /I "%SMOKE_ARG%"=="smoke" set "SMOKE_BUILD=true"

if not defined WTLIncludeDir set "WTLIncludeDir=C:\Tools\WTL10\Include"
if not exist "%WTLIncludeDir%\atlapp.h" (
  echo WTL not found: %WTLIncludeDir%\atlapp.h
  echo Set WTLIncludeDir to your WTL Include folder, e.g. C:\Tools\WTL10\Include
  exit /b 1
)

rem Avoid "(x86)" inside if () blocks — use a short var.
set "PF86=%ProgramFiles(x86)%"
set "VCVARS_DIR=%PF86%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build"
if not exist "!VCVARS_DIR!\vcvars32.bat" (
  echo vcvars not found under: !VCVARS_DIR!
  echo Install VS Build Tools with C++ workload
  exit /b 1
)

if /I "%ARCH_ARG%"=="all" (
  call "%ComSpec%" /c ""%~f0" x86 %SMOKE_ARG%"
  if errorlevel 1 exit /b 1
  call "%ComSpec%" /c ""%~f0" x64 %SMOKE_ARG%"
  exit /b %ERRORLEVEL%
)

if /I not "%ARCH_ARG%"=="x86" if /I not "%ARCH_ARG%"=="x64" (
  echo Unknown arch: %ARCH_ARG%
  echo Use: build-release.bat [x86^|x64^|all] [smoke]
  exit /b 1
)

if /I "%ARCH_ARG%"=="x86" (
  set "MSBUILD_PLATFORM=x86"
  set "OUT_PLATFORM=Win32"
  set "VCVARS=!VCVARS_DIR!\vcvars32.bat"
  set "PKG_SUFFIX=win32"
) else (
  set "MSBUILD_PLATFORM=x64"
  set "OUT_PLATFORM=x64"
  set "VCVARS=!VCVARS_DIR!\vcvars64.bat"
  set "PKG_SUFFIX=win64"
)

if /I "%SMOKE_BUILD%"=="true" (
  set "PKG_NAME=foo_lyrics_ai_translator_!PKG_SUFFIX!_smoke.fb2k-component"
) else (
  set "PKG_NAME=foo_lyrics_ai_translator_!PKG_SUFFIX!.fb2k-component"
)

echo.
echo === Building %ARCH_ARG% (!OUT_PLATFORM!) ===
call "!VCVARS!"
if errorlevel 1 (
  echo Failed: !VCVARS!
  exit /b 1
)

set "INCLUDE=%WTLIncludeDir%;%INCLUDE%"

msbuild "%~dp0lyrics-plugin.sln" /t:Rebuild /p:Configuration=Release /p:Platform=!MSBUILD_PLATFORM! /p:PlatformToolset=v145 /p:WTLIncludeDir="%WTLIncludeDir%" /p:LyricsSmokeBuild=%SMOKE_BUILD% /m /v:minimal
if errorlevel 1 exit /b 1

set "DLL=%~dp0out\!OUT_PLATFORM!\foo_lyrics_ai_translator.dll"
set "DIST=%~dp0dist\%ARCH_ARG%\lyrics-ai-translator"
if not exist "!DLL!" (
  echo Build succeeded but DLL not found: !DLL!
  exit /b 1
)

if not exist "!DIST!" mkdir "!DIST!"
copy /Y "!DLL!" "!DIST!\" >nul
echo Copied DLL to !DIST!

if /I not "%SMOKE_BUILD%"=="true" (
  echo Packaging Go binaries...
  set "PS_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
  if not exist "!PS_EXE!" set "PS_EXE=pwsh"
  "!PS_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-release.ps1" -Arch %ARCH_ARG%
  if errorlevel 1 exit /b 1
)

set "PKG=%~dp0dist\!PKG_NAME!"
if exist "!PKG!" del /f /q "!PKG!"
if exist "!PKG!.zip" del /f /q "!PKG!.zip"
where tar >nul 2>&1
if errorlevel 1 (
  echo Note: install tar to auto-create .fb2k-component; package is in !DIST!
  exit /b 0
)

pushd "!DIST!"
tar -a -c -f "!PKG!.zip" *
popd
if exist "!PKG!.zip" ren "!PKG!.zip" "!PKG_NAME!"
if exist "!PKG!" echo Created !PKG!
exit /b 0
