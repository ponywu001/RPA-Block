<#
.SYNOPSIS
    Assemble the deployed build output into a single self-extracting exe.

.DESCRIPTION
    Collects the runtime files from the build's bin directory (stripping debug
    artefacts), zips them, and appends the archive to the launcher stub with a
    footer the stub reads back at run time.

    Run build-full.cmd first — this script only packages what is already built.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File package.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputFile
)

$ErrorActionPreference = 'Stop'

# Resolved here rather than in the param defaults: $PSScriptRoot is not reliably
# populated while default values are being evaluated.
$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $scriptDir) { $scriptDir = (Get-Location).Path }

if (-not $BuildDir) { $BuildDir = Join-Path $scriptDir 'build-full' }
if (-not $OutputFile) { $OutputFile = Join-Path $scriptDir 'dist\RPA-Block.exe' }

$bin = Join-Path $BuildDir 'bin'
$stub = Join-Path $bin 'rpa-pack-stub.exe'
$app = Join-Path $bin 'rpa-studio.exe'

foreach ($required in @($bin, $stub, $app)) {
    if (-not (Test-Path $required)) {
        throw "Missing build output: $required`nRun build-full.cmd first."
    }
}

# --- Stage the runtime files ------------------------------------------------
# Debug symbols and the intermediate linker files are large and useless to a
# user, and the stub itself must not be inside its own payload.
$stage = Join-Path $env:TEMP ("rpablock-stage-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$excludeNames = @('rpa-pack-stub.exe', 'rpa-tests.exe', 'vc_redist.x64.exe')
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

# Ship the example flows so a fresh install has something to open.
$examples = Join-Path $scriptDir 'examples'
if (Test-Path $examples) {
    $target = Join-Path $stage 'examples'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item "$examples\*.rpa.json" $target
}

# Ship the OCR models so "find text" works with nothing to download and nothing
# to configure. All three files or none: the app treats a partial folder as
# absent, and shipping half of one would only produce a confusing load error.
$models = Join-Path $scriptDir 'models'
$modelFiles = @('det.onnx', 'rec.onnx', 'keys.txt')
$haveModels = (Test-Path $models) -and
              -not ($modelFiles | Where-Object { -not (Test-Path (Join-Path $models $_)) })
if ($haveModels) {
    $target = Join-Path $stage 'models'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    foreach ($name in $modelFiles) { Copy-Item (Join-Path $models $name) $target }
    $notice = Join-Path $models 'NOTICE.txt'
    if (Test-Path $notice) { Copy-Item $notice $target }
    Write-Host "  including bundled OCR models"
} else {
    Write-Host "  no OCR models to bundle (run fetch-deps.ps1); 'find text' will need a folder set in Settings"
}

$staged = Get-ChildItem $stage -Recurse -File
Write-Host ("  {0} files, {1:N1} MB" -f $staged.Count, (($staged | Measure-Object -Property Length -Sum).Sum / 1MB))

# --- Zip it -----------------------------------------------------------------
$zip = Join-Path $env:TEMP ("rpablock-payload-" + [guid]::NewGuid().ToString('N') + ".zip")
Write-Host "Compressing..."
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
$zipSize = (Get-Item $zip).Length
Write-Host ("  payload {0:N1} MB" -f ($zipSize / 1MB))

# --- Concatenate: stub + payload + footer -----------------------------------
# Footer is 8 bytes of little-endian payload length followed by the 8-byte magic
# the stub looks for. The magic keeps its original spelling: it identifies the
# container format, not the product, and changing it would only make an older
# dist exe fail in a way that looks like corruption.
$outDir = Split-Path $OutputFile -Parent
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

Write-Host "Writing $OutputFile ..."
$output = [System.IO.File]::Open($OutputFile, 'Create', 'Write')
try {
    foreach ($part in @($stub, $zip)) {
        $input = [System.IO.File]::OpenRead($part)
        try { $input.CopyTo($output) } finally { $input.Dispose() }
    }
    $output.Write([System.BitConverter]::GetBytes([uint64]$zipSize), 0, 8)
    $output.Write([System.Text.Encoding]::ASCII.GetBytes('PRAPACK1'), 0, 8)
} finally {
    $output.Dispose()
}

Remove-Item $zip -Force
Remove-Item $stage -Recurse -Force

$final = Get-Item $OutputFile
Write-Host ""
Write-Host ("Done: {0}  ({1:N1} MB)" -f $final.FullName, ($final.Length / 1MB))
Write-Host "Double-click it. The first run unpacks to %LOCALAPPDATA%\RPA-Block\<key>\; later runs start straight away."
