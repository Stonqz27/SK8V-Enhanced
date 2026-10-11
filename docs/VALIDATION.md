# Validation

Target: Enhanced **1.0.1158.16**, Windows x64; upstream SK8V **1.0.9 / 15b9a3b**.

## Live checks

- Animation: Franklin's 317-bone buffer; two worker layouts with 3,387 and 2,258 matches, zero mismatched counts.
- Resources: native board, dictionary and weapon loaded; all seven board bone tags/parents matched.
- Graphics: 555 callbacks, 555 interop-ready frames and 555 diagnostic frames after DXGI/COM fixes.
- Audio: two native three-second streams were heard; cursors, retained-ring ownership and stop cleanup passed without reported underruns/faults.
- Gameplay: maintainer reported stable full skating on October 10, 2026. Logs show skating, HUD/x-ray publication, bails/recovery and mount/dismount transitions. This is observed use, not exhaustive certification.

An extended session also logged `Nonfinite grab query`; skating exited and GTA tasks were restored. This remains open, alongside performance comparisons, complete surface coverage and wider dynamic interaction testing.

## Headless integration

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

## Code gates

Development passed seven C++ gates, including a private Franklin capture replay. Public source runs six asset-free gates; image/body audits are optional with each developer's own ignored fixtures. Seven Rust reader tests cover sparse records, bounds, tagged instances, streaming replacement, geometry/material placement, mover filtering and unchanged Legacy decoding. Retail fixtures are not supplied or downloaded by tests.

Before publication, the clean source checkout rebuilt the Enhanced host and addon and passed **6/6 C++ checks** without private captures or retail images. The parameterized .NET asset helper also compiled with a separately supplied CodeWalker build, with zero warnings/errors. Python and PowerShell source syntax checks and the public-tree artifact/credential audit passed.

Tested installation hashes (rebuilds may differ with toolchain/linker/path metadata):

```text
SkateVEnhanced.asi
6d6676e42881b74a23f7f5c75516408aa13d0ca83d86f337a6c0adc7f82cc58b
SkateVRuntime.dll
5ac10c15d2b7d9616d82cbcfba13af59ac4cbe81690b7943a0b45e8ad8cba091
```
