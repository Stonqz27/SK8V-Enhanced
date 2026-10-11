# Enhanced asset preparation helper

This source helper reads the player's installed Gen9 resources through a separately supplied compatible CodeWalker Core build. It writes only under this checkout's ignored `local/` directory, rejects existing output files, and does not write game process memory.

Build with .NET 9 and a dependency directory containing CodeWalker Core, SharpDX, SharpDX.Mathematics and the Gen9 shader conversion XML:

```powershell
dotnet build tools/enhanced-assets/EnhancedAssets.csproj -c Release -p:CodeWalkerDir="C:\path\to\CodeWalker"
```

Run the resulting executable without arguments to see the command names. Each command reports its required arguments when called without them:

- `extract`: extract a selected owned archive entry.
- `skeletons`: export installed Enhanced ped skeletons and validate protagonist coverage.
- `shader-reference`: export a Gen9 shader reference as semantic XML.
- `convert-board`: convert an authored board into Gen9 YDR version 159; verify skeleton, vertices, indices, weights and texture pixels/mips.
- `inspect-clip`: inspect the prepared animation dictionary.
- `stage-pack`: stage the locally authored Enhanced board, type, live clip and weapon data into a local DLC archive.
- `stage-update`: stage a copy of an already OPEN modded update archive, add one DLC-list entry, preserve existing entries and compare the entire archive using bounded buffers. Unrelated changed bytes reject the candidate.

No game files are installed by the helper. All retail-derived output, extracted skeletons, resources and keys stay private. The generated clip must match `host/src/live_clip_layout_enhanced.h`; runtime skeletons cover Michael (354 bones), Franklin (317) and Trevor (282). The native board contract has seven bones.

This is a preparation component, not a complete portable setup wizard. See [installation](../../docs/INSTALLATION.md). Offline conversion alone does not prove in-game loading or collision behavior.
