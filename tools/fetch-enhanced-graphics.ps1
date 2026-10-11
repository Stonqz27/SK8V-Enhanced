# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
# Enhanced port addition (2026), Stonqz27. Fetch source only; never install into GTA.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskSource=Join-Path $taskRoot 'third_party\reshade-source-6.8.0'
$taskPin='18deaa52de0c425a78b329e9cb3c497281cd00ec'
if(-not (Test-Path -LiteralPath $taskSource)) {
    & git clone --branch v6.8.0 --recurse-submodules https://github.com/crosire/reshade.git $taskSource
    if($LASTEXITCODE -ne 0){throw 'ReShade source fetch failed.'}
}
$taskHead=& git -C $taskSource rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $taskHead -ne $taskPin){throw 'ReShade pin mismatch; existing source preserved.'}
& git -C $taskSource submodule update --init --recursive
if($LASTEXITCODE -ne 0){throw 'ReShade submodule fetch failed.'}
$taskPatch=Join-Path $PSScriptRoot 'reshade-internal-factory.patch'
# A reverse check makes repeated preparation idempotent.
$ErrorActionPreference='Continue'
& git -C $taskSource apply --reverse --check $taskPatch 2>$null
$taskAlreadyPatched=$LASTEXITCODE -eq 0
$ErrorActionPreference='Stop'
if(-not $taskAlreadyPatched) {
    & git -C $taskSource apply --check $taskPatch
    if($LASTEXITCODE -ne 0){throw 'Existing ReShade edits conflict; source preserved.'}
    & git -C $taskSource apply $taskPatch
    if($LASTEXITCODE -ne 0){throw 'ReShade patch failed.'}
}
Write-Output 'Pinned ReShade source and patch prepared. No game files changed.'
