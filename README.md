# SK8V Enhanced

**Skate 3 gameplay in GTA V Enhanced Story Mode.**

![Enhanced edition](https://img.shields.io/badge/GTA_V-Enhanced-2288cc)
![Tested build](https://img.shields.io/badge/tested_build-1.0.1158.16-green)
![License](https://img.shields.io/badge/license-Apache--2.0-blue)

A community **Enhanced compatibility port of [SK8V by Hoppi / becaring](https://github.com/becaring/SK8V)**. The original release targets Legacy. This repository adapts the native host, live collision reader, animation hooks, board resources, HUD presentation and audio integration for Enhanced. The original Skate simulation and gameplay remain the foundation.

Maintained by **[Stonqz27](https://github.com/Stonqz27)**. Based on SK8V 1.0.9, commit [`15b9a3b`](https://github.com/becaring/SK8V/commit/15b9a3b7a4148cfd1fa233e4aac2034574cfe880). Original Git history, licenses and credits are preserved.

## Status

**Playable on the tested Enhanced installation.** On October 10, 2026, the maintainer reported that the full skating test stayed stable. Separate live checks passed for animation jobs, native board/weapon loading, Direct3D 12 presentation and GTA audio streams. A headless test exercised the actual Skate runtime, board, poses, camera, scoring HUD and audio together.

This is currently a **source release for developers**. A portable Enhanced setup wizard and ready-to-install binary release still need packaging. The upstream Legacy wizard is not an Enhanced installer. See [BUILDING.md](BUILDING.md) and [docs/INSTALLATION.md](docs/INSTALLATION.md).

## Requirements

- Windows x64 and **GTA V Enhanced 1.0.1158.16**, Story Mode.
- Compatible [ScriptHookV](https://www.dev-c.com/gtav/scripthookv/) and ASI loader. The test used its v3889.0 / 1158.13 distribution on Enhanced 1.0.1158.16.
- An Xbox or compatible XInput controller. Gameplay uses Skate 3's controller inputs.
- Your own Skate 3 Xbox 360 ISO or extracted disc folder. Tested: title ID `454108E6`, media ID `5C087C2C`, no title update.
- Enhanced-compatible OpenRPF for locally prepared DLC.
- Patched ReShade 6.8.0 and `SK8VHudBridge.addon64` as described in the build guide for the scoring HUD and Hall of Meat presentation.
- SSD recommended; several GB free for owned asset preparation.

**Story Mode only; disable BattlEye when using ASI mods.** Online and FiveM are unsupported. Other Enhanced patches need their own address/resource validation.

## Enhanced fixes

| Area | Adaptation |
|---|---|
| Animation | Validated Enhanced worker bindings; apply Skate poses after GTA's animation jobs. |
| Characters | Support larger skeletons; Franklin, Michael and Trevor pose layouts covered. |
| Collision | Enhanced table capacity, record layout, tagged instances and streaming-owner checks. |
| Board | Native seven-bone board, Gen9 drawable conversion and DLC loading. |
| Graphics | Original HUD through D3D11-on-12 on GTA's Direct3D 12 presentation path. |
| Audio | Enhanced native stream bindings, ring layout and lifetime handling. |
| Compatibility | Exclude unported Legacy private streaming/collider hooks. |

One Skate runtime supplies simulation and presentation. GTA renders the native player and board, without a second guest-character overlay.

## Controls

| Input | Action |
|---|---|
| Weapon wheel → **Skateboard**, melee slot | Select the board / enter Skate mode |
| Controller **Y** | Mount or dismount |
| Hold **Y** | Put the board away and return to GTA |
| Keyboard **/** or **Back + LB** | Open SK8V settings |

Other skating inputs are Skate 3's Flick-It controls. **O is not required.** Preparation runs in the background after the player loads; the first preparation on the tested machine took roughly 40–60 seconds.

## Known limitations

- Stable observed play does not establish every surface, mod combination or game patch. Long-session performance and collision edge cases need broader testing.
- An extended test logged `Nonfinite grab query` and returned to GTA control. This simulation issue remains open; the guard restores player tasks.
- Full NPC/car contact, death recovery and live Michael/Trevor gameplay need wider testing. Their pose layouts passed the headless test.
- `PedPoseCollision=0` disables the unported Legacy private registrar. The Legacy weapon-wheel icon hook is not ported.
- Some audio splice start offsets are unsupported; complete sound parity is not claimed.
- Upstream teleporting/Editor limitations still apply. Return to GTA control before teleporting.

See [docs/VALIDATION.md](docs/VALIDATION.md). Report issues with the exact game version, GPU and reproduction steps; avoid uploading game dumps, keys or extracted assets.

## Credits and license

- **Hoppi / becaring** — original SK8V host, integration and gameplay.
- **chasmlol and vladtrc** — pinned Rust engine/mashup used by SK8V.
- **Sol4ra** — live collision reader credited by upstream; attribution preserved.
- **Patrick Mours / crosire** — ReShade and addon API.
- **dexyfex / CodeWalker contributors** — separately supplied Gen9 reader for asset preparation.
- Other components retain their licenses; see [NOTICE](NOTICE).

SK8V and this port's project code use [Apache-2.0](LICENSE). ReShade headers and the separate audio decoder retain their own licenses.

No retail game assets, executables, decryption keys, private memory captures, compiled SDKs or personal settings are distributed here. Supply and convert your own games. This is an unofficial community port, unaffiliated with EA, Rockstar Games or Take-Two Interactive and not an official upstream SK8V release.
