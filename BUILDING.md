# Building SK8V Enhanced

Target: GTA V Enhanced **1.0.1158.16**. Base: SK8V **1.0.9 / 15b9a3b**. Building source does not install or alter GTA.

## Toolchain

- Windows x64, Git, Python 3.11+; .NET 9 for the optional asset helper.
- Rust/rustup; this port pins the tested **1.99.0** toolchain.
- Visual Studio C++ x64 tools, Windows SDK, CMake 3.24+ and Ninja. Scripts discover Visual Studio with `vswhere`.
- NumPy and Pillow for asset conversion; preparation scripts document further per-step dependencies.
- ScriptHookV SDK fetched from its publisher by the script below; never committed.

## Host and runtime

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-upstreams.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-scripthookv.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-rust.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-enhanced-host.ps1 -ProbeMode Gameplay
```

Pinned engine sources remain in `upstream/`; 44 project patches are applied to `build/overlay/skate/`. Outputs:

- `build/enhanced-gameplay-package/SkateVEnhanced.asi`
- `build/enhanced-gameplay-package/SK8VHudBridge.addon64`
- `build/runtime/SkateVRuntime.dll`

The public checkout runs six C++ checks without retail fixtures. Optional image/body audits require the developer's own ignored `port-evidence/` files; the slower runtime smoke executable needs locally converted data. See [validation](docs/VALIDATION.md).

`BUILD.bat` / `tools/build-host.ps1` select Enhanced gameplay. Diagnostic modes remain explicit: Animation, Resources, Graphics or Audio. They cannot be mixed with gameplay.

## Graphics dependency

The tested HUD uses an **unofficial patched ReShade 6.8.0**, commit `18deaa52de0c425a78b329e9cb3c497281cd00ec`. Its small patch preserves an internal DXGI factory request during D3D11-on-12 creation; the host also handles native/proxy COM identity.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-enhanced-graphics.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-enhanced-graphics-runtime.ps1
```

The fetch script gets pinned public source/submodules and applies `tools/reshade-internal-factory.patch`. Build output is `local/graphics-runtime/ReShade64.dll`. ReShade has its own SDK/generator prerequisites; follow its pinned build guide for missing dependencies. Generated receipts stay local and ignored.

It is used as `dxgi.dll` beside GTA with the addon and `tools/SkateVEnhanced.ReShade.ini`. Preserve existing ReShade/graphics mods and merge configurations deliberately.

## Enhanced asset helper

Supply a compatible Gen9-capable CodeWalker Core build. Put `CodeWalker.Core.dll`, `SharpDX.dll`, `SharpDX.Mathematics.dll` and `ShadersGen9Conversion.xml` in one directory:

```powershell
dotnet build tools/enhanced-assets/EnhancedAssets.csproj -c Release -p:CodeWalkerDir="C:\path\to\CodeWalker"
```

The helper reads owned Enhanced resources and writes only under the ignored `local/` tree. Commands cover skeletons, shader reference extraction, verified board conversion, clip inspection and DLC/update staging. See its [guide](tools/enhanced-assets/README.md).

## Player setup

Legacy `tools/prepare.py` and release-packaging scripts remain upstream reference, not a complete Enhanced setup flow. The tested installation uses freshly converted Skate assets, Enhanced skeletons/clip/board DLC, host/runtime and graphics integration. See the [installation contract](docs/INSTALLATION.md).

A portable installer is separate work. Do not distribute a maintainer's converted assets, full update archive, private capture or absolute-path INI.
