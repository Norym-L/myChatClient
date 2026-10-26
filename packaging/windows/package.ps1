#requires -Version 5.1
<#
    MyChat (qtClient) Windows packaging script
    ==========================================

    Produces, in <repo>\dist:
      * MyChat-<version>\              staged, self-contained app directory
      * MyChat-<version>-win64.zip     portable build (no installer needed)
      * MyChat-Setup-<version>.exe     installer (only when Inno Setup 6 is found)

    Pipeline:
      1. cmake --preset mingw-release (once) + build target qtClient
      2. stage qtClient.exe + config.ini into dist\MyChat-<version>
      3. windeployqt --dir <stage>     (Qt DLLs, QML tree, plugins)
      4. zip the stage
      5. ISCC.exe /DMyAppVersion=<version> packaging\windows\MyChat.iss

    Usage (from the repo root):
      powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1
      powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1 -SkipBuild
      powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1 -SkipBuild -SmokeTest

    Notes:
      * This file is intentionally ASCII-only: Windows PowerShell 5.1 reads
        BOM-less scripts as ANSI, and non-ASCII text would be mangled.
      * The installer is per-user by default (no admin, %LOCALAPPDATA%\Programs).
        That matters because the chat database lives in <app>\data, which a
        Program Files install could not write to.
#>
[CmdletBinding()]
param(
    # Overrides the version parsed from CMakeLists.txt (project(qtClient VERSION ...)).
    [string]$Version = "",

    # CMake preset / build directory. Keep them in sync.
    [string]$Preset = "mingw-release",
    [string]$BuildDir = "",

    # config.ini shipped next to the exe. Defaults to the repo template.
    [string]$ConfigFile = "",

    # Full path to ISCC.exe. Auto-detected when empty.
    [string]$IsccPath = "",

    [switch]$SkipBuild,
    [switch]$SkipZip,

    # Run the packaged exe once with the offscreen platform and MYCHAT_SMOKE.
    # Proves the staged directory is self-contained (no Qt in PATH needed).
    [switch]$SmokeTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Fail([string]$Text) {
    throw $Text
}

function Find-CommandPath([string]$Name, [string[]]$Candidates) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return $null
}

$packagingDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $packagingDir "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot "build\$Preset" }
if (-not $ConfigFile) { $ConfigFile = Join-Path $repoRoot "assets\config.ini" }

if (-not $Version) {
    $cmakeLists = Get-Content -Raw (Join-Path $repoRoot "CMakeLists.txt")
    if ($cmakeLists -match 'project\s*\(\s*qtClient\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    }
    else {
        Fail "Cannot parse project version from CMakeLists.txt; pass -Version explicitly."
    }
}

$stage = Join-Path $repoRoot "dist\MyChat-$Version"
$zip = Join-Path $repoRoot "dist\MyChat-$Version-win64.zip"

Write-Host "repo     : $repoRoot"
Write-Host "preset   : $Preset"
Write-Host "build    : $BuildDir"
Write-Host "version  : $Version"
Write-Host "stage    : $stage"

# -- 1. build ----------------------------------------------------------------
if (-not $SkipBuild) {
    $cmake = Find-CommandPath "cmake.exe" @(
        "C:\dev\Qt\Tools\CMake_64\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe"
    )
    if (-not $cmake) { Fail "cmake.exe not found; add CMake to PATH." }

    Write-Step "Configure (only when the build dir is empty)"
    if (-not (Test-Path (Join-Path $BuildDir "build.ninja"))) {
        & $cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed with exit code $LASTEXITCODE" }
    }

    Write-Step "Build qtClient ($Preset)"
    & $cmake --build $BuildDir --target qtClient
    if ($LASTEXITCODE -ne 0) { Fail "cmake build failed with exit code $LASTEXITCODE" }
}
else {
    Write-Step "Build skipped (-SkipBuild)"
}

$exe = Join-Path $BuildDir "qtClient.exe"
if (-not (Test-Path $exe)) { Fail "qtClient.exe not found: $exe" }
if (-not (Test-Path $ConfigFile)) { Fail "config.ini not found: $ConfigFile" }

# -- 2. stage ----------------------------------------------------------------
Write-Step "Stage app files"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item $exe $stage
Copy-Item $ConfigFile (Join-Path $stage "config.ini")

# MinGW/Qt writes an external manifest next to the exe (longPathAware, asInvoker).
$manifest = "$exe.manifest"
if (Test-Path $manifest) { Copy-Item $manifest $stage }

# -- 3. windeployqt ----------------------------------------------------------
Write-Step "Deploy Qt runtime (windeployqt)"

# Resolve this build's Qt from CMakeCache, so the script follows the preset
# instead of hardcoding C:\dev\Qt\...
$qtPrefix = $null
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $match = Select-String -Path $cacheFile -Pattern '^CMAKE_PREFIX_PATH:[^=]*=(.+)$' |
        Select-Object -First 1
    if ($match) { $qtPrefix = $match.Matches[0].Groups[1].Value.Trim() }
}

