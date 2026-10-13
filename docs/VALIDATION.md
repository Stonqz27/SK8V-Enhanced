# Validation

Target: Enhanced **1.0.1158.16**, Windows x64; current upstream SK8V **1.1.8 / 9fbef44**.

## Setup wizard checks (2026-10-13)

The `1.1.8-enhanced.2-preview` installer adds the native monochrome wizard.
All four gameplay/graphics payload hashes match the first installer release.

- Rendered and inspected the actual WinForms source, review, progress, completion
  and error screens, with the bundled Space Grotesk font privately loaded.
- Exercised the worker output queue, stderr capture, exit code, JSON stage
  messages, retry and completion states. Paths with spaces, quotes, ampersands,
  trailing backslashes and Unicode passed through without shell interpretation.
- Relocated the packaged release into a path containing spaces, an ampersand
  and Unicode; both wizard rendering and the console entry point worked.
- The packaged wizard surfaced a real preflight rejection against a fake game
  directory (or the running-game guard) and saved the full error. It did not
  install or prepare retail data during this check.
- All eight backup/restore installation regressions still passed.

These checks cover the new interface and its worker boundary. The complete
fresh-PC disc conversion and live installation flow still needs wider testing.

## Experimental binary release checks (2026-10-11)

The `1.1.8-enhanced.1-preview` download includes the matching Enhanced host/
runtime and a new preparation/installation path. It is a prerelease, not new
evidence of live gameplay stability or a complete clean-PC installation test.

- Rebuild the runtime and asset authoring CLI with personal source paths remapped.
- Recheck all **7 C++ gates** and **183 Rust tests** (0 failures, 2 intentional skips).
- The rebuilt runtime passes the **600-frame** coupled owned-data smoke check:
  finite character/board/camera poses, HUD publication and audible-energy audio
  output with no pull underruns. No GTA process is touched by this harness.
- **8 installer regressions** exercise fresh installation/restoration, existing
  graphics conflicts, explicit backed-up replacement, preference preservation,
  protection of subsequently modified files, injected mid-install failure,
  path escapes and unrelated-file restoration rejection, using temporary folders.
- Owned-resource shader export, seven-bone board Gen9 conversion and board/weapon
  DLC staging pass model/texture/packed-resource preservation checks.
- Fresh-install encrypted `update.rpf` staging passes a full archive comparison
  against an isolated copy of the installed base archive. Only permitted header/
  DLC-list ranges change; all existing DLC entries are preserved. The source and
  actual game installation are unchanged.
- Retry staging uses the resulting modded archive: the existing Skate DLC entry
  is reused, no duplicate is added, and unrelated bytes remain unchanged.
- Verified portable Python/NumPy/Pillow downloads and a relocated, extracted ZIP
  launcher work. The package manifest verifies every shipped file by SHA-256.
- Allowlist packaging excludes retail formats, private caches, dependency SDK/
  reader assemblies and personal paths; notices cover 327 resolved dependency
  packages. The separate LGPL decoder's source accompanies its binary.

Setup's full disc-to-live-game flow, controller variants, third-party graphics
combinations and other PCs still need testing. Optional baked world/template
caches are absent from the first download; wheel-track visuals are disabled.
See [player setup and limitations](PLAYER-SETUP.md).

## 1.1.8 source-update checks (2026-10-11)

- All 49 engine patches apply to the pinned adoption source; upstream commit pins are unchanged.
- Enhanced release host and HUD addon build; **7/7 C++ checks** pass, including a new gate that links the Enhanced stencil exclusion without D3D11/ScriptHook dependencies.
- Release-mode Rust unit checks: **183 passed, 0 failed, 2 ignored** (21 HUD, 139 runtime, 23 collision-format tests). The ignored tests exercise an intentional fault and an owner-only ground fixture benchmark.
- Eight live-reader tests cover both layouts, including the new Enhanced nearby-change/rebuild regression.
- Matching release runtime, audio decoder and preparation tools build successfully.
- Mixed-version check: the new host bridge rejects the old runtime and names all five missing exports.
- New runtime smoke check: **600** finite Franklin poses, board/camera/HUD publications; Michael and Trevor skeleton changes also pass (317 / 354 / 282 bones). The seven-bone board travels **35.4604 m**, with maximum riding separation **0.244375 m**, on a process-local synthetic Enhanced floor.
- HUD rasterization: 51 textures, 19 draw commands and 6,689 visible pixels. Audio: **2,702,592** samples, positive energy, zero pull underruns. New gesture/posture/style/equipment exports and ground-height/normal/material publication pass.
- No x-ray vertices were produced by this particular run; it does not validate live bail/x-ray presentation. The optional newly prepared Trick Analyzer movie was absent in the older owned-data fixture.
- Python/PowerShell syntax and the **361-file** public artifact/credential audit pass.
- Installed ASI and runtime hashes still match the previous live checkpoint; no game installation was changed.

