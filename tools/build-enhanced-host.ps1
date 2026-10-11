# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
param([ValidateSet('Debug','Release')][string]$Config='Release',
      [ValidateSet('Animation','Resources','Graphics','Audio','Gameplay')][string]$ProbeMode='Gameplay')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskVsWhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$taskVsInstall=& $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $taskVsInstall){throw 'Visual Studio C++ tools not found.'}
Import-Module (Join-Path $taskVsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $taskVsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$taskBuild=Join-Path $taskRoot $(if($ProbeMode -eq 'Gameplay'){'build\enhanced-gameplay-ninja'}elseif($ProbeMode -eq 'Audio'){'build\enhanced-audio-ninja'}else{'build\enhanced-ninja'})
if(-not (Get-Command ninja -ErrorAction SilentlyContinue)){
 $taskNinja=Join-Path $taskVsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
 if(-not (Test-Path -LiteralPath $taskNinja)){throw 'Ninja build tool not found.'}
 $env:PATH=(Split-Path -Parent $taskNinja)+';'+$env:PATH
}
$taskResourceProbe=if($ProbeMode -eq 'Resources'){'ON'}else{'OFF'}
$taskGraphicsProbe=if($ProbeMode -eq 'Graphics'){'ON'}else{'OFF'}
$taskAudioProbe=if($ProbeMode -eq 'Audio'){'ON'}else{'OFF'}
$taskGameplay=if($ProbeMode -eq 'Gameplay'){'ON'}else{'OFF'}
& cmake -S $taskRoot -B $taskBuild -G 'Ninja' "-DCMAKE_BUILD_TYPE=$Config" -DSK8V_ENHANCED=ON "-DSK8V_ENHANCED_RESOURCE_PROBE=$taskResourceProbe" "-DSK8V_ENHANCED_GRAPHICS_PROBE=$taskGraphicsProbe" "-DSK8V_ENHANCED_AUDIO_PROBE=$taskAudioProbe" "-DSK8V_ENHANCED_GAMEPLAY=$taskGameplay"
if($LASTEXITCODE -ne 0){throw 'Enhanced host configure failed.'}
& cmake --build $taskBuild --config $Config --parallel 4
if($LASTEXITCODE -ne 0){throw 'Enhanced host build failed.'}
& ctest --test-dir $taskBuild -C $Config --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Enhanced host interface checks failed.'}
$taskPackage=Join-Path $taskRoot $(if($ProbeMode -eq 'Gameplay'){'build\enhanced-gameplay-package'}elseif($ProbeMode -eq 'Audio'){'build\enhanced-audio-package'}else{'build\enhanced-package'})
New-Item -ItemType Directory -Path $taskPackage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskBuild 'host\SkateVEnhanced.asi') -Destination $taskPackage -Force
Copy-Item -LiteralPath (Join-Path $taskBuild 'host\SK8VHudBridge.addon64') -Destination $taskPackage -Force
Write-Output "Enhanced compatibility host built: $taskPackage\SkateVEnhanced.asi"
if($ProbeMode -eq 'Gameplay'){Write-Output 'Enhanced gameplay built. Prepare owned data and back up GTA before installation; see docs/INSTALLATION.md.'}
else{Write-Output 'This build collects port evidence; full skating activation is gated.'}
