# Changelog

## 1.1.8-enhanced.1-preview packaging, 2026-10-11

- Publish an asset-free binary prerelease with compiled Enhanced host/runtime, HUD bridge, patched ReShade and preparation tools.
- Add guided setup from an owned Xbox 360 ISO/extracted disc and Enhanced resources, plus documented separately supplied dependencies.
- Generate a Gen9 board/weapon DLC, prepare sound/HUD/skeleton data, and preserve existing modded DLC-list entries.
- Add version/dependency checks, conflict checks, timestamped backups, transactional installation and guarded restoration.
- Add installer regressions and package checksums; retain the earlier live checkpoint. New 1.1.8 live gameplay and wider first-run testing remain pending.

## SK8V 1.1.8 Enhanced source update, 2026-10-11

- Merge upstream 1.1.0 through 1.1.8 (9fbef44), retaining upstream history and all Enhanced compatibility adaptations.
- Adopt incremental collision building, cached dynamic-world lookups, controller/HID support and the long-uptime controller/snow/shooting fixes.
- Add gestures, posture/style/equipment settings, Trick Analyzer support and smoother wheel spin.
- Measure HUD frame cost on the Enhanced D3D12 dispatch rather than the unused Legacy callback.
- Exclude the new D3D11 snow-stencil detour and Legacy-only wheel-track texture registrar from Enhanced.
- Add Enhanced nearby-streaming and stencil-exclusion regression checks; allow the runtime smoke harness to test a new DLL without replacing the fixture installation.
- Document matching host/runtime deployment and owned-data preparation changes. New 1.1.8 live play testing remained pending at the source update; experimental packaging followed above.

## Enhanced compatibility port — initial source publication, 2026-10-11

- Adapt SK8V 1.0.9 to Enhanced 1.0.1158.16: animation jobs, live collision layout, larger protagonist skeletons and native board.
- Add Gen9 resource preparation, D3D12 HUD presentation and Enhanced native audio integration.
- Preserve the pinned upstream simulation, controls, scoring and bails; retain original credits and history.
- Record the stable user play test and publish source without retail assets.

Broader first-run setup/performance/interaction testing and the `Nonfinite grab query` edge case remain open.
