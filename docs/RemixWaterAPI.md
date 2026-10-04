# Remix Water API

Water surfaces get a soft, animated shoreline where they meet opaque
geometry. The surface is plain config: `rtx.water.*` options set via
`SetConfigVariable` (or `rtx.conf` / `user.conf`). There is no dedicated
API function.

For the channel itself, see
[`RemixApi.md`](RemixApi.md#configuration--setconfigvariable). For the
implementation, see
[`rtx_fork_water.cpp`](../src/dxvk/rtx_render/rtx_fork_water.cpp),
[`rtx_fork_water.h`](../src/dxvk/rtx_render/rtx_fork_water.h) and
[`rtx_fork_water_shore.slangh`](../src/dxvk/shaders/rtx/algorithm/rtx_fork_water_shore.slangh).

## Which surfaces are affected

Translucent surfaces whose instance is in the **AnimatedWater** texture
category (`rtx.animatedWaterTextures`). Opaque materials and other
translucents are untouched.

## How it works

At every resolved hit on such a surface, a short ray is traced straight
down (world `-Z` or `-Y`, depending on `rtx.zUp`) against opaque geometry
only, up to `shoreFadeDistance`. The hit distance is the water depth. No
hit means the water is deep and nothing changes. The depth probes only
count static ground. Surfaces that moved or were skinned this frame
(characters, moving props) are skipped. Otherwise a body standing in the
water would read as shallow ground directly above it, and the fade would
step at the body's outline.

A single probe can slip through cracks and T-junctions between separate
meshes and read as deep water. When the center probe misses, two more
probes are traced, offset horizontally along the two world axes by
`shoreProbeSpread` plus the pixel footprint. The shallowest hit is used.
The three points are not collinear, so a straight crack cannot swallow
all of them. A center hit costs nothing extra.

The depth is offset by a pseudo-height: the signed slope of the animated
water normal along a horizontal diagonal, times `shoreHeightScale`. It
averages to zero and follows the wave frequency, so the shoreline is
irregular and moves with the normal animation.

```
fade = saturate((depth + slope * shoreHeightScale - shoreCutDepth) / shoreFadeWidth)
```

Objects that intersect the water (bodies, props) are often over deep
ground, so the depth probe alone leaves a hard edge around them. A
second, soft-intersection probe continues along the incoming ray into
the water. The distance to an opaque hit fades the water like a soft
particle:

```
fade = min(fade, smoothstep(0, 1, (viewDistance + slope * shoreHeightScale - shoreCutDepth) / objectFadeWidth))
```

The raw distance along the ray is used, not the depth below the surface,
so grazing views fade less. Over plain terrain this term never fades more
than the depth term does, because the distance along the ray is never
shorter than the vertical depth.

As `fade` goes to 0, the water's base reflectivity, normal perturbation
(blended toward the view direction, which removes grazing Fresnel),
refraction (IOR toward 1), thin-wall absorption, diffuse layer and
emission all fade out. At exactly 0 the water hit is skipped altogether:
the ray continues as if the water were not there, the same way stencil
cutters skip it, so no reflection or transmission ray is spawned. This
removes the bright reflection line where water meets the shore and the
hard edge.

## Options

All distances are in world units (Call of Duty: ~1 inch).

| Key | Type | Default | Notes |
|---|---|---|---|
| `rtx.water.shoreFadeEnable` | bool | `True` | Master switch. |
| `rtx.water.shoreFadeDistance` | float | `48.0` | Probe range. Raised internally to at least `shoreCutDepth + shoreFadeWidth + shoreHeightScale`. |
| `rtx.water.shoreCutDepth` | float | `0.5` | Depth at and below which water is fully faded. |
| `rtx.water.shoreFadeWidth` | float | `16.0` | Depth range of the fade above `shoreCutDepth`. |
| `rtx.water.shoreHeightScale` | float | `24.0` | Depth offset per unit of normal slope. `0` gives a smooth, depth-only fade. |
| `rtx.water.shoreProbeSpread` | float | `0.5` | Horizontal offset of the two extra depth probes traced when the center probe misses, added to the pixel footprint. |
| `rtx.water.objectFadeWidth` | float | `6.0` | Soft-intersection fade width along the view ray. `0` disables the object probe. |

Format conventions follow [`RemixSkyAPI.md`](RemixSkyAPI.md#format-conventions).

## Limits

- Direct-light visibility rays (shadows cast through thin-walled water)
  do not apply the fade.
- On paths that re-read translucent surfaces from the G-buffer
  (non-PSR integration of the primary surface), the IOR fade is not
  applied. Reflectivity, normal and color fades are.
- Probes only see `OBJECT_MASK_OPAQUE` instances. Player-model and
  view-model instances do not fade the water.
- Static means "did not move this frame". A character that is fully
  frozen and not skinned counts as ground.
- Cost per resolved hit on AnimatedWater translucents: one short depth
  ray where it hits ground, three where it misses (deep water). Add one
  object ray wherever the water is not fully faded. Depth rays inspect
  each candidate's surface flags in the traversal loop. All rays are
  short and use no any-hit shaders.
