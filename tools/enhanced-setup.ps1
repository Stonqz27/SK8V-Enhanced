# Guided entry point for the asset-free Enhanced release.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Python = Join-Path $Root 'python/python.exe'
if (-not (Test-Path -LiteralPath $Python)) {
    $Existing = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($Existing) {
        $ErrorActionPreference = 'Continue'
        & $Existing.Source -c 'import sys, numpy, PIL; assert sys.version_info >= (3, 11)' 2>$null
        $Code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($Code -eq 0) { $Python = $Existing.Source }
    }
}
if (-not (Test-Path -LiteralPath $Python)) {
    Write-Host 'Setup needs Python with NumPy and Pillow. It can download verified portable copies into this folder.'
    if ((Read-Host 'Download them now? Type YES') -ne 'YES') { throw 'Download cancelled. See START-HERE.md for manual Python setup.' }
    & (Join-Path $PSScriptRoot 'get-python.ps1')
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Python preparation failed' }
}
if ($Python -eq (Join-Path $Root 'python/python.exe')) {
    $env:PYTHONHOME = Join-Path $Root 'python'
    $env:PYTHONPATH = $null
    $env:PYTHONNOUSERSITE = '1'
}
& $Python -B (Join-Path $PSScriptRoot 'enhanced_setup.py') @args
exit $LASTEXITCODE
