# Enhanced installation contract

This is the tested compatibility source, without a portable Enhanced setup wizard. The original Legacy release and `tools/prepare.py --install` are not Enhanced installers.

Prepare data from the player's own copies. Upstream scripts produce core Skate data, character animation data, HUD, Hall of Meat, audio cache and tuning. The Enhanced helper exports Gen9 ped skeletons and converts the native seven-bone board. The generated live clip must match the host's Enhanced bone-tag layout.

| Local data | Setting / role |
|---|---|
| Converted Skate assets | `DataRoot` |
| Enhanced ped skeleton cache | `PedCache` |
| Decoded audio cache and tuning | `AudioCache` |
| Optional world sidecars/templates | `WorldCache` |
| Locally generated board, clip and weapon DLC | `mods/update/x64/dlcpacks/skatev/dlc.rpf` |

Live geometry comes from the Enhanced collision reader. Missing optional caches can reduce dynamic collision/effect fidelity.

## Updating from the initial Enhanced checkpoint

Read [the 1.1.8 upgrade notes](UPSTREAM-1.1.8.md). Deploy the newly built Enhanced ASI and runtime together; the new host rejects an older runtime missing required exports. The maintainer's working game installation has not been replaced by this source update. Upstream's 1.1.8 Legacy ZIP is not an Enhanced update package.

Trick Analyzer display needs a fresh owned-data export. The upstream 1.1.6 setup change also updates the weapon-wheel HUD asset; its OpenIV/Legacy discovery path does not establish OpenRPF/Enhanced compatibility. Preserve your existing Enhanced DLC and modded HUD until the Enhanced preparation path is validated.

## Install layout

After backing up and **closing GTA**, the installation needs:

- `SkateVEnhanced.asi` and `SkateVRuntime.dll` from this port.
- `SkateVLegacy.ini` with the player's data paths. The basename is kept for compatibility with the settings loader.
- `SK8VHudBridge.addon64`, patched ReShade as `dxgi.dll`, and compatible ReShade settings.
- ScriptHookV/ASI loader and Enhanced-compatible OpenRPF from their publishers.
- Locally prepared `skatev` DLC and a `dlcpacks:/skatev/` entry in the **modded** DLC list. Preserve existing entries and the previous modded archive.

Run one skating host at a time. Preserve other graphics mods and configurations. Generate the INI from `tools/SkateVLegacy.ini.template` with the player's paths, retaining sections/defaults and these tested choices:

```ini
Runtime=1
PosePed=1
BoardNative=1
DrawSkater=0
LiveClip=0
PedPoseCollision=0
BackgroundPrepare=1
AudioOutput=Gta
```

`DrawSkater=0` avoids a costly guest-mesh fallback. GTA renders the native player and board; the verified pose hook applies animation after GTA jobs. `PedPoseCollision=0` excludes the unported Legacy private registrar.

## Play and recovery

Launch Story Mode with BattlEye disabled, let preparation finish, select Skateboard in the melee wheel and tap **Y**. Test flat ground, a small ollie, then dismount/remount. `/` or **Back + LB** opens settings. Logs: `%LOCALAPPDATA%/SkateV`.

Close GTA before restoring the previous ASI/runtime/settings. To remove the entire port, also restore previous graphics settings and the modded DLC list, removing only this port's added resources. Keep converted data/backups until recovery is verified. A portable installer must record ownership and preserve unrelated mods.
