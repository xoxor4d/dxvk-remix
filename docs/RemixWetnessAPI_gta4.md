# Remix Wetness API (GTA IV branch)

Per draw wetness for D3D9 and API draws: roughness scaled by the game's wetness, puddles, rain shadow
and raindrop impacts. The per draw inputs come from the game through render state 210 (the D3D9 path) or
`remixapi_InstanceInfoD3DOverridesEXT::packedWetness` (the API path); the distance limits are plain
config, `rtx.wet.*` options set via `SetConfigVariable` (or `rtx.conf` / `user.conf`).

For the channel itself, see [`RemixApi.md`](RemixApi.md#configuration--setconfigvariable). For the
implementation, see
[`rtx_fork_wetness.cpp`](../src/dxvk/rtx_render/rtx_fork_wetness.cpp) and
[`rtx_fork_wetness.slangh`](../src/dxvk/shaders/rtx/concept/surface_material/rtx_fork_wetness.slangh).

## The packed per draw value

32 bits, written by the mod (`renderer::set_remix_roughness_scalar`):

| Bits | Content |
| :-- | :-- |
| 0-4 | normal blend width (0..1 over 31) |
| 5-9 | normal.z above which a surface is wet (0..1 over 31) |
| 10-15 | wetness scalar, 0..4 over 63, remapped to -2..2 in the shader; 0 is fully wet, 1 dry |
| 16-23 | raindrop scale, 0..10 over 255 |
| 24 | wetness variation (wet and less wet patches, streaks) |
| 25 | raindrop impacts on world surfaces (rings in world XY) |
| 26 | raindrops in texture space instead of world XY |
| 27 | raindrops sitting on the surface (vehicles, peds), object space |
| 28 | half raindrop density (drizzle) |
| 29 | reflective puddle layer on flat ground |
| 30 | rain shadow: an upward ray decides whether the point is under a roof |
| 31 | dithered rain shadow sample position (needs DLSS-RR) |

## Where the model is evaluated

Primary and PSR hits evaluate everything. Indirect bounces, shadow rays through alpha tested surfaces
and NEE cache samples take the base layer only (roughness scaling and floor, darkening): a bounce cannot
resolve puddle edges or raindrops.

## The rain map

With `rtx.wet.puddleQuality = 2` the runtime traces a top-down height field of the static geometry around
the camera every frame (`rtx_fork_rain_map.cpp`, two small compute passes): one downward ray per texel
that looks through moving objects and alpha tested surfaces and records the lowest static hit (the ground)
and the highest (cover, but only when it stands `occlusionMinHeight` above that ground, so a parked car or a
bench is never a roof), then per texel the depression (how much lower the ground is than the ground within
3 m, walls and bridges excluded), the slope, and the rain cover blurred with a tent filter over
`occlusionFeather`. The wetness model reads it with one fetch: the rain shadow is the blurred cover, so
cover edges are a smooth gradient, and water pools where the depression is positive: gutters, the dips of
intersections, potholes. Puddles only form on static surfaces, never on a moving car or a ped. A
sidewalk edge is higher than its surroundings, so a road puddle never climbs the curb, and the field is
continuous, so a puddle continues when the curb ends. The debug views "Fork: Rain Map" and "Fork: Puddles"
show both. Puddles exist where the map exists and fade out over its outer fifth; beyond it only the base layer
and the patches remain, since a per pixel rain shadow ray cannot be feathered.

## The layers

Base: roughness scaled by the game's wetness scalar and floored at `minRoughness`; albedo darkened and
saturated by porosity (rough, dark materials most). Patches (flag 0): wet and less wet areas with streaks.
Puddles (flag 5): a water level in world units against the ground height, which is a small unevenness
(`microAmplitude`, `noiseScale`) minus the rain map depression. The level rises with the game's wetness, so
puddles grow from the low spots outward and dry from the edges. Inside, the water surface replaces the
ground: flat normal (normal and height maps vanish, `normalDetail` too), wind ripples, `puddleRoughness`,
bottom darkened by depth; the damp shore band above the water line is darkened by `shoreDarkening`. Rain:
raindrop rings, full strength in puddles, `raindropGroundScale` on wet ground.

## Options

World units (GTA IV: meters). Remix menu: Game Setup -> Step 2: Parameter Tuning -> Global Wetness.

| Option | Default | Meaning |
| :-- | :-- | :-- |
| `rtx.wet.puddleQuality` | 2 | 0 off, 1 noise puddles and rain shadow by ray, 2 rain map. |
| `rtx.wet.mapExtent` | 128 | Side length of the rain map window around the camera. |
| `rtx.wet.mapResolution` | 512 | Texels per side (0.25 units at 128). |
| `rtx.wet.mapTraceAbove` / `mapTraceBelow` | 100 / 60 | Vertical trace range around the camera. |
| `rtx.wet.occlusionMinHeight` | 2.5 | Cover closer than this above the ground casts no rain shadow (cars, benches); the rain map applies it per texel against its own ground. |
| `rtx.wet.occlusionFeather` | 1.5 | Radius of the tent blur applied to the rain cover in the map (soft cover edges), capped at 12 texels. |
| `rtx.wet.occlusionRayLength` / `occlusionDistance` | 80 / 120 | Quality 1 and beyond the map: ray length and camera distance of the per pixel rain shadow ray. |
| `rtx.wet.detailDistance` | 300 | Puddles and patches are evaluated up to this camera distance. |
| `rtx.wet.raindropDistance` | 35 | Raindrop impacts are evaluated up to this camera distance. |
| `rtx.wet.minRoughness` | 0.05 | Roughness floor of wet surfaces outside puddles. |
| `rtx.wet.albedoDarkening` | 0.5 | Darkening of a fully wet porous surface. |
| `rtx.wet.saturationBoost` | 0.18 | Extra saturation of fully wet surfaces. |
| `rtx.wet.puddleCoverage` | 0.64 | Share of flat featureless ground under water when fully wet. |
| `rtx.wet.microAmplitude` | 0.03 | Height range of the small ground unevenness puddles form in. |
| `rtx.wet.noiseScale` | 1 | Cell size of that unevenness. |
| `rtx.wet.depressionWeight` | 0.3 | Weight of the rain map depression on the ground height. |
| `rtx.wet.slopeLimit` | 0.053 | Grade above which water runs off. |
| `rtx.wet.slopeFeather` | 2.4 | Width of the slope fade, as a multiple of `slopeLimit`. |
| `rtx.wet.edgeWidth` | 0.025 | Water depth over which the puddle edge fades in. |
| `rtx.wet.shoreWidth` | 0.02 | Height above the water line that is damp shore. |
| `rtx.wet.puddleOcclusion` | 0.5 | How much the rain shadow suppresses puddles. |
| `rtx.wet.puddleRoughness` | 0.02 | Roughness of standing water. |
| `rtx.wet.puddleDarkening` | 0.25 | Albedo multiplier under a puddle, reached at 2 cm depth. |
| `rtx.wet.shoreDarkening` | 0.8 | Albedo multiplier of the shore band. |
| `rtx.wet.rippleStrength` | 0.08 | Wind ripple normal strength; 0 disables. |
| `rtx.wet.rippleWavelength` | 12 | Shortest of six ripple wavelengths in cm; waves shorter than a few pixels at the hit distance fade out. |
| `rtx.wet.rippleVariation` | 0.48 | Gusts: how much the ripples come and go in patches. |
| `rtx.wet.rippleSpeed` | 2.5 | Ripple animation speed. |
| `rtx.wet.raindropGroundScale` | 0.5 | Raindrop strength outside puddles. |

## Cost

Rain map: 512 x 512 short downward ray queries against static geometry plus a 16 tap relief pass, once per
frame. Per primary hit on a wet surface: one map fetch, two to five value noise fetches, six trigonometric
terms inside puddles, and the raindrop grids within `raindropDistance`. Bounces: a few multiplies.