$windeployqt = $null
if ($qtPrefix) {
    $candidate = Join-Path $qtPrefix "bin\windeployqt.exe"
    if (Test-Path $candidate) { $windeployqt = $candidate }
}
if (-not $windeployqt) {
    $qtdirCandidates = @()
    if ($env:QTDIR) { $qtdirCandidates += (Join-Path $env:QTDIR "bin\windeployqt.exe") }
    $windeployqt = Find-CommandPath "windeployqt.exe" $qtdirCandidates
}
if (-not $windeployqt) { Fail "windeployqt.exe not found; check CMAKE_PREFIX_PATH in $cacheFile." }

Write-Host "windeployqt: $windeployqt"

# --qmldir must point at the source tree: this project uses a plain .qrc
# instead of qt_add_qml_module, so the tool cannot discover QML on its own.
# qoffscreen is added on purpose: ~40 KB, and it lets the packaged build run
# the headless smoke test (MYCHAT_SMOKE) on a machine without a desktop session.
& $windeployqt `
    --release `
    --no-translations `
    --no-opengl-sw `
    --compiler-runtime `
    --include-plugins qoffscreen `
    --qmldir $repoRoot `
    --dir $stage `
    (Join-Path $stage "qtClient.exe")

if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed with exit code $LASTEXITCODE" }

# Fallback: MinGW runtimes. The default build statically links libgcc/libstdc++
# but always needs libwinpthread-1.dll; windeployqt --compiler-runtime usually
# handles it, so only fill in what is still missing.
$mingwBin = $null
if (Test-Path $cacheFile) {
    $compilerMatch = Select-String -Path $cacheFile `
        -Pattern '^CMAKE_CXX_COMPILER:[^=]*=(.+)$' |
        Select-Object -First 1
    if ($compilerMatch) {
        $mingwBin = Split-Path -Parent $compilerMatch.Matches[0].Groups[1].Value.Trim()
    }
}
foreach ($dll in @("libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll")) {
    $target = Join-Path $stage $dll
    if (Test-Path $target) { continue }
    foreach ($candidateDir in @($BuildDir, $mingwBin)) {
        if (-not $candidateDir) { continue }
        $candidate = Join-Path $candidateDir $dll
        if (Test-Path $candidate) {
            Copy-Item $candidate $stage
            Write-Host "  + $dll (from $candidateDir)"
            break
        }
    }
}

# -- 4. self-contained smoke test (optional) ---------------------------------
if ($SmokeTest) {
    Write-Step "Smoke test the staged build"
    $smoke = Join-Path $stage "smoke.png"
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR "Fonts"
    $env:QT_ASSUME_STDERR_HAS_CONSOLE = "1"
    $env:MYCHAT_PAGE = "Login"
    $env:MYCHAT_SMOKE = $smoke

    # qtClient is a GUI-subsystem exe: `& exe` in PowerShell returns immediately
    # without waiting, so Start-Process -Wait is required to see the exit code.
    $proc = Start-Process -FilePath (Join-Path $stage "qtClient.exe") `
        -WorkingDirectory $stage -PassThru -Wait
    if ($proc.ExitCode -ne 0 -or -not (Test-Path $smoke)) {
        Fail "smoke test failed (exit $($proc.ExitCode)); check deployed plugins / QML."
    }
    Remove-Item $smoke -Force
    Write-Host "  OK: offscreen render succeeded"
}

# -- 5. portable zip ---------------------------------------------------------
if (-not $SkipZip) {
    Write-Step "Create portable zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }

    # bsdtar (shipped with Windows 10+) is much faster than Compress-Archive.
    $tar = Join-Path $env:SystemRoot "System32\tar.exe"
    if (Test-Path $tar) {
        & $tar -a -c -f $zip -C $stage .
        if ($LASTEXITCODE -ne 0) { Fail "tar failed with exit code $LASTEXITCODE" }
    }
    else {
        Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
    }
    Write-Host "  $zip"
}

# -- 6. installer ------------------------------------------------------------
Write-Step "Build installer (Inno Setup)"

if (-not $IsccPath) {
    $IsccPath = Find-CommandPath "ISCC.exe" @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
}

if (-not $IsccPath) {
    Write-Host "  SKIPPED: ISCC.exe not found." -ForegroundColor Yellow
    Write-Host "  Install Inno Setup 6 first:  winget install JRSoftware.InnoSetup" -ForegroundColor Yellow
    Write-Host "  Then re-run:  packaging\windows\package.ps1 -SkipBuild" -ForegroundColor Yellow
}
else {
    Write-Host "ISCC: $IsccPath"
    & $IsccPath "/DMyAppVersion=$Version" (Join-Path $packagingDir "MyChat.iss")
    if ($LASTEXITCODE -ne 0) { Fail "ISCC failed with exit code $LASTEXITCODE" }
}

# -- summary -----------------------------------------------------------------
Write-Step "Done"
if (Test-Path $zip) {
    $sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
    Write-Host ("  portable : {0} ({1} MB)" -f $zip, $sizeMb)
}
Write-Host "  staged   : $stage"
$setup = Join-Path $repoRoot "dist\MyChat-Setup-$Version.exe"
if (Test-Path $setup) {
    $sizeMb = [math]::Round((Get-Item $setup).Length / 1MB, 1)
    Write-Host ("  installer: {0} ({1} MB)" -f $setup, $sizeMb)
}
