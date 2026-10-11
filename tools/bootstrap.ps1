# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
# Fetches the pinned sources and builds everything the release needs:
# the Rust runtime and tools, the ASI and rage.exe.
#   powershell -ExecutionPolicy Bypass -File tools\bootstrap.ps1
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'fetch-upstreams.ps1')
& (Join-Path $PSScriptRoot 'fetch-scripthookv.ps1')
& (Join-Path $PSScriptRoot 'build-rust.ps1')
& (Join-Path $PSScriptRoot 'build-host.ps1')
& (Join-Path $PSScriptRoot 'build-rage-cli.ps1')
Write-Host 'Enhanced host/runtime built. See BUILDING.md for graphics and owned asset preparation; the Legacy release packager is not an Enhanced installer.'
