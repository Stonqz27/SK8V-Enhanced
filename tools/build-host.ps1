# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
param([ValidateSet('Debug','Release')][string]$Config='Release')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build-enhanced-host.ps1') -Config $Config -ProbeMode Gameplay
if($LASTEXITCODE -ne 0){throw 'Enhanced host build failed.'}
