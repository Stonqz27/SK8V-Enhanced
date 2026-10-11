#pragma once
// Keeps GTA's snow off the board. GTA's deferred snow pass (deferred_lighting
// PS_snow) whitens every upward-facing G-buffer pixel whose stencil material
// (low 3 bits) is 0 (default) or 4 (terrain), unless bit 8 (interior) is set;
// peds (1) and vehicles (2) take snow from their own shaders or none. The
// board draws as a default prop, so its deck took snow, and its underside
// too when it faced up mid-flip. While Active (skating), the board's G-buffer draws
// write material 1 instead (the lighting shaders read only bit 8).
//
// The board is found at draw time by its diffuse texture in pixel-shader slot 0:
// uncompressed BGRA/RGBA with a full mip chain, 512 (deck) or 128 (trucks,
// wheels) square. GTA's textures are almost all block-compressed; a false
// match only takes the snow off that object.
namespace boardstencil {

// Registers a Present callback that detours the device's indexed draws
// on the render thread (once); Stop puts them back.
void Start(void (*log)(const char*));
void Stop();
void SetActive(bool on);
// Since the last call, while active: indexed draws on the immediate and
// deferred contexts, those with the board's texture bound, those
// into the G-buffer, and those re-marked.
struct Stats { unsigned immediate, deferred, seen, gbuffer, marked; };
Stats Take();
// Every indexed draw GTA made since the last call, active or not: how much
// scene each camera draws, for the frame report.
unsigned TakeAllDraws();

} // namespace boardstencil
