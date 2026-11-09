# Remix LOD Fade API

A game that cross fades a mesh and its lod (GTA IV draws both while one
fades in over the other) can pass each draw's opacity, and the runtime
dithers the draw by it. Rays skip a fading surface wherever a hash of the
hit position is at or above its opacity, so it stays fully lit where it
is hit and needs no sorting. The tuning surface is plain config:
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
mesh as if the bit were not set. Bit 10, `REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER`,
marks the opaque side of a cross fade while the other side fades (the
mesh a fading lod sits on, or the lod a fading mesh sits in). Bits 9, 10
and 24-31 do not change the material.

## How it works

The opacity lands on the instance's surface. Primary and indirect rays
re-trace past a skipped hit, the same way they pass clip planes. A fading
instance gets `VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR` so visibility
rays see it as a non-opaque candidate and its shadow fades too. Its
geometry flags stay as they are, so the BLAS is not rebuilt.

The pattern is made of world space cells of `cellSize`, with the lattice
shifted by half a cell so that a flat surface at a round height sits in
the middle of a cell and not on a boundary, where the float error of the
hit position would flip its cells along rings around the camera. A cell is
skipped when the low byte of its hash is at or above the opacity, so an
opacity of `a` keeps about `a / 256` of the surface. With `animated` the hash also
takes the frame index, and temporal accumulation turns the pattern into a
smooth blend. Without it the pattern is fixed: a dissolve without
temporal noise.

Bit 9 is part of the material identity, the opacity byte and the partner
bit are not: a fade starting or ending takes the dynamic instance update
once (the instance flag changes), the frames in between take the preserve
path, which refreshes the surface's opacity and partner bit. A retained
instance gets both through `UpdateRetainedInstances` with
`REMIXAPI_RETAINED_UPDATE_LOD_FADE` (`lodFadeState`: bits 0-7 the opacity,
bit 8 the partner) and stays pinned while it fades.

A ray tests the fade on the hit position from the hit distance along the
ray. A lod shares the planes of its parent's facade, and which of two
surfaces on one plane the traversal returns first is a matter of float
ulps, so the owner of a cell is decided per hit by a search of the slab
around it with every triangle as a candidate, each judged at the position
of the hit it may replace so that both sides see the same cell. A hit on a partner surface
goes to the nearest fading surface with a kept cell in the slab, if there
is one: the fading side wins where it keeps a cell, like the alpha pass
of the game blends it over the opaque side. A skipped primary or indirect
hit goes to the nearest candidate that is not a skipped cell; without one
the ray moves on through the triangle by the usual self intersection
offset, so geometry a little behind the cell (a lod inside its parent) is
still hit. Visibility rays skip the candidate.

## Options

All distances are in world units. The defaults are tuned for GTA IV (1 unit = 1 m).

| Key | Type | Default | Notes |
|---|---|---|---|
| `rtx.lodFade.enable` | bool | `True` | Master switch. Off draws fading draws opaque. |
| `rtx.lodFade.cellSize` | float | `0.1` | Edge length of a dither cell. |
| `rtx.lodFade.animated` | bool | `False` | New pattern every frame. |

Format conventions follow [`RemixSkyAPI.md`](RemixSkyAPI.md#format-conventions).

## Limits

- Visibility rays that only query opaque geometry
  (`visibilityModeOpaqueOnly`) and the fork's opaque-only probes (rain
  map, wetness occlusion, water shore) see a fading surface as solid.
- A cell smaller than a pixel at the fading distance turns the dissolve
  into per pixel noise; raise `cellSize` or use `animated`.
