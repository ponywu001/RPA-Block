<#
.SYNOPSIS
    Fetch the prebuilt Qt 6, OpenCV, and ONNX Runtime that the full build needs.

.DESCRIPTION
    Downloads official prebuilt binaries only — nothing is compiled from source,
    so this takes minutes rather than the hours a vcpkg Qt build would.

    Qt comes via aqtinstall, which pulls the same official binaries as the Qt
    online installer but needs no account. Only qtbase (Core/Gui/Widgets/Network)
    and qttools (for windeployqt) are fetched, not the whole ~50 GB of Qt.

    Safe to re-run: anything already present is skipped.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File fetch-deps.ps1
#>
[CmdletBinding()]
param(
    [string]$QtVersion = '6.8.3',
    [string]$QtRoot = 'C:\Qt',
    [string]$DepsRoot = 'C:\deps',
    [string]$OpenCvVersion = '4.10.0',
    [string]$OrtVersion = '1.20.1'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # the progress bar makes downloads far slower

function Resolve-Python {
    foreach ($candidate in @('python', 'py', 'C:\Python313\python.exe')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    throw "Python 3 was not found on PATH. It is needed to run aqtinstall for the Qt download."
}

New-Item -ItemType Directory -Force -Path $DepsRoot | Out-Null

# --- Qt ---------------------------------------------------------------------
$qtPrefix = Join-Path $QtRoot "$QtVersion\msvc2022_64"
if (Test-Path (Join-Path $qtPrefix 'lib\cmake\Qt6\Qt6Config.cmake')) {
    Write-Host "Qt $QtVersion already present at $qtPrefix"
} else {
    $python = Resolve-Python
    Write-Host "Installing aqtinstall..."
    & $python -m pip install --quiet --user aqtinstall
    if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed." }

    Write-Host "Downloading Qt $QtVersion (qtbase + qttools, msvc2022_64)..."
    & $python -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 `
        --outputdir $QtRoot --archives qtbase qttools
    if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed." }

    if (-not (Test-Path (Join-Path $qtPrefix 'bin\windeployqt.exe'))) {
        throw "Qt installed but windeployqt.exe is missing; the qttools archive did not arrive."
    }
    Write-Host "  Qt -> $qtPrefix"
}

# --- ONNX Runtime -----------------------------------------------------------
$ortDir = Join-Path $DepsRoot "ort\onnxruntime-win-x64-$OrtVersion"
if (Test-Path (Join-Path $ortDir 'lib\onnxruntime.lib')) {
    Write-Host "ONNX Runtime $OrtVersion already present"
} else {
    $zip = Join-Path $DepsRoot 'onnxruntime.zip'
    $url = "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/onnxruntime-win-x64-$OrtVersion.zip"
    Write-Host "Downloading ONNX Runtime $OrtVersion..."
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 900
    Expand-Archive -Path $zip -DestinationPath (Join-Path $DepsRoot 'ort') -Force
    Remove-Item $zip -Force
    if (-not (Test-Path (Join-Path $ortDir 'lib\onnxruntime.lib'))) {
        throw "ONNX Runtime extracted to an unexpected layout under $DepsRoot\ort."
    }
    Write-Host "  ONNX Runtime -> $ortDir"
}

# --- OpenCV -----------------------------------------------------------------
$cvDir = Join-Path $DepsRoot 'opencv\build'
if (Test-Path (Join-Path $cvDir 'OpenCVConfig.cmake')) {
    Write-Host "OpenCV already present"
} else {
    $installer = Join-Path $DepsRoot 'opencv.exe'
    $url = "https://github.com/opencv/opencv/releases/download/$OpenCvVersion/opencv-$OpenCvVersion-windows.exe"
    Write-Host "Downloading OpenCV $OpenCvVersion (~175 MB)..."
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing -TimeoutSec 1800

    # The release asset is a self-extracting 7z, not an installer; -o sets the
    # destination and -y suppresses the prompt.
    Write-Host "Extracting OpenCV..."
    Start-Process -FilePath $installer -ArgumentList "-o`"$DepsRoot`"", '-y' -Wait -NoNewWindow
    Remove-Item $installer -Force
    if (-not (Test-Path (Join-Path $cvDir 'OpenCVConfig.cmake'))) {
        throw "OpenCV extracted but OpenCVConfig.cmake is missing under $cvDir."
    }
    Write-Host "  OpenCV -> $cvDir"
}

# --- OCR models -------------------------------------------------------------
# Into the source tree rather than $DepsRoot: these ship inside the exe, so they
# are part of what gets packaged, not a build-time dependency.
$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$modelDir = Join-Path $scriptDir 'models'

# PP-OCRv5, whose unified charset covers Traditional Chinese. The older
# PP-OCRv4 Chinese model is smaller but its dictionary omits characters this
# application's own UI uses -- see models\NOTICE.txt.
$modelBase = 'https://huggingface.co/nathanfhh/PaddleOCR-ONNX/resolve/main'
$models = @(
    @{ name = 'det.onnx'; url = "$modelBase/PP-OCRv5_mobile_det.onnx"; size = 'about 4.6 MB' }
    @{ name = 'rec.onnx'; url = "$modelBase/PP-OCRv5_mobile_rec.onnx"; size = 'about 15.8 MB' }
    @{ name = 'keys.txt'; url = "$modelBase/ppocrv5_dict.txt";         size = 'about 70 KB' }
)

if (-not (Test-Path $modelDir)) { New-Item -ItemType Directory -Force -Path $modelDir | Out-Null }
foreach ($model in $models) {
    $target = Join-Path $modelDir $model.name
    if ((Test-Path $target) -and (Get-Item $target).Length -gt 0) {
        Write-Host "OCR model $($model.name) already present"
        continue
    }
    Write-Host "Downloading OCR model $($model.name) ($($model.size))..."
    Invoke-WebRequest -Uri $model.url -OutFile $target -UseBasicParsing -TimeoutSec 600
}
Write-Host "  OCR models -> $modelDir"

Write-Host ""
Write-Host "All dependencies ready:"
Write-Host "  Qt            $qtPrefix"
Write-Host "  OpenCV        $cvDir"
Write-Host "  ONNX Runtime  $ortDir"
Write-Host "  OCR models    $modelDir"
Write-Host ""
Write-Host "Next:  build-full.cmd"
