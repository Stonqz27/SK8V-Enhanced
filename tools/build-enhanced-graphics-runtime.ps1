# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
# Requires the official v6.8.0 source/submodules with the recorded source patch.
# Builds only the local dependency, never modifies GTA or global Git settings.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskSource=Join-Path $taskRoot 'third_party\reshade-source-6.8.0'
$taskCommit=& git -c "safe.directory=$taskSource" -C $taskSource rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $taskCommit -ne '18deaa52de0c425a78b329e9cb3c497281cd00ec'){throw 'ReShade source pin mismatch.'}
$taskVs=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $taskVs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $taskVs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:PIP_NO_INDEX='1'
Push-Location $taskSource
try {
    & msbuild ReShade.sln /t:ReShade /p:Configuration=Release /p:Platform=64-bit /p:GladReproducible=false /p:CL_MPCount=4 /m:2 /nologo /v:minimal
    if($LASTEXITCODE -ne 0){throw 'Graphics runtime build failed.'}
} finally { Pop-Location }
& python (Join-Path $PSScriptRoot 'record-graphics-runtime.py')
if($LASTEXITCODE -ne 0){throw 'Graphics runtime provenance check failed.'}
