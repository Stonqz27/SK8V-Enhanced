# Building SK8V Enhanced

Target: GTA V Enhanced **1.0.1158.16**. Base: SK8V **1.1.8 / 9fbef44**. Building source does not install or alter GTA.

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

Pinned engine sources remain in `upstream/`; 49 project patches are applied to `build/overlay/skate/`. Outputs:

- `build/enhanced-gameplay-package/SkateVEnhanced.asi`
- `build/enhanced-gameplay-package/SK8VHudBridge.addon64`
- `build/runtime/SkateVRuntime.dll`

The [1.1.8 upgrade notes](docs/UPSTREAM-1.1.8.md) list preparation changes and unported visuals. Build and deploy the host and runtime as a matching pair.

The public checkout runs seven C++ checks without retail fixtures. Optional image/body audits require the developer's own ignored `port-evidence/` files; the slower runtime smoke executable needs locally converted data. See [validation](docs/VALIDATION.md).

`BUILD.bat` / `tools/build-host.ps1` select Enhanced gameplay. Diagnostic modes remain explicit: Animation, Resources, Graphics or Audio. They cannot be mixed with gameplay.

Rust regression checks:

```powershell
cargo test --manifest-path rust/Cargo.toml --release --locked -p skatev-runtime -p skate-hud -p svwc --lib
```

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

Legacy `tools/prepare.py` and `tools/build-release.py` remain upstream reference. The Enhanced release uses `tools/enhanced_setup.py`; see [player setup](docs/PLAYER-SETUP.md) and the [installation contract](docs/INSTALLATION.md).

## Packaging an Enhanced release

Build the host, runtime, decoder, `live_clip` and authoring CLI, the patched
ReShade runtime, and the Enhanced asset helper first. Use Rust source-path
remapping before compiling binaries for distribution (the existing
`tools/build-release.py` documents its `remap_config` helper). Then run:

```powershell
python tools/test_enhanced_setup.py
powershell -NoProfile -STA -ExecutionPolicy Bypass -File tools/enhanced-wizard.ps1 -SelfTest -RenderDirectory build/wizard-preview
python tools/build-enhanced-release.py --version 1.1.8-enhanced.2-preview
```

The packager uses an allowlist and the pinned converter-tool snapshot, includes
dependency notices and decoder source, excludes separately supplied SDK/readers
and retail file formats, and refuses personal paths. It writes the Enhanced
ZIP, a release manifest, file checksums and an archive SHA-256 under ignored
`build/release/`. Commit the final source before packaging so the release
manifest identifies the source revision. Never package a maintainer's converted
assets, update archive, memory capture or absolute-path INI.
