# No arguments opens the native wizard; command-line options keep console setup.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if ($args.Count -eq 0) {
    try {
        & (Join-Path $PSScriptRoot 'enhanced-wizard.ps1')
        exit 0
    } catch {
        Add-Type -AssemblyName System.Windows.Forms
        [void][Windows.Forms.MessageBox]::Show($_.Exception.Message, 'SK8V Enhanced / Could not open setup')
        exit 2
    }
}
$Worker = $args -contains '--wizard-worker'
$Download = $args -contains '--download-python'
if ($Worker) {
    [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
    $OutputEncoding = [Console]::OutputEncoding
}
$SetupArguments = @($args | Where-Object { $_ -notin @('--wizard-worker', '--download-python') })
try {
    $Python = Join-Path $Root 'python/python.exe'
    if (-not (Test-Path -LiteralPath $Python)) {
        foreach ($Existing in @(Get-Command python.exe -CommandType Application -All -ErrorAction SilentlyContinue)) {
            if ($Existing.Source -match '\\WindowsApps\\') { continue }
            $ErrorActionPreference = 'Continue'
            & $Existing.Source -c 'import sys, ctypes, hashlib, zlib, xml.etree.ElementTree, numpy, PIL.Image; assert sys.version_info >= (3, 11)' 2>$null
            $Code = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($Code -eq 0) { $Python = $Existing.Source; break }
        }
    }
    if (-not (Test-Path -LiteralPath $Python)) {
        if ($Worker -and -not $Download) { throw 'Enable the portable Python download, or install Python 3.11+ with NumPy and Pillow. See START-HERE.md.' }
        if (-not $Worker -and -not $Download) {
            Write-Host 'Setup can download verified portable Python with NumPy and Pillow into this folder.'
            if ((Read-Host 'Download them now? Type YES') -ne 'YES') { throw 'Download cancelled. See START-HERE.md.' }
        }
        Write-Output '@sk8v {"title":"Downloading the portable setup runtime","progress":0,"phase":"install"}'
        & (Join-Path $PSScriptRoot 'get-python.ps1')
    }
    if ($Python -eq (Join-Path $Root 'python/python.exe')) {
        $env:PYTHONHOME = Join-Path $Root 'python'
        $env:PYTHONPATH = $null
        $env:PYTHONNOUSERSITE = '1'
    }
    $env:PYTHONIOENCODING = 'utf-8'
    # Native stderr is log output, not a PowerShell terminating error.
    $ErrorActionPreference = 'Continue'
    & $Python -B -u (Join-Path $PSScriptRoot 'enhanced_setup.py') @SetupArguments
    exit $LASTEXITCODE
} catch {
    Write-Output ('Setup stopped: ' + $_.Exception.Message)
    Write-Output $_.ScriptStackTrace
    exit 2
}