Locally built 1.1.8 hashes (not distributed binaries; rebuild metadata can differ):

```text
SkateVEnhanced.asi
c413eaf4727946fd16466ae948128f4c10c9654d349a6ede240c32a0838487a3
SkateVRuntime.dll
aff3bda9645d94f5c42777448d1eead8a33bfdf82cefb5821e90992438a5cdce
```

The 1.1.8 update has **not** been installed into GTA or live-tested in Story Mode. The Legacy D3D11 snow stencil and Legacy loose track-texture registration are deliberately excluded on Enhanced. [Upgrade notes](UPSTREAM-1.1.8.md) describe those limits and data preparation.

## Previous 1.0.9 Enhanced live checkpoint

The following live observations and installed hashes belong to the initial port (`d9b0cb6`, upstream `15b9a3b`); they are not evidence of 1.1.8 live stability.

### Live checks

- Animation: Franklin's 317-bone buffer; two worker layouts with 3,387 and 2,258 matches, zero mismatched counts.
- Resources: native board, dictionary and weapon loaded; all seven board bone tags/parents matched.
- Graphics: 555 callbacks, 555 interop-ready frames and 555 diagnostic frames after DXGI/COM fixes.
- Audio: two native three-second streams were heard; cursors, retained-ring ownership and stop cleanup passed without reported underruns/faults.
- Gameplay: maintainer reported stable full skating on October 10, 2026. Logs show skating, HUD/x-ray publication, bails/recovery and mount/dismount transitions. This is observed use, not exhaustive certification.

An extended session also logged `Nonfinite grab query`; skating exited and GTA tasks were restored. This remains open, alongside performance comparisons, complete surface coverage and wider dynamic interaction testing.

### Previous headless integration

Actual runtime DLL and converted data, one session on a synthetic Enhanced floor:

| Measure | Result |
|---|---:|
| Character/board/camera/HUD frames | 600 each |
| Franklin / Michael / Trevor pose bones | 317 / 354 / 282 |
| Board bones | 7 |
| Travel | 35.46 m |
| Maximum riding root/board separation | 0.244 m |
| Audio samples | 2,740,224 |
| Pull underruns | 0 |

Original scoring HUD was rasterized through D3D11 WARP. Synthetic floor results do not establish whole-map performance or full x-ray rendering.

### Previous publication gates

Development passed seven C++ gates, including a private Franklin capture replay. Public source runs six asset-free gates; image/body audits are optional with each developer's own ignored fixtures. Seven Rust reader tests cover sparse records, bounds, tagged instances, streaming replacement, geometry/material placement, mover filtering and unchanged Legacy decoding. Retail fixtures are not supplied or downloaded by tests.

Before publication, the clean source checkout rebuilt the Enhanced host and addon and passed **6/6 C++ checks** without private captures or retail images. The parameterized .NET asset helper also compiled with a separately supplied CodeWalker build, with zero warnings/errors. Python and PowerShell source syntax checks and the public-tree artifact/credential audit passed.

Tested installation hashes (rebuilds may differ with toolchain/linker/path metadata):

```text
SkateVEnhanced.asi
6d6676e42881b74a23f7f5c75516408aa13d0ca83d86f337a6c0adc7f82cc58b
SkateVRuntime.dll
5ac10c15d2b7d9616d82cbcfba13af59ac4cbe81690b7943a0b45e8ad8cba091
```
