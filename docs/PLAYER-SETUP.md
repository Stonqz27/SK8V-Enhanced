# Install SK8V Enhanced

Download the **SK8V-Enhanced-1.1.8-enhanced.2-preview.zip** asset from
[Releases](https://github.com/Stonqz27/SK8V-Enhanced/releases), not GitHub's automatic Source code ZIP.

This is an experimental community release based on Hoppi/becaring SK8V 1.1.8.
The initial 1.0.9 Enhanced port had successful live gameplay tests. The 1.1.8
host/runtime passed local regression and coupled headless tests; a new live
1.1.8 gameplay test and testing on other PCs are still pending. First-run setup
is new. Report setup failures with its final error and your versions.

## Before setup

You need:

- **Windows x64, GTA V Enhanced 1.0.1158.16**, and an Xbox/XInput controller.
  Other game patches, Legacy, Online and FiveM are unsupported by this release.
- Your own **Skate 3 Xbox 360 ISO or extracted disc folder** containing
  `default.xex` and the `data` folder. PS3 files and `default.xex` alone do not
  work. The tested disc is title ID `454108E6`, media ID `5C087C2C`, without a title update.
- [Script Hook V and its ASI loader](https://www.dev-c.com/gtav/scripthookv/).
  Copy `ScriptHookV.dll` and `dinput8.dll` from its `bin` folder next to
  `GTA5_Enhanced.exe`. Use a distribution compatible with your Enhanced patch.
  ScriptHookVDotNet and NativeUI are not required for SK8V.
- [OpenRPF for GTA V Enhanced](https://www.gta5-mods.com/tools/openrpf-openiv-asi-for-gta-v-enhanced).
  Install `OpenRPF.asi` next to `GTA5_Enhanced.exe` following its publisher's instructions.
- [Microsoft .NET 9 x64 Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0).
  The asset helper uses .NET 9; the SDK is unnecessary for the release.
- A **Gen9/Enhanced-capable [CodeWalker](https://github.com/dexyfex/CodeWalker)**
  build supplied separately. Setup needs these four files together:
  `CodeWalker.Core.dll`, `SharpDX.dll`, `SharpDX.Mathematics.dll`, and
  `ShadersGen9Conversion.xml`. Use the author's current Enhanced build;
  a Legacy-only build will not read Enhanced resources. The shader XML is also
  in the author's `CodeWalker.Core` source directory. The maintained preparation
  helper was built against CodeWalker source commit `485d56bec00262ed7fa472261cce7bbc6202b96e`.
- Python 3.11+ with NumPy and Pillow, **or let setup download its verified
  portable Python packages**. It asks first and installs them only in the
  extracted setup folder. For an existing Python: `python -m pip install numpy Pillow`.
- An SSD is recommended. Allow roughly **15 GB free** for preparation and
  additional space in the GTA directory for a modded update archive and backups.
  Required space varies with disc contents and existing mods.

Disable **BattlEye** in the Rockstar launcher before running Story Mode with ASI
mods. Close GTA completely before setup. Move other skate ASIs such as
`SkateVLegacy.asi` or `NativeFranklin.asi` out of the game folder.

## Guided setup

1. Extract the **whole ZIP** to a short, permanent path such as
   `C:\Mods\SK8V-Enhanced`. Do not run it inside the ZIP. Keep this folder afterward:
   GTA reads the converted assets from it.
2. Run **SK8V Enhanced Setup.cmd** to open the black-and-white setup wizard.
   Its Space Grotesk font is bundled and loaded privately; no font installation.
   If you need write access to a protected GTA
   directory, use Run as administrator. Setup does not automatically elevate.
3. Browse to the folder containing `GTA5_Enhanced.exe`, your own Skate 3 ISO/extracted
   disc folder, and the CodeWalker dependency folder. Choose Regular or Goofy,
   then press **Continue**.
4. Review the paths and portable Python download choice. If existing graphics
   files are found, explicitly choose whether to back up and replace them.
   Press **Install**. Let preparation finish. It extracts and converts your Skate assets, audio
   and HUD, reads Enhanced ped skeletons, and creates a native Enhanced board/
   weapon DLC. Completed stages are reused when you rerun the same setup.
5. The wizard shows real preparation stages, elapsed time and completion.
   Progress is based on completed stages, not bytes or a time estimate. **Show details**
   displays console output; **Open log** opens the full saved log. Press **Finish**
   when installation succeeds. This release uses an unofficial patched
   ReShade 6.8.0 for the Skate HUD; existing graphics configurations need review
   before replacement.
6. Launch **Story Mode** with BattlEye disabled. Initial background preparation
   can take around 40-60 seconds on the tested PC. Select **Skateboard** in the
   melee weapon-wheel slot. Tap **Y** to mount/dismount; hold **Y** to put it away.
   Press **/** or **Back + LB** for the SK8V settings menu.

GTA's original archives remain unchanged. Setup stages a copy of the current
`mods/update/update.rpf` (or the original update archive on a fresh install),
preserves its existing DLC entries, and adds `dlcpacks:/skatev/` once. The helper
checks the whole candidate archive for unrelated byte changes before installation.

The release contains compiled mod binaries, preparation tools and licenses.
It contains **no Skate/GTA game assets, executables, decryption-key files,
CodeWalker assemblies, ScriptHookV or OpenRPF**. Obtain dependencies from their
publishers and prepare game data locally.

## Command-line setup

From the extracted release folder, with a suitable Python:

```powershell
python tools/enhanced_setup.py --gta "D:\Games\GTA V Enhanced" --preflight
python tools/enhanced_setup.py --gta "D:\Games\GTA V Enhanced" --skate "D:\OwnedGames\Skate3.iso" --codewalker "C:\Tools\CodeWalker" --stance Regular
```

Use `--prepare-only` to generate assets without installing. Use
`--allow-graphics-replacement` only when you have reviewed replacement of an
existing ReShade/graphics setup. The `.cmd` launcher forwards these options and
can use the portable Python copy.

## Updating and restoring

Keep the previous release folder and its generated data. Extract the new
release to a different permanent folder and run its setup. Install the new ASI
and runtime as a matching pair. Do not mix upstream's Legacy update ZIP with
this Enhanced build. Setup preserves menu preferences and forces the Enhanced
compatibility choices. New exports, including Trick Analyzer, are generated
from your disc by setup.

Installation creates `SK8V-Enhanced.installed.json` and timestamped backups
under `SK8V-Enhanced-backups` in the GTA folder. To restore the immediately
previous files, close GTA and run:

```powershell
python tools/enhanced_setup.py --gta "D:\Games\GTA V Enhanced" --restore
```

Or use `SK8V Enhanced Setup.cmd --gta "D:\Games\GTA V Enhanced" --restore`.
The wizard also has **Restore a previous install** on its first page: select
the GTA folder, click the link and review its restore confirmation.
Restoration keeps converted data and retires the current files into the backup
folder. It refuses to overwrite immutable files changed by another mod after
installation. In that case, merge manually from the recorded backup.

## Troubleshooting and current limits

- **Version rejected:** this release's private engine addresses are checked for
  Enhanced 1.0.1158.16. A different patch needs a separate compatibility update.
- **CodeWalker/.NET error:** check the four dependency files, Gen9 support and
  .NET 9 x64 Runtime. Reopen setup after installing the runtime.
- **Missing board:** check OpenRPF and ASI-loader installation, then the generated
  `mods/update/x64/dlcpacks/skatev/dlc.rpf` and DLC-list entry. GTA must be fully
  restarted after installation.
- **No HUD/graphics conflict:** check the bundled patched `dxgi.dll` and
  `SK8VHudBridge.addon64`; restore your previous graphics files if incompatible.
- **Permission error:** close GTA; use a writable directory or run setup as
  administrator for a protected game folder.
- **Preparation interrupted:** rerun setup with the same inputs. Incomplete
  stages are retained separately and rebuilt. If installation itself was
  interrupted, run `--restore` first.
- **Setup logs:** `<extracted release>\local\setup-logs\setup-*.log` contains
  full setup output, including stderr and the final traceback. Open it from
  the wizard. Completed preparation stages can be reused by **Try again**.
- **Gameplay logs:** `%LOCALAPPDATA%\SkateV\SkateVLegacy.log` and `SkateVRuntime.log`.
  Include your exact game version, GPU, setup error and reproduction steps in
  [an issue](https://github.com/Stonqz27/SK8V-Enhanced/issues). Do not attach
  game dumps, keys or extracted assets.

The live Enhanced collision reader is included. Optional baked world sidecars
and collision-template caches are not distributed in this release; some
dynamic collision/effect behavior may differ from upstream's fully prepared
Legacy setup. The Legacy private posed-ped registrar and wheel-icon hook remain
disabled. Wheel/snow-track texture visuals remain unported and are disabled in
the generated INI. Full car/ped interactions, death recovery, custom peds and
long sessions need wider testing. Skate simulation guards may return control
to GTA on invalid state. Stable earlier play is not proof of every surface.

Credits: Hoppi/becaring, chasmlol, vladtrc, Sol4ra, ReShade and CodeWalker
contributors; see `NOTICE` and `THIRD-PARTY-NOTICES.txt`. Modified ReShade source
and patch are documented in [BUILDING.md](https://github.com/Stonqz27/SK8V-Enhanced/blob/main/BUILDING.md).
The separate LGPL audio decoder's source and license accompany the package;
its standalone source folder can be built with `cargo build --release`.
