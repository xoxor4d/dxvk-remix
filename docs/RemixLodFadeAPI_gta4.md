# Remix LOD Fade API

A game that cross fades a mesh and its lod (GTA IV draws both while one
fades in over the other) can pass each draw's opacity, and the runtime
fades the draw by it. Every ray of a pixel skips a fading surface when
the pixel's blue noise sample is at or above its opacity, so it stays
fully lit where it is hit and needs no sorting. The tuning surface is plain config:
`rtx.lodFade.*` options set via `SetConfigVariable` (or `rtx.conf` /
`user.conf`). There is no dedicated API function.

For the channel itself, see
[`RemixApi.md`](RemixApi.md#configuration--setconfigvariable). For the
implementation, see
[`rtx_fork_lod_fade.cpp`](../src/dxvk/rtx_render/rtx_fork_lod_fade.cpp),
[`rtx_fork_lod_fade.h`](../src/dxvk/rtx_render/rtx_fork_lod_fade.h) and
[`rtx_fork_lod_fade.slangh`](../src/dxvk/shaders/rtx/algorithm/rtx_fork_lod_fade.slangh).

## Which draws fade

Draws whose per draw modifier word (render state 149 on D3D9,
`remixapi_InstanceInfoD3DOverridesEXT::modifierFlags` on the API) has
bit 9 set, `REMIX_MODIFIER_FROM_D3D_LOD_FADE`. The top byte of the same
word (bits 24-31) is the opacity: `0` draws nothing, `255` draws the
mesh as if the bit were not set. Bits 9 and 24-31 do not change the
material. Bit 10 is free (a partner bit for the opaque side of a cross
fade lived there in 0.1000.9 and was removed in 0.1000.10).

## How it works

The opacity lands on the instance's surface. Primary and indirect rays
re-trace past a skipped hit, the same way they pass clip planes. A fading
instance gets `VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR` so visibility
rays see it as a non-opaque candidate and its shadow fades too. Its
geometry flags stay as they are, so the BLAS is not rebuilt.

The threshold is one blue noise sample per pixel and frame, the same
for every ray of the path (primary, bounces and visibility rays all run
in the pixel's thread), so a path sees one side of a cross fade and
temporal accumulation blends the two. A surface is skipped when the
sample is at or above its opacity, so an opacity of `a` keeps about
`a / 256` of the surface. A screen space sample has no lattice to alias
against the pixel grid.

Bit 9 is part of the material identity, the opacity byte is not: a fade
starting or ending takes the dynamic instance update once (the instance
flag changes), the frames in between take the preserve path, which
refreshes the surface's opacity. A retained instance gets the opacity
through `UpdateRetainedInstances` with `REMIXAPI_RETAINED_UPDATE_LOD_FADE`
(`lodFadeState`: bits 0-7, the rest 0) and stays pinned while it fades.

While anything fades, primary and indirect ray queries drop the
force-opaque flag, so a fading instance (non-opaque per instance) comes
up as a candidate during traversal and a skipped surface is simply not
committed: the traversal continues behind it with nothing re-traced, and
where a lod lies exactly on its parent's facade the opaque parent wins
the tie, so the fade never shows there. Every other non-opaque
candidate is committed as the flag would have. Visibility rays skip the
candidate the same way. The TraceRay path (`renderPassGBufferRaytraceMode`
2) has no candidates, and only there the resolve tests the committed hit
itself: a skipped hit is handed to the nearest surface in the slab
around it that the path keeps (the slab as wide as the self intersection
offset, reaching as far as the offset moves the ray through parallel
surfaces), else the ray moves on through the triangle by that offset.

## Options

All distances are in world units. The defaults are tuned for GTA IV (1 unit = 1 m).

| Key | Type | Default | Notes |
|---|---|---|---|
| `rtx.lodFade.enable` | bool | `True` | Master switch. Off draws fading draws opaque. |
| `rtx.lodFade.debugForceOpacity` | int | `255` | Fades every surface by this opacity, `255` = off. |

Format conventions follow [`RemixSkyAPI.md`](RemixSkyAPI.md#format-conventions).

## Limits

- Visibility rays that only query opaque geometry
  (`visibilityModeOpaqueOnly`) and the fork's opaque-only probes (rain
  map, wetness occlusion, water shore) see a fading surface as solid.
- The blend relies on temporal accumulation; with it off the fade is per
  pixel noise.
