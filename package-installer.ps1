<#
.SYNOPSIS
    Build the Windows installer (dist\RPA-Block-Setup.exe).

.DESCRIPTION
    Stages the same runtime files package.ps1 puts inside the single-file exe,
    then hands them to NSIS. The two packages ship identical contents; they
    differ only in how they land on the machine.

    Use this one for customer installs: it puts the files in Program Files,
    creates shortcuts and an uninstaller, and starts without unpacking. The
    single-file exe is for handing someone a build to try with nothing to
    install.

    Run build-full.cmd first — this script only packages what is already built.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File package-installer.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputFile,
    [string]$Version = '0.1.0',
    [string]$MakeNsis
)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $scriptDir) { $scriptDir = (Get-Location).Path }

if (-not $BuildDir) { $BuildDir = Join-Path $scriptDir 'build-full' }
if (-not $OutputFile) { $OutputFile = Join-Path $scriptDir 'dist\RPA-Block-Setup.exe' }

if (-not $MakeNsis) {
    $candidates = @(
        'C:\Program Files (x86)\NSIS\makensis.exe',
        'C:\Program Files\NSIS\makensis.exe'
    )
    $found = Get-Command makensis -ErrorAction SilentlyContinue
    if ($found) { $MakeNsis = $found.Source }
    else { $MakeNsis = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1 }
}
if (-not $MakeNsis -or -not (Test-Path $MakeNsis)) {
    throw "makensis.exe not found. Install NSIS (winget install NSIS.NSIS) or pass -MakeNsis."
}

$bin = Join-Path $BuildDir 'bin'
$app = Join-Path $bin 'rpa-studio.exe'
foreach ($required in @($bin, $app)) {
    if (-not (Test-Path $required)) {
        throw "Missing build output: $required`nRun build-full.cmd first."
    }
}

# --- Stage the runtime files ------------------------------------------------
# Same exclusions as package.ps1: debug symbols, linker leftovers, and the two
# developer tools that have no business on a user's machine.
$stage = Join-Path $env:TEMP ("rpablock-installer-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$excludeNames = @('rpa-pack-stub.exe', 'rpa-tests.exe', 'rpa-ai-probe.exe', 'vc_redist.x64.exe')
$excludeExt = @('.pdb', '.ilk', '.exp', '.lib')

Write-Host "Staging runtime files..."
Get-ChildItem $bin -Recurse -File | ForEach-Object {
    if ($excludeNames -contains $_.Name) { return }
    if ($excludeExt -contains $_.Extension.ToLower()) { return }

    $relative = $_.FullName.Substring($bin.Length).TrimStart('\')
    $destination = Join-Path $stage $relative
    $parent = Split-Path $destination -Parent
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Copy-Item $_.FullName $destination
}

# Without these the app dies at startup on any machine that has never had the
# Visual C++ tools installed -- which is every customer machine.
$crt = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll')
$missingCrt = $crt | Where-Object { -not (Test-Path (Join-Path $stage $_)) }
if ($missingCrt) {
    throw ("The MSVC runtime is missing from the build output: {0}`n" -f ($missingCrt -join ', ')) +
          "Rebuild with build-full.cmd; rpa-studio/CMakeLists.txt copies these next to the exe."
}

$examples = Join-Path $scriptDir 'examples'
if (Test-Path $examples) {
    $target = Join-Path $stage 'examples'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item "$examples\*.rpa.json" $target
}

# All three model files or none: the app treats a partial folder as absent.
$models = Join-Path $scriptDir 'models'
$modelFiles = @('det.onnx', 'rec.onnx', 'keys.txt')
$haveModels = (Test-Path $models) -and
              -not ($modelFiles | Where-Object { -not (Test-Path (Join-Path $models $_)) })
if ($haveModels) {
    $target = Join-Path $stage 'models'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    foreach ($name in $modelFiles) { Copy-Item (Join-Path $models $name) $target -Force }
    $notice = Join-Path $models 'NOTICE.txt'
    if (Test-Path $notice) { Copy-Item $notice $target -Force }
    Write-Host "  including bundled OCR models"
} else {
    Write-Host "  no OCR models to bundle (run fetch-deps.ps1); 'find text' will need a folder set in Settings"
}

$staged = Get-ChildItem $stage -Recurse -File
Write-Host ("  {0} files, {1:N1} MB" -f $staged.Count, (($staged | Measure-Object -Property Length -Sum).Sum / 1MB))

# --- Compile ----------------------------------------------------------------
$outDir = Split-Path $OutputFile -Parent
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

Write-Host "Compiling the installer..."
$nsi = Join-Path $scriptDir 'installer.nsi'
& $MakeNsis "/DSTAGE_DIR=$stage" "/DOUT_FILE=$OutputFile" "/DVERSION=$Version" $nsi | ForEach-Object {
    if ($_ -match '^(Error|warning)') { Write-Host $_ }
}
$code = $LASTEXITCODE

Remove-Item $stage -Recurse -Force

if ($code -ne 0) { throw "makensis failed with exit code $code" }

$final = Get-Item $OutputFile
Write-Host ""
Write-Host ("Done: {0}  ({1:N1} MB)" -f $final.FullName, ($final.Length / 1MB))
Write-Host "Installs to Program Files, with Start Menu and optional desktop shortcuts."
