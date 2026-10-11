# SK8V 1.1.8 Enhanced update

Upstream: [release 1.1.8](https://github.com/becaring/SK8V/releases/tag/1.1.8), commit `9fbef449b51178521495d3f9dd90c91f98f7fe18`. Previous Enhanced source: `d9b0cb601322e3df499a1b3d6c014812e4241233`, based on upstream 1.0.9. Target game remains **Enhanced 1.0.1158.16**, Story Mode.

## What changed

All 12 upstream commits after 1.0.9 are merged, rather than replacing the Enhanced port with the Legacy release. This preserves both histories and allows future upstream merges.

| Upstream change | Enhanced integration |
|---|---|
| 1.1.8 controller/automatic snow/backup shooting uptime fixes | Imported; scan deadlines start at the first current tick, not zero. |
| GTA's XInput import and PlayStation HID reader | Imported; missing GTA import falls back to this host's XInput path. New HID path still needs live hardware testing. |
| Incremental collision rebuild and background streaming checks | Imported; retains bounded Enhanced `phLevel` capacity, +0x20 record instances and streaming-owner validation. |
| Dynamic pool/model/door caches and control processing | Imported; direct entity translation is checked against the native before it is used. |
| Gestures, posture, riding style, truck/wheel configuration | New exports and engine patches included. |
| Trick Analyzer | Included; optional owned-data movie required. Uses the existing Enhanced HUD renderer. |
| Smoother wheel spin and shutdown/error handling | Imported. |
| HUD and animation timing diagnostics | Included; present timing wraps the actual Enhanced rendering dispatch. |
| Board snow stencil | Legacy D3D11 detour excluded on Enhanced; no D3D12 snow-stencil adaptation claimed. |
| Wheel tracks | Ground/material and tread logic included. Rendering is disabled on Enhanced because the new texture registrar uses private Legacy addresses. Gen9 texture DLC preparation is pending. |

The engine adoption pins remain unchanged. There are now **49** project patches. This merge does not change the user's separately planned custom mobility, carry, bail or HUD design.

## Data and deployment compatibility

The runtime ABI remains 8, but the host now requires five additional exports: `sv_set_gestures`, `sv_set_posture`, `sv_set_riding_style`, `sv_set_equipment` and `sv_ground_at`. Keep the new Enhanced host and runtime together. An older DLL is rejected with named missing exports; it must not be mixed into the new host.

Upstream's [1.1.4](https://github.com/becaring/SK8V/releases/tag/1.1.4) introduced the Trick Analyzer data preparation. From an extracted **owned** Skate 3 disc, export it into your existing converted assets:

```powershell
python tools/prepare-hom-hud.py --game "<owned extracted Skate folder>" --assets "<converted assets folder>" --movie trickanalyser
```

The output stays under `private/hud-analyser/`. If absent, the runtime logs that the analyzer is unavailable and continues with the existing scoring HUD. No game-derived data is published here.

Upstream's [1.1.6](https://github.com/becaring/SK8V/releases/tag/1.1.6) also changed setup to preserve a modded weapon-wheel HUD. Its wizard requires a fresh setup for 1.1.5 and older; that is a **Legacy** instruction. The source includes that change, but its OpenIV/Legacy discovery path is not a validated Enhanced/OpenRPF installer. Do not run the Legacy wizard against Enhanced or replace your working Enhanced DLC with its outputs.

Preserve existing menu settings and local caches. Build source with [BUILDING.md](../BUILDING.md), and follow the [Enhanced installation contract](INSTALLATION.md). This update publishes source only; no new player ZIP or installable binary release is claimed.

## Validation and limits

See [VALIDATION.md](VALIDATION.md) for the new source checks and the separate 1.0.9 live checkpoint. The 1.1.8 update has not been installed into the maintainer's GTA directory or tested in a live Story Mode session. Headless checks do not establish whole-map surface coverage, frame rate, PlayStation hardware behavior, or arbitrary mod compatibility.

The smoke harness can use existing private fixtures with a separately built DLL and a separate output directory:

```powershell
build/enhanced-gameplay-ninja/host/EnhancedRuntimeSmokeTests.exe "<fixture project>" "<new runtime DLL>" "<ignored evidence directory>"
```

This exercises a process-local synthetic floor, converted owned assets and all three protagonist skeletons; it does not attach to GTA.

## Rollback

The previous source checkpoint is preserved at commit `d9b0cb6`, with a local `backup/enhanced-1.0.9-before-1.1.8` branch. For an isolated old-source checkout:

```powershell
git worktree add ../SK8V-Enhanced-1.0.9 d9b0cb601322e3df499a1b3d6c014812e4241233
```

If you later install this update and need to undo it, close GTA and restore the previous **matching** host/runtime and any replaced settings or prepared resources from your own installation backup. This GitHub source update has left the current working game installation unchanged.
