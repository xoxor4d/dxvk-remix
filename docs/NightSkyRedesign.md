# Night Sky Redesign (Numos)

Design document. No code in this document has been implemented; nothing outside
this file was changed. Branch context: `rebased-9-19`, on top of the uncommitted
night-sky work described in the brief (blackbody tints, Lambert+surge phase,
Lambert terminator, halo/spikes, moon shadow softness, moon cloud shadowing).

Reference material used: Jensen, Durand, Stark, Premoze, Dorsey, Shirley,
"A Physically-Based Night Sky Model", SIGGRAPH 2001 ("the paper" below);
the halo_ref renderer's `atmosphere.hlsli` star field; the Bright Star
Catalogue as landed in `rtx_star_catalogue_data.h`; and the current shaders and
CPU code listed in the brief, all read in full.

---

## 0. Summary

The night sky becomes one radiometric chain from the sun's irradiance to the
radiance written per pixel, with exactly **one** non-physical number in the
illumination path: a night exposure gain, expressed in EV, standing in for the
scotopic adaptation the tonemapper cannot perform (its auto-exposure is capped
at 8x; reality needs ~400,000x between noon and full moon). Every other number
is either measured (catalogue magnitudes, lunar phase law, sky brightness in
mag/arcsec^2) or an explicitly labelled *appearance* knob that cannot leak into
illumination.

Concretely:

1. **Clouds glow because of two things, not one.** The blue uniform term the
   brief identifies (`nightSkyColor * starBrightness * ...`) is the visible
   symptom. The reason it was *needed* is `cloudMoonBrightness = 0.2` against
   `surfaceMoonBrightness = 50`: the cloud path receives 1/250 of the moonlight
   the ground receives, so moonlit clouds were near-black and a fake ambient
   was added to lift them. With one shared gain the moon lights clouds and
   ground in physical proportion, moonlit clouds come out several times
   brighter than the ground (as they are in reality), and the fake ambient has
   no job left. Section 6.
2. **Stars come from the catalogue**, spatially bucketed into a cube-face cell
   table with conservative binning, so a sky ray reads one cell (mean 1.3
   stars, worst ~25). Each star is a flux-conserving PSF in *angle* space, so
   total energy per star is independent of resolution, DLSS ratio and camera
   rotation. Constellations, clumping and the Milky Way's star density are
   real. Section 3.
3. **Lunar phase fits Krisciunas & Schaefer (1991)**, a published
   disk-integrated V-band law (full:quarter = 11.0), not the paper's
   Lommel-Seeliger (2.65) or Hapke (4.9) which both under-predict. The disk
   uses the Lommel-Seeliger *shape* (no limb darkening at full, correct
   terminator falloff) normalised so its integral is the K&S value. Disk and
   light therefore agree by construction. Section 5.
4. **The diffuse sky** (airglow, zodiacal light, unresolved stars, Milky Way
   glow, galactic/cosmic light) is a static celestial-frame dome texture
   (4 MB, baked once) plus airglow baked into the existing sky-view LUT. Its
   hemisphere mean is what lights clouds and the scene at night; discrete
   stars never reach diffuse rays, but their flux does, through the dome.
   Section 4.
5. **Glare** (Spencer et al. 1995) is one model shared by bright stars and
   moon disks, replacing the ad hoc Gaussian moon halo. Section 3.4 and 5.5.
6. **Migration retires 26 options** and adds 12, with `migrateValuesTo`
   declining every value whose meaning changed, so a saved `rtx.conf` gets a
   `[Deprecated Config]` log line rather than a silently re-scaled sky.
   Section 9.

Three findings in the brief are, I believe, wrong or misleading; they are
collected in Section 1 so they are not designed around.

---

## 1. Corrections to the brief

**1.1 The paper's "integrated starlight 3e-8 W/m^2" cannot be used as an
anchor.** The paper's own Eq. 6, `E = 10^(0.4(-m-19)) W/m^2`, gives Sirius
alone `9.6e-8 W/m^2` and Vega `2.4e-8`. Summing Eq. 6 over the 9096 catalogue
stars (script run for this document, Appendix B) gives `2.4e-6 W/m^2` for the
whole sky - 80x the paper's figure - and the catalogue is only the naked-eye
minority of integrated starlight (the sky-averaged total is ~3-4x higher; see
1.2). The Figure 2 table in the extracted text is also column-shifted by one
row. Whatever Roach & Gordon's `3e-8` measures, it is not the irradiance that
Eq. 6 defines, and the cloud coupling must not be anchored to it. The design
anchors starlight to the **catalogue sum** (exact for what is drawn) and the
diffuse sky to **measured sky surface brightness** (Section 2.2).

**1.2 "Moonless starlit sky ~1e-3 lux" is right, and it is not what the
paper's table adds up to.** A dark-site zenith sky of 21.5-22.0 V mag/arcsec^2
is `1.7e-4`-`2.7e-4 cd/m^2`, i.e. `5.4e-4`-`8.5e-4 lux` for a uniform
hemisphere - consistent with the brief. The paper's table sums to `~2e-7
W/m^2`, which is 20-50x too low at any plausible luminous efficacy. Use the
mag/arcsec^2 route.

**1.3 Full:quarter is nearer 11 than 8-9.** `0.25 / 0.03 = 8.3` is two rounded
figures divided. The Krisciunas & Schaefer (1991) phase law
`dm = 0.026|a| + 4e-9 a^4` (a in degrees) gives 2.60 mag at a = 90, ratio
10.96; Rougier's photometry gives ~12. Fit to K&S, not to the two lux figures.

**1.4 `composite.comp.slang` and `cloud_render.comp.slang` do not call
`evalSkyRadiance`.** They mention it in comments only. The callers are the two
sites in `geometry_resolver.slangh` (lines ~1939 and ~2574) and one in
`integrator_indirect.slangh` (~397); those are also the only two files that
include `atmosphere_sky.slangh`. This matters for plumbing: new star/dome
resources need the **common ray-tracing binding set only**. The cloud passes
need night data as constant-buffer scalars, not bindings, and the composite
needs nothing. Constraint 5's three-way redeclaration applies only if a
compute pass consumes the resource; in this design none does except the
one-time dome bake, which has its own pass-local slots.

**1.5 `rtx.atmosphere.moonN.angularRadius` is a diameter.** `populateMoonParams`
reads it into `angularDiamDeg` and the docstring says "angular diameter in
degrees"; the shader field `MoonParams::angularRadius` is then the half-angle.
The default 3.5 deg diameter is 6.75x the real Moon's 0.518 deg, i.e. **45.6x the
solid angle**, so the default moon delivers 45.6x the real Moon's irradiance at
the same surface radiance. This is the single biggest reason the moon/star
balance in tuned configs is off (Section 2.4). Keep the key (renaming would
retire saved values for nothing), fix the docstring and UI label.

**1.6 The current star profile is evaluated in cube-cell space, not angle.**
`starPos = neighbor + cellHash.xy - gridFrac` is in cell units and the Gaussian
is isotropic in that space. A cube-face tangent projection is anisotropic
towards face corners (up to ~1.7:1 in the two axes' angular scale), so stars
near cube corners are currently elliptical - the same defect halo_ref's
comment describes for its octahedral map, in milder form. Any new PSF must be
evaluated in angle (Section 3.4).

**1.7 The paper's Eq. 7 (+0.4 mag for atmospheric loss) double-counts.**
Standard V magnitudes are reduced to outside the atmosphere by convention. Use
Eq. 6 (photometric form, Section 2.2) and apply the renderer's own extinction
per view direction. Flagged as "I believe" in Section 11.

**1.8 The moon disk and the moon light already disagree by ~1.5x.** The disk
uses `computeMoonSharedFactor(m, args, surfaceColor)` with the procedural
`surfaceDetail` (clamped 0.3-1.15, maria darkened) while every light path uses
`m.color`. The visible disk's mean is ~0.65-0.7 of the light model's. Section
5.2 fixes this by normalising the procedural detail to unit mean.

---

## 2. Radiometric spine

### 2.1 The frame unit

The renderer's radiometric scale is set by the sun:
`args.sunIlluminance = sunIlluminance(15) * sunIntensity(1.09) = 16.35`
frame units of irradiance at the top of atmosphere. Pairing that with the
physical sun (1361 W/m^2, ~128,000 lux TOA) defines the conversions used
everywhere in this design:

| Quantity | 1 frame unit equals |
|---|---|
| Irradiance | 83.2 W/m^2, or 7,830 lux |
| Radiance | 7,830 cd/m^2 (nit) |
| `kFramePerLux` | `16.35 / 128000 = 1.277e-4` |
| `kFramePerWm2` | `16.35 / 1361 = 1.201e-2` |

These are derived from `args.sunIlluminance` at runtime, not hardcoded, so a
user who changes the sun's illuminance scales the whole night with it. Every
physical constant below is entered in physical units and converted through
these two factors in exactly one place (`nightRadiometry()` in
`atmosphere_common.slangh`, mirrored in `rtx_atmosphere.cpp`).

The distant-light convention, verified in `distant_light.slangh:88`
(`lightSample.radiance = radiance / sin^2(halfAngle)` with a uniform-cone pdf):
**`RtDistantLight::radiance` is `E / pi`**, the exit radiance of a white
Lambert surface facing the light. `syncDistantLights` already does
`radiance = E * radScale / kFhPi`. Keep it.

### 2.2 Calibration targets (physical -> frame, before any gain)

| Target | Physical | Source | Frame units |
|---|---|---|---|
| Sun, TOA irradiance | 1361 W/m^2, 128 klux | definition | 16.35 |
| Full Moon irradiance, zenith, clear, mean distance | 0.25 lux (0.31 TOA) | K&S m = -12.73 gives 0.31 lux TOA; x0.8 zenith transmittance | 3.2e-5 |
| Quarter Moon | 0.25/11 | K&S | 2.9e-6 |
| Star, m = 0 (Vega) | 2.54e-6 lux | standard zero point | 3.24e-10 |
| Sirius (m = -1.46) | 9.7e-6 lux | | 1.24e-9 |
| Faintest catalogue star (m = 6.5) | 6.4e-9 lux | | 8.1e-13 |
| All 9096 catalogue stars, whole sky | 96.0 Vega = 2.44e-4 lux | catalogue sum (App. B) | 3.1e-8 |
| Catalogue stars per hemisphere | 1.22e-4 lux | | 1.56e-8 |
| Total integrated starlight (incl. unresolved) | ~3-4x catalogue, sky-averaged | literature scale, unverified | ~5e-8 - 6e-8 |
| Dark-site zenith sky, all diffuse sources | 22.0 mag/arcsec^2 = 1.71e-4 nit | `S = 10.8e4 * 10^(-0.4 m)` | 2.2e-8 (radiance) |
| Typical dark site | 21.5 mag/arcsec^2 = 2.7e-4 nit | | 3.5e-8 |
| Moonless hemisphere illuminance | 5.4e-4 - 8.5e-4 lux | pi x zenith radiance | 6.9e-8 - 1.1e-7 |
| Sky under full Moon, 90 deg from Moon | ~18 mag/arcsec^2 = 6.8e-3 nit | K&S model | 8.7e-7 |
| Airglow share of dark sky | ~50 % | Roach & Gordon | 1.1e-8 |
| Zodiacal share | ~25-40 % (ecliptic-latitude dependent) | Leinert et al. 1998 | 5-9e-9 |
| Resolved starlight share | catalogue/hemisphere / pi | computed | 5e-9 |
| Earthshine on the Moon, full Earth | 0.095 W/m^2 vs 1905 sunlight = 5.0e-5 | paper Eq. 1 | ratio |

Two checks that the chain closes without fudge factors:

* Moon: disk radiance `L = E_sun * albedo / pi = 16.35 * 0.12 / pi = 0.625`;
  real solid angle `2 pi (1 - cos 0.259 deg) = 6.42e-5 sr`; irradiance
  `4.0e-5` frame = 0.31 lux TOA = K&S's m = -12.73 to 2 %. A flat Lambert disk
  at geometric albedo 0.12 *is* the full Moon; the opposition surge is already
  inside the definition of geometric albedo. It is the departure from full that
  a Lambert sphere gets wrong (Section 5.1).
* Stars vs sky: catalogue stars per hemisphere `1.56e-8` frame irradiance,
  i.e. `5e-9` frame of equivalent uniform radiance - 23 % of a 22.0
  mag/arcsec^2 sky. Right order for "resolved stars are a minority of the
  glow".

### 2.3 The night exposure gain

**Problem.** Physically, a moonlit 0.2-albedo ground is `0.2 * 3.2e-5 / pi =
2e-6` frame; a sunlit one is 1.04. The tonemapper's auto exposure
(`rtx.autoExposure.maxExposure = 8`) cannot bridge 5e5:1, and the game's own
night light sources (lamps, fires) are authored at the game's scale, so a true
scotopic exposure would blow them out. Today this gap is filled by per-path
gains that disagree with each other: `surfaceMoonBrightness 50`,
`cloudMoonBrightness 0.2`, `haloMoonBrightness 15`, `starBrightness 12`,
`starAmbientCouplingStrength 0.25`, `nightSkyBrightness 0.002` and the
internal `kStarCloudCoupling 0.008`, `kMoonAirglowScale 0.0015`.

**Design.** One gain, `rtx.atmosphere.nightExposureEv` (float, EV stops,
default **5.6**, so `G = 2^5.6 = 48.5`), applied at the *source* of every
night illuminant so all ratios between them stay physical:

```
G_illum(args) = 2^nightExposureEv * adaptation(sunDirection.y)
adaptation(y) = smoothstep(sin(-5 deg), sin(-13 deg), -y)   // 0 by day, 1 at night
G_night(args) = lerp(1, G_illum, adaptation)                // never below 1
```

* By day `G_night = 1`: stars at physical brightness are drowned by the LUT
  sky (Vega's peak `1.7e-4` vs sky ~1), and a daytime moon adds 0.1 % to
  daylight. No visibility hacks are needed in either direction.
* The ramp is the mesopic range (zenith sky ~3 cd/m^2 at civil twilight to
  ~0.01 at nautical), so it is the one perceptual curve in the design, and it
  **replaces the four disagreeing twilight curves** now in the code:
  `nightFactor = smoothstep(0.02, -0.05, y)` (clouds), `atmosphereFade =
  smoothstep(-0.09, 0, y)` (LUT kill), `smoothstep(0.02, 0.15, -y)` (stars),
  `moonFade = smoothstep(0, -0.09, y)`.
* Default 5.6 EV reproduces the shipped ground level: today's default
  `surfaceMoonBrightness = 50` on the default 3.5 deg moon. With a real-size
  0.52 deg moon the equivalent is **+11.1 EV**; the UI should offer both as
  presets ("Cinematic moon", "Physical night").

What G multiplies: the moon irradiance on ground (distant light), clouds and
atmosphere (LUT moon term); star irradiance; airglow, dome and their means.
What it does **not** multiply: the sun-lit sky, the sun, game lights.

### 2.4 The one appearance/illumination split: the moon disk

If the disk were also scaled by G it would sit at `0.625 * 48 = 30` frame
units and clip to featureless white, as it does in every night photograph. The
eye sees maria because of local adaptation the tonemapper does not have. So:

* Disk *appearance* radiance uses `G_disk = 2^moonDiskExposureEv`, default 0
  EV (physical 0.625: 20-35x brighter than the brightest moonlit cloud, which
  reads as a bright moon with visible detail).
* Disk *illumination* (everything the moon lights) uses `G_night`.
* Glare around the disk (Section 5.5) is computed from the **displayed** disk
  flux, because glare is an optical effect on what reaches the eye; otherwise
  the halo/disk ratio would be exaggerated by `G_night / G_disk`.

This is the only place appearance and illumination diverge, and it is
labelled as such in the option docstring.

### 2.5 Compositing order per sky ray

This is the paper's Section 5.1 "alpha image" made explicit:

```
T_view      = atmospheric transmittance along the view ray (transmittance LUT tap)
L_in        = skyViewLut(dir)                 // sun-sky + moon-sky + airglow, see 4.2
L_extra     = dome(R dir) * G_night           // 4.1; R = sidereal rotation
            + stars(R dir) * G_night          // 3; primary-class rays only
cov_i, L_i  = moon disk i coverage and appearance radiance (5.2)

L_behind    = (1 - sum cov_i) * L_extra + sum cov_i * L_i
radiance    = L_in + T_view * L_behind + glare(dir)
```

Differences from today: the disk **adds** onto the atmosphere's in-scatter
(it is behind the air, so a daytime moon is correctly veiled by blue sky
instead of replacing it); stars and dome are attenuated by the real
`T_view` (reddening near the horizon for free) instead of
`smoothstep(-0.02, 0.1, elevation)`; there is no `atmosphereFade`,
`nightFactor` or `moonFade` here at all. Clouds composite over this later
exactly as today (composite.comp.slang for primary, the secondary dome LUT for
the rest), with `T_cloud^1`, not `T_cloud^2.5` (Section 9, `starCloudExtinctionPower`).

### 2.6 Knob taxonomy

| Class | Rule | Examples |
|---|---|---|
| Physical | in physical units or a dimensionless multiple of a measured value, default = measured | moon albedo, angular diameter, phase, `airglowScale` (1 = 22.0 mag/arcsec^2 sky), `starMagnitudeLimit` |
| Labelled artistic, illumination | may change how much light there is; exactly one exists | `nightExposureEv` |
| Labelled artistic, appearance | cannot change illumination anywhere | `moonDiskExposureEv`, `starGain`, `starSpikeStrength`, `glareStrength`, `starPsfWidthPx` |
| Perceptual model | fixed constants, documented, not exposed | adaptation ramp, Spencer PSF weights |

`starGain` deserves a sentence: it scales discrete star radiance only, never
the dome or any mean. Its justification is that a fixed-exposure display
under-represents point sources relative to the eye (no scotopic gain, no
adaptation), and a user who wants a 3.5 deg moon *and* prominent stars needs
it. It cannot inflate cloud lighting because clouds never see discrete stars.

---

## 3. Stars

### 3.1 Representation

The catalogue (`kStarCatalogue[9096]`, 6 bytes each) is compile-time data.
At `RtxAtmosphere::initialize` the CPU builds two GPU buffers once
(`buildStarCatalogueBuffers()`, new, `rtx_atmosphere.cpp`):

```
struct StarEntry {            // 16 bytes
  float3 dir;                 // unit vector, celestial frame: x = cos d cos a, y = sin d, z = cos d sin a
  uint   packed;              // r:8 g:8 b:8 (chroma, Y-normalised, x255) | v:8 (catalogue byte, m = v/25 - 2)
};
StructuredBuffer<StarEntry> StarEntries;      // ~18-20k entries after conservative binning
StructuredBuffer<uint>      StarCellOffsets;  // 6*N*N + 1 prefix sums, N = 64
```

The celestial frame is the existing one: `+Y` is the celestial pole, so
`starAxisElevation = 90` puts the pole at zenith as before and `starRotation`
rotates about it with the existing Rodrigues code in `evalStarField`. RA zero
is arbitrary in-game; document that `starRotation = 0` puts RA 0h on the +X
horizon axis.

Colour is resolved on the CPU per entry from the `bv` byte (Section 3.3),
so the shader does no colour math. Scintillation seed is a hash of the entry
index. Nothing in the buffers depends on any option, so they never rebuild at
runtime; `starMagnitudeLimit` is a shader-side compare on the `v` byte.

### 3.2 Spatial query

Cube-face cells, `N = 64` per face (24,576 cells, 0.6-1.8 deg across). Measured
on the catalogue (Appendix B): mean occupancy 0.37, p99 = 3, worst cell 11
(Pleiades/Cygnus). A 3x3 gather with no duplication would read up to 31 stars
in the worst neighbourhood and 9 offset words per ray; instead use
**conservative binning**: at build time each star is written into every cell
whose area lies within `kStarBinMarginDeg = 0.25 deg` of the star (its own cell
plus up to three neighbours; face edges handled by re-projecting the margin
disc onto the adjacent face). A ray then reads **one** cell:

```
c   = starCell(R dir)                         // face + (i, j), ~20 ALU
for k in [StarCellOffsets[c], StarCellOffsets[c+1]):
  s = StarEntries[k]
  cosT = dot(dir, s.dir); if cosT < cosMargin continue
  theta = acos-free small-angle: sqrt(2 - 2 cosT)   // radians, exact enough below 1 deg
  L += E(s) * psfCore(theta) * rgb(s) * scint(s)
```

Expected reads per ray: `0.37 * (1 + 0.77 duplication) = 0.66` entries plus one
cell fetch; worst ~25. Entry count ~16-20k, 260-320 KB; offsets 98 KB.
Because the margin is fixed in angle and the PSF core+skirt is windowed to
zero at `kStarBinMarginDeg` (Section 3.4), the profile-support-versus-gather
constraint the brief calls out is satisfied structurally: a star can never be
summed by a pixel that did not fetch it, at any resolution. Anything wider
than the margin belongs to the bright list.

**Bright list.** The 32 brightest stars (m <= 1.76, 20 % of catalogue flux)
are also stored in a fixed `float4 brightStars[32]` block (dir + E) in
`AtmosphereArgs` - 32 rows, 512 bytes - or in the entries buffer with a
known offset. Every primary-class sky ray loops over them for the wide glare
(Section 3.4). Cost 32 dots and compares, ~100-150 ALU, almost all rays miss.
The moon disks join the same glare loop (5.5).

Why not the halo_ref approach (a painted tile with its own tangent plane)?
It solves the anisotropy problem and reads as a sky, but it cannot show
constellations, cannot follow the sidereal rotation with real content, and a
512^2 tile at ~5 px/texel is why halo_ref tiles it. The catalogue gives the
clumping for free and the cost is comparable.

### 3.3 Magnitude and colour

Irradiance from magnitude (photometric zero point, not the paper's Eq. 6/7):

```
E_frame(m) = kFramePerLux * 2.54e-6 * 10^(-0.4 m) = 3.24e-10 * 10^(-0.4 m)   (at default sun)
```

Temperature from B-V: **Ballesteros (2012)**, which fits Vega to ~10,100 K
where the paper's Eq. 8 (`7000 K / (B-V + 0.56)`) gives 12,500 K:

```
T = 4600 K * ( 1/(0.92 (B-V) + 1.7) + 1/(0.92 (B-V) + 0.62) )
```

Sun (B-V 0.65) -> 5778 K; Vega (0.00) -> 10,100 K; Betelgeuse (1.85) -> 3,500 K;
the catalogue's B-V range -0.28..5.75 -> 15,000 K..1,900 K (clamp at 1,667 K,
the limit of the Planckian-locus fit).

T -> linear sRGB: Planckian locus in CIE xy (Kang et al. 2002 fit, 1667-25000
K), Y = 1, then XYZ -> linear sRGB with D65 white, clamp negatives to 0,
store as 8-bit chroma normalised so `dot(rgb, Y709) = 1`. Done once on the
CPU into a 256-entry table indexed by the `bv` byte, then written per entry.
Because the star's V magnitude *is* its luminance, `E * rgb` has luminance
`E` exactly and the colour is a pure chromaticity - no double counting of
brightness through a "temperature tint" as `starTint(x) * brightness` does
today.

Optional, appearance-only: `starFaintDesaturation` (default 0) pulls colour
toward white for faint stars, modelling rod vision. Off by default because
the paper's Figure 10 argument (a camera sees the colours) is the more useful
default for a game, and the knob is trivial to add later.

### 3.4 Point-spread and glare

Two layers, one normalisation rule: **every profile integrates to 1 over
solid angle**, so a star deposits exactly `E` into the image regardless of
resolution, DLSS ratio or sub-pixel phase.

**Core** (all stars): isotropic Gaussian in angle,

```
sigma_rad = starPsfWidthPx * pixelAngleRad          // pixelAngleRad pushed CPU-side
psfCore(theta) = exp(-theta^2 / (2 sigma^2)) / (2 pi sigma^2)
```

`starPsfWidthPx` default 0.7. With `sigma = 0.7 px`, the sum of a Gaussian
sampled at pixel centres has relative ripple `~2 exp(-2 pi^2 sigma^2) = 0.01
%` as the centre crosses pixel boundaries, which is the anti-aliasing
guarantee under camera rotation. `pixelAngleRad = 2 tan(fovY/2) / renderHeight`
is computed in `getAtmosphereArgs` from the camera and the downscaled render
extent, rides the `padStarAppearance0` slot, and is zeroed in
`normalizeForSkyLutCache` (it feeds no bake; it must not re-bake on FOV
change).

**Skirt and glare** (Spencer, Shirley, Zimmerman, Greenberg 1995, cited by
the paper as its star flare model). The eye's PSF is a weighted sum,
theta in degrees, each `f_i` normalised to unit integral:

```
f0 = 2.61e6 exp(-(theta/0.02)^2)          // core, sigma ~0.014 deg ~ 0.4 px at 28 px/deg
f1 = 20.91 / (theta + 0.02)^3             // the "glow"
f2 = 72.37 / (theta + 0.02)^2             // broad veil
photopic  P = 0.384 f0 + 0.478 f1 + 0.138 f2
scotopic  P = 0.282 f0 + 0.478 f1 + 0.207 f2 + 0.033 f3(theta, lambda)   // f3 = lenticular halo ring at ~3 deg
```

Coefficients as I recall them; **verify against the paper before hardcoding**
(Section 11). The structural facts the design relies on are robust: about
40 % of a point source's energy is in a sub-pixel core and about 60 % is in
`1/theta^3` and `1/theta^2` skirts spread over degrees. For a faint star that
60 % is invisible (spread too thin); for Vega it is the glow.

Implementation: the core Gaussian above stands in for `f0` (it must match the
render pixel, not 0.02 deg). The skirt `0.478 f1 + 0.138 f2` is evaluated for
every star out to the bin margin (0.25 deg) with a smooth window to zero at
the margin, and the CPU precomputes the fraction of `f1 + f2` energy inside
0.25 deg so the drawn total still sums to `E` (the remainder is deliberately
discarded: it is below any display threshold for m > 2). For the bright list
the skirt continues to `kGlareRadiusDeg = 3 deg` without the window. One
`glareStrength` (appearance, default 1 = Spencer photopic weights) scales the
skirt fraction for stars and moons alike; 0 gives bare Gaussian cores.

**Spikes.** Navarro & Losada (1997) find the eye's star image has irregular
radial streaks from lens sutures and aberrations, not a four-fold camera
cross; a symmetric cross reads "photographic". Keep the four-fold spike as an
appearance knob `starSpikeStrength`, default lowered to 0.05, with a
per-star rotation (already done), evaluated only for the bright list (it is
invisible on faint stars and the `atan2` is then paid 32 times per ray, not
per catalogue star).

Magnitude-to-size falls out: a brighter star's skirt exceeds the visible
threshold farther out, so it *is* bigger. `starSizeMagnitudeScale` retires.

### 3.5 Atmospheric extinction

Stars and dome are multiplied by `T_view` from the transmittance LUT at the
camera altitude and the view zenith angle (one tap, already bound as
`AtmosphereTransmittanceLut`). Rayleigh + Mie + ozone are in the LUT, so a
star at 10 deg elevation loses ~1.5 mag and reddens, and below the horizon
`T_view -> 0` removes the need for a horizon smoothstep. Twilight visibility
is then a contest between `E * psf * G_night` and the LUT sky, with no star
fade curve.

### 3.6 Scintillation

Empirical, labelled: intensity fluctuation amplitude grows with air mass
(Dravins et al. 1997 find variance roughly `~ (sec z)^1.5-2` for small
apertures):

```
X   = airMass(zenith)              // Kasten-Young, already in getAtmosphericTransmittanceForDir
amp = clamp(0.12 * X^0.9, 0, 0.6) * starScintillation
twinkle = 1 + amp * (0.6 sin(w1 t + p1) + 0.4 sin(w2 t + p2)),  w in 0.5..3 Hz per star
```

Frequencies are kept at or below ~3 Hz on purpose: DLSS's temporal
accumulation averages faster flicker into a dimmer star. Chromatic
scintillation (low stars flashing colours) is a possible later addition:
shift the per-channel phases by a fraction proportional to `X`.
`starScintillation` (physical-ish, default 1 = typical seeing) replaces
`starTwinkleSpeed`.

### 3.7 Anti-aliasing and temporal stability

* **Camera rotation**: flux-conserving Gaussian, sigma >= 0.7 render pixels
  (3.4). Ripple 0.01 %.
* **DLSS**: sub-pixel jitter moves the star centre relative to pixel centres
  each frame; per-pixel values change but the sum does not, so accumulation
  converges to the same star. Risk: DLSS history rejection on isolated
  high-contrast points can sparkle or dim them. Mitigations in order: raise
  `starPsfWidthPx` to 0.9; lower `starGain`; last resort, move discrete stars
  to a composite-time splat at output resolution gated by an edge-aware
  upsample of the sky mask and cloud transmittance (constraint 7). Not
  recommended initially: it re-implements sky visibility outside the tracer
  and PSR reflections of stars would lose them.
* **Glossy reflections**: `PathState.coneSpreadAngle` exists in
  `path_state.slangh:83` but is marked unused. If it carries a value at the
  sky-miss site, widen the star: `sigma_eff^2 = sigma^2 + coneSpread^2` with
  the same `E`, which anti-aliases stars in rough reflections automatically.
  If it does not, fall back to gating discrete stars on
  `applySkyIndirectRadianceScale` only (as now) and accept that very rough
  specular misses may sparkle. Open question 11.4.
* **Resolution independence**: `E` per star is invariant; only sigma tracks
  the render pixel, so a 4K/DLAA and a 1080p/Performance frame show the same
  star energy.

### 3.8 Ray classes

| Ray class (flags at `evalSkyRadiance`) | LUT | Dome | Discrete stars | Bright glare | Moon disk | Moon glare |
|---|---|---|---|---|---|---|
| Primary (`isPrimaryRay`) | yes | yes | yes | yes | yes | yes |
| PSR / specular / refraction / cutout miss | yes | yes | yes | yes | yes | yes |
| Diffuse indirect (`applySkyIndirectRadianceScale`) | yes | yes | **no** | **no** | **no** (new) | **no** |
| Cloud march, secondary cloud LUT | LUT ambient tap + dome mean scalar | | no | no | no | no |

Excluding moon *disks* from diffuse gathers is new: the disk at 0.625 frame
is a firefly against a 1e-3 night, and the moon's light already reaches
surfaces through its `RtDistantLight` - counting the disk again in the gather
double-counts exactly as the sun disk would (the sky-view LUT carries no sun
disk for the same reason). Discrete-star energy reaches diffuse rays
statistically through the dome (Section 4.1).

### 3.9 Cost

Per primary-class sky ray: rotation 9 MAD; cell index ~20; one offset pair
load; ~0.7 entries x ~30 ALU; bright list ~130 ALU; transmittance LUT tap 1;
dome tap 1. Roughly **250-350 ALU, 3-4 memory ops**. The current field costs
nine `hash33` (~180 ALU), up to nine profiles, and when the Milky Way is on
three `fbmNoise3D` (3+5+4 octaves) plus a `perlinNoise3D` - about 13 noise
evaluations, 500+ ALU. The redesign is at worst cost-neutral and cheaper with
the Milky Way on. At 1440p DLSS Quality (1707x960 render, ~40 % sky) that is
~0.65 M primary sky rays; estimate **<= 0.1 ms** on a 4070-class GPU, to be
measured with the existing `debugSkyBisectFlags` bit 1 (flat sky miss) A/B.

---

## 4. The diffuse sky: Milky Way, zodiacal light, airglow, galactic light

### 4.1 Night dome texture (celestial frame, static)

One `RGBA16F 1024x512` equirectangular texture in celestial coordinates
(RA along u, Dec along v), 4 MB, sampled once per sky ray after the sidereal
rotation. Angular resolution 0.35 deg at the equator, enough for Milky Way
dust lanes at the 1 deg scale the eye resolves; anything finer is below the
dark-sky contrast a game display shows. Stored **pre-gain, scaled by a fixed
`kNightDomeEncode = 2^24`** so physical radiances of 1e-8..1e-6 frame land in
float16's normal range (otherwise they are subnormal and band).

Content, all in physical frame radiance, summed:

1. **Galactic glow.** Default: procedural, driven by the catalogue's own
   density. The catalogue's star density is 1100 /sr at |b| < 10 deg against
   ~500 /sr at the poles (Appendix B), so a 5 deg-smoothed splat of the
   catalogue stars *already traces the band's shape*, including Cygnus,
   Sagittarius, Carina and the Coalsack gap. Add a Gaussian disk in galactic
   latitude (sigma ~6 deg, brighter toward the galactic centre at
   RA 266.4 deg, Dec -28.9 deg; galactic north pole RA 192.86 deg, Dec +27.13
   deg, J2000), plus 3D FBM dust lanes as multiplicative extinction
   (`milkyWayDustAmount` retained as an appearance knob for this). Calibrated
   so the whole-sky mean of (galactic + unresolved starlight) equals the
   unresolved-starlight budget: `(3 to 4 - 1) x` the catalogue total, i.e.
   ~3e-8 frame irradiance per hemisphere (labelled approximate).
   Option: a photographic mosaic. Mellinger's panorama (the paper's) is
   copyrighted; ESO's Milky Way panorama is CC BY 4.0, which an MIT repo can
   carry with attribution (unlike the CC BY-SA HYG data the catalogue header
   rejects). Offer as an opt-in asset path `milkyWayTexture`; when present it
   replaces the procedural term after the same mean calibration.
2. **Zodiacal light.** Ecliptic-fixed band (ecliptic pole at RA 270 deg, Dec
   +66.56 deg). Brightness as a function of ecliptic latitude only, from
   Leinert et al. 1998 at 90 deg elongation: ~180 S10 at latitude 0 falling to
   ~65 S10 at the pole (1 S10 = 8.2e-9 W/m^2/sr = 9.8e-11 frame). The
   elongation dependence (evening/morning cones, Gegenschein) is dropped: it
   needs the sun's ecliptic longitude, and the strongly sun-dependent parts
   are within ~40 deg of the sun, below the horizon in deep night. Labelled
   approximation; a rebake keyed on the sun's celestial longitude quantised
   to 5 deg is the upgrade path.
3. **Diffuse galactic + cosmic light.** Constant `~1e-8 W/m^2` hemisphere
   (paper 6.3) -> 4e-11 frame radiance. Included for completeness; invisible.

Baked once at init by a new compute pass `night_dome_bake.comp.slang`
(pass-local bindings only, as `cloud_placement_map` does), re-baked when
`milkyWayScale`, `milkyWayDustAmount`, `zodiacalScale` or the texture path
change - a "rare rebake" keyed on those four, never on time or sun. The bake
also writes back (via a small readback or a CPU-side mirror of the same
formulas) the dome's **whole-sky mean radiance**, `nightDomeMeanRadiance`,
which is pushed into `AtmosphereArgs` for the cloud and volumetric ambient.
The CPU mirror is preferred (no GPU readback latency); the formulas are
simple enough to evaluate on a 64x32 grid.

Why not put the dome in the sky-view LUT? The LUT is keyed on quantised
sun/moon direction and would need the sidereal angle in its key (a re-bake
every 0.1 deg of sky rotation - the same cadence as a moving sun, so not
worse, but coupling two unrelated invalidation reasons), and its 512x256
warped parameterisation gives ~1.4 deg resolution at the zenith and an
azimuth singularity there (the pinwheel already noted for the cloud bleed).
A static celestial texture has neither problem and costs one tap.

### 4.2 Airglow in the sky-view LUT

Airglow is local-frame (elevation only), so it belongs with the atmosphere.
In `sky_view_lut.comp.slang`, after `evalAtmosphereRadiance`:

```
L_ag(z) = airglowScale * L_ag0 * vanRhijn(z) * T_layer(z)
vanRhijn(z) = 1 / sqrt(1 - (R / (R + 90 km))^2 sin^2 z)      // 1.0 zenith, 1.9 at z=60, 4.2 at 80, 6.0 at horizon
T_layer(z)  = transmittance from the 90 km layer to the ground along z (transmittance LUT)
L_ag0       = 0.5 * 22.0 mag/arcsec^2 = 1.1e-8 frame, colour ~ (1.0, 0.83, 0.70) linear
```

The colour: the dark night sky measures B-V ~ +0.8, V-R ~ +0.9 - it is
*redder than the sun* (OI 557.7 and 630 nm lines plus a red continuum). The
"blue night" impression is the Purkinje shift of scotopic vision, which the
paper implements as a *tone-mapping* blue shift, not as blue light. This
design keeps the light physical and recommends a night-gated grade in the
post-processing stack for the Purkinje look. A `nightSkyTint` appearance knob
(default white) on the diffuse floor only is acceptable if demanded, because
the floor's physical magnitude bounds any tint's effect on clouds to
~1e-6 frame; it is not in the staged plan.

Consequences:

* `sampleSkyAmbientForVolume` and the cloud ambient taps get the airglow
  floor for free; the volumetric fog at night stops being pitch black.
* The deep-night `atmosphereFade` hack in `evalSkyRadiance` moves *into the
  bake*, applied to the **sun term only**, over -8 deg..-18 deg (astronomical
  twilight) instead of 0..-5 deg. Its job was hiding multiscatter noise in
  the sun term; it must not zero the moon or airglow terms it now shares the
  LUT with. Risk 11.6.
* **The sky-view LUT must become `R32G32B32A32_SFLOAT`** (rtx_atmosphere.cpp
  ~1599, currently `R16G16B16A16_SFLOAT`; 512x256x16 B = 2 MB). Night values
  after gain are 1e-6..2e-3 frame; float16's normal range starts at 6.1e-5
  and subnormal precision is 6e-8, which bands the van Rhijn gradient at low
  gains. The moon term at default gain sits just above the edge; the airglow
  does not. Also the multiscattering LUT (32x32) if the moon term is folded
  through it; the transmittance LUT is [0,1] and unaffected.

### 4.3 From dome to ambient

`E_night_ambient = pi * (nightDomeMeanRadiance + LUT hemisphere terms)`. The
Nubis evaluator wants a *radiance* for its ambient shape term, so the cloud
context gets `nightAmbientRadiance = nightDomeMeanRadiance * G_night` added to
the existing LUT taps (which now carry moon-sky and airglow, ungated). No
slider anywhere in that sum. Section 6.

### 4.4 Affordability

Dome: 4 MB, one tap per sky ray, one-time bake. Airglow: ~30 ALU per LUT
texel at bake time, zero per ray. LUT format change: +1 MB. Everything here
is cheaper than the three FBM evaluations per ray it replaces.

---

## 5. Moons

### 5.1 Phase law

Disk-integrated illumination, normalised to 1 at full, from Krisciunas &
Schaefer (1991, PASP 103, 1033), itself a fit to Rougier's lunar photometry
and the law observatory sky-brightness models use:

```
alpha_deg = 180 * |1 - 2 phase|                         // 0 full, 180 new
dm(alpha) = 0.026 alpha + 4e-9 alpha^4                  // magnitudes fainter than full
KS(alpha) = 10^(-0.4 dm)
```

Comparison at the phase angles that matter (full = 1):

| alpha | 30 deg | 60 deg | 90 deg (quarter) | 120 deg | 150 deg |
|---|---|---|---|---|---|
| `0.5 - 0.5 cos` (old, lit area) | 0.933 | 0.750 | 0.500 | 0.250 | 0.067 |
| Lambert sphere + exp surge (stopgap) | 0.63 | 0.36 | 0.168 | 0.06 | 0.011 |
| Lommel-Seeliger `Phi_LS` (paper Eq. 5) | 0.77 | 0.58 | 0.377 | 0.20 | 0.058 |
| Hapke `B(alpha) S(alpha)`, g = 0.6, t = 0.1 (paper Eq. 2-4) | ~0.55 | ~0.35 | 0.204 | ~0.10 | ~0.03 |
| **Krisciunas & Schaefer 1991** | **0.487** | **0.233** | **0.0912** | **0.0288** | **0.0043** |
| Observed (brief) | | | 0.11-0.125 | | |

Full:quarter 11.0. The crescent tail at 150 deg is 1/234, between the
stopgap's 1/90 and the paper's L-S 1/17. K&S is a fit to ~150 deg; beyond
that `Phi_LS` scaled to be continuous at 150 deg is used (both go to zero at
new moon). One function, `moonPhaseIllumination(phase)` in
`atmosphere_common.slangh`, mirrored as `fhMoonPhaseIllumination` in
`rtx_atmosphere.cpp`, replacing the Lambert+surge bodies in both. All four
consumers already call it.

### 5.2 Disk appearance

Lommel-Seeliger *shape*, normalised to the K&S *total*. L-S is exactly
limb-flat at full (`cos i / (cos i + cos e) = 1/2` everywhere when i = e),
which is the observed property the paper cites, and it falls to zero linearly
at the terminator instead of the old mask's hard edge:

```
n      = sphere normal at disk point (as in getPhaseIllumination today)
cos_e  = n.z                             // toward viewer
cos_i  = max(0, dot(n, sunDirInDiskFrame))
shape  = 2 cos_i / (cos_i + cos_e + eps)                 // = 1 at full, everywhere
Phi_LS(alpha) = 1 - sin(alpha/2) tan(alpha/2) ln(cot(alpha/4))   // paper Eq. 5 bracket, = disk integral of shape/2... normalised to 1 at full
L_disk(p) = L_mean_full * detail(p) * shape * KS(alpha) / Phi_LS(alpha) * G_disk
L_mean_full = args.sunIlluminance * m.color * m.brightness * T_atm(moonDir) / pi
```

`KS / Phi_LS` is the factor by which the real regolith is darker than
Lommel-Seeliger at that phase (0.24 at quarter); it is applied uniformly, so
the disk integral equals `L_mean_full * KS(alpha) * Omega` exactly - the same
quantity `moonIrradiance()` uses (5.4). The `limbDarkening = 1 - 0.3 n^2`
term in `evalMoonDisk` is removed: L-S already contains the correct (nil)
limb behaviour at full and the correct terminator behaviour elsewhere.

`detail(p)`: the procedural rocky/volcanic surfaces stay, but
`surfaceDetail * surfaceColor / m.color` is normalised to unit mean over the
disk. The mean is a constant per `surfaceStyle` (and per `craterDensity`,
`surfaceContrast` linearly), measured once with a debug integration and
stored as `kRockyMeanAlbedoFactor`, `kVolcanicMeanAlbedoFactor`. This closes
the 1.5x disk/light disagreement in 1.8.

Default `m.color`: the Moon's B-V is 0.92 against the sun's 0.65, so
moonlight is slightly warm. Proposed physical default `(0.132, 0.120, 0.105)`
(geometric albedo 0.12 in V, tilted by the +0.27 colour excess); a default
change only, saved values are honoured. Blue moonlight is, again, Purkinje.

Edge: keep the 3 %-wide `smoothstep` anti-aliasing of the rim but express it
in pixels (`pixelAngleRad`) so a 0.5 deg moon at 1080p does not get a blurrier
rim than a 3.5 deg one.

### 5.3 Earthshine

Paper Eq. 1, with `phi = pi - alpha` the Earth's phase as seen from the Moon:

```
E_earth / E_sun_at_moon = (0.095 / 1905) * Phi_LS(phi) = 5.0e-5 * Phi_LS(pi - alpha)
darkRadiance = L_mean_full * detail(p) * earthshine * 5.0e-5 * Phi_LS(pi - alpha) * (L-S shape lit from the Earth, i.e. from the viewer: cos_i = cos_e -> shape = 1)
```

`rtx.atmosphere.moonN.earthshine` (physical, 1.0 = an Earth-like planet with
Bond albedo 0.3; larger planets or moons of gas giants use more). Replaces
`darkSideBrightness` (default 0.005 = 100x the physical ratio). At the
default 3.5 deg moon and `G_disk = 1`, a new-moon dark side is `0.625 * 5e-5
= 3e-5` frame - invisible under an 8x exposure, as it should be against a
moonlit or twilight sky, and visible in a thin crescent if the user raises
`moonDiskExposureEv`, which is when the eye sees it too.

### 5.4 Emitted light and the consistency contract

One shader function and one C++ mirror produce the moon's irradiance:

```
Omega          = 2 pi (1 - cos m.angularRadius)
moonIrradiance = L_mean_full * KS(alpha) * Omega * G_night     // frame units, at the ground
```

Consumers, all of which must use it and nothing else:

| Consumer | File / function | Uses |
|---|---|---|
| Distant light | `rtx_atmosphere.cpp: syncDistantLights` | `radiance = moonIrradiance * radScale / pi`; half-angle from `moonShadowSoftnessDeg` or `m.angularRadius`; `atmosphereCloudShadowed = moonCloudShadowed` |
| Atmosphere (moon-sky in LUT) | `atmosphere_common.slangh: evalAtmosphereRadiance` combined-moon block | `combinedMoonIrradiance += moonIrradiance * moonAtmosphericCouplingStrength` |
| Cloud direct term | `cloud_march_common.slangh: buildCloudShadeContext` | `moonIrradiance` as the light's irradiance (6.2) |
| Disk appearance | `atmosphere_sky.slangh: evalMoonDisk` | `L_disk` above; integrates to `moonIrradiance / G_night * G_disk` |
| Glare | `atmosphere_sky.slangh: evalGlare` | flux `L_mean_full * KS * Omega * G_disk` |
| UI probe | `rtx_atmosphere_ui.cpp` | prints `moonIrradiance` in frame units and lux-equivalent |

The `nightFactor` gate in `syncDistantLights` and `buildCloudShadeContext`
goes; `G_night`'s adaptation ramp is the only twilight dependence, and the
light stays alive by day at physical strength (1/400,000 of the sun for a
real-size moon; RTXDI will essentially never sample it, so the cost is one
light-list entry).

### 5.5 Halo: glare, not a Gaussian

The visible glow around a moon is (a) the Mie/Rayleigh aureole, which the
LUT's moon single-scatter term already produces with the HG phase at
`mieAnisotropy` (coarsely - 1.4 deg texels at the zenith), and (b) glare in
the eye/camera. (b) is the same Spencer skirt as the bright stars, applied to
the disk's total displayed flux as a point source for `theta > angularRadius`
(outside the disk the difference between a disk and a point convolution is
negligible past ~1.5 radii). `moonHaloMagnitude`, `haloMoonBrightness`,
`moonHaloGlowStrength` retire; `glareStrength` is shared.

### 5.6 Multiple moons

Nothing changes structurally: each moon has its own direction, phase, size,
albedo and earthshine; irradiances add; the distant lights are per moon; the
LUT and cloud march keep the brightness-weighted combined direction
(Lever 4). Moons are not lit by other moons, nor do they eclipse each other or
the sun (labelled omissions; the disk composite's array order still
determines overlap).

### 5.7 Shadows

`moonShadowSoftnessDeg` (0 = physical half-angle) and `moonCloudShadowed`
stay exactly as landed. The additional shadow feature is in 6.2: the cloud
voxel `D_sun` grid follows the dominant light, so cumulus shadows the ground
under moonlight the way it does under the sun.

---

## 6. Coupling into clouds and scene ambient

### 6.1 Replace `nightLight`

In `buildCloudShadeContext` the three terms

```
nightLight += sharedFactorAmbient * moonAmbientAirglow * 0.0015 * moonDot * phaseGlow * cloudMoonBrightness * nightFactor
nightLight += nightSkyColor * nightSkyBrightness * nightFactor * 0.5
nightLight += nightSkyColor * starBrightness * starAmbientCouplingStrength * 0.008 * nightFactor
```

are deleted. Ambient at night comes from the same three LUT taps the day
uses (`skyRadianceWarm/Cool/Zenith` via `sampleSkyAmbientForVolume`), with
their `dayFactor` gate **removed**, because the LUT now carries the
moon-scattered sky and the airglow floor, plus one scalar:

```
ctx.skyRadianceZenith += args.nightDomeMeanRadiance * G_night     // dome mean, from the bake
```

That is the entire night ambient. It is derived, it is small unless a moon
is up, and no slider reaches it. The user's current uniform blue term is
`(0.064, 0.093, 0.208) * (0.002*0.5 + 12*0.25*0.008) = (1.6e-3, 2.3e-3, 5.2e-3)`;
the physical ambient under a full default-size moon is ~2e-3 grey-blue from
the LUT's Rayleigh moon-sky, and ~5e-5 (dark) on a moonless night. Moonless
clouds will read as dark shapes against the stars, which is correct; a user
who wants them lifted raises `nightExposureEv`, which lifts everything
together.

The `dayFactor` gate also hides the fact that the sky-view LUT below -5 deg
is currently forced to zero by `atmosphereFade`; with 4.2 that is no longer
true, so removing the gate is safe only after Stage 5. Stage 1 therefore
substitutes a temporary `airglowFloor` constant for the LUT contribution
(Section 10).

### 6.2 Direct moonlight on clouds: one evaluator, one shadow grid

Target: the moon is a distant light like the sun, so it goes through
`evalNubisCubedSampleCore` with its own irradiance, direction and optical
depth instead of the "byte-faithful legacy" Lambert+HG path with six gains.
Then a cloud lit by the moon looks exactly like the same cloud lit by a
400,000x dimmer sun - which it is - and the cloud/ground ratio that is right
by day is right by night for free. Specifics:

* **Light slot.** `CloudShadeContext.sunDirYUp / sunRadiance` become
  `lightDir / lightIrradiance`. CPU picks: sun while `sunDirection.y > 0`,
  else the brightest moon (combined direction if several). The sun's direct
  term is identically zero below the horizon (its transmittance is), so the
  switch is not visible.
* **Shadow grid.** `cloud_sun_density_grid.comp.slang` bakes along
  `args.sunDirection`; pass it the chosen light direction instead. It
  re-bakes every frame already, so there is no cadence change. Terrain
  moon-shadows through `sampleCloudGroundShadow_OptionB` follow automatically,
  and the 2-tap `sampleCloudSunOpticalDepth_local` moon march is retired.
* **Secondary moons.** Additive with the legacy cheap path at their own
  (small) irradiance, or ignored in the march when below 5 % of the primary
  light. Their disks and distant lights are unaffected.
* **Gains retired**: `moonNeeStrength`, `cloudMoonBrightness`,
  `moonCloudDiffuseGain`, `moonCloudPhaseGain`, `moonCloudAnisotropy`
  (`cloudPhaseG1/G2` apply), `moonSilverLiningIntensity`, `moonAmbientAirglow`.

Expected look change, stated plainly: today's cloud-moon irradiance factor is
`cloudMoonBrightness 0.2`; the new one is `G = 48.5`. Directly moonlit cloud
faces go from ~1e-4 to ~1.7e-2 frame at the default moon: a moonlit deck
becomes several times brighter than the moonlit ground, with a lit side and
a shadow side, instead of a uniform 5e-3 blue glow. That is what a full moon
does. If the user finds it too much, the physical answer is a smaller moon
(the default is 45x too much light) or a lower `nightExposureEv`; the design
deliberately does not reintroduce a per-path cloud gain (Section 11.3).

### 6.3 Scene ambient

Diffuse indirect rays that miss get `LUT + dome` (3.8), so the scene's night
ambient is the same physical sky the clouds see, scaled by the existing
`skyIndirectRadianceScale` if the user uses it. Volumetric fog gets the LUT
floor through `sampleSkyAmbientForVolume`. No new coupling term is needed for
the scene.

---

## 7. Performance, precompute vs live, LUT placement

### 7.1 What is live per ray

| Component | Ray classes | Cost per ray | Where |
|---|---|---|---|
| Sky-view LUT tap | all | 1 tex (existing) | `sampleSkyViewLutForRay` |
| Transmittance tap (`T_view`) | primary-class | 1 tex, ~10 ALU | new in `evalSkyRadiance` |
| Dome tap | all | 9 MAD rotation + 1 tex | `evalNightDome` (new) |
| Catalogue stars | primary-class | ~20 ALU index + 2 loads + ~0.7 x 30 ALU | `evalCatalogueStars` (new) |
| Bright-list glare + spikes | primary-class | ~130-180 ALU | `evalGlare` (new) |
| Moon disks (up to 4) | primary-class | ~60 ALU each when enabled, early-out on `cosAngle` | `evalMoonDisk` (rewritten) |
| Moon glare | primary-class | in the glare loop | `evalGlare` |

Total added per primary sky ray **~300-400 ALU and 4-5 memory ops**, against
today's ~700+ ALU with the Milky Way on. Diffuse rays: 2 taps and a rotation.
Estimated **0.05-0.15 ms at 1440p DLSS Quality**, sky-fraction dependent;
measure with `debugSkyBisectFlags` bit 1 before and after each stage.

### 7.2 What is precomputed

| Resource | Size | When (re)built | Key |
|---|---|---|---|
| `StarEntries`, `StarCellOffsets` | ~320 KB + 98 KB | once at init | none (catalogue is constant) |
| Bright-star block | 512 B in `AtmosphereArgs` or buffer prefix | once | none |
| Night dome | 4 MB RGBA16F 1024x512 | init; on `milkyWayScale`, `milkyWayDustAmount`, `zodiacalScale`, texture path | those four |
| `nightDomeMeanRadiance` | 12 B in args | with the dome | |
| Sky-view LUT (now RGBA32F) | 2 MB | existing cache gate; add `airglowScale`, `nightExposureEv` to what is *not* zeroed | existing key |
| B-V -> RGB table | 3 KB CPU | once | |

**New dispatches**: exactly one, `night_dome_bake.comp.slang`, one-time (and
rare). Everything else is per-ray shader code or CPU work. No new per-frame
pass.

### 7.3 Bindings and files touched by new resources

Three new bindings at 218-220, the next free indices after
`BINDING_ATMOSPHERE_CLOUD_DEPTH_RT (217)`. 216 is marked retired in the header
(legacy view-pass placement-map slot, removed 2026-07-16) and, like 203, should
not be reused without a collision audit, so the design skips it:

| Step | File |
|---|---|
| `#define BINDING_ATMOSPHERE_STAR_CELLS 218`, `_STAR_ENTRIES 219`, `_NIGHT_DOME 220`; bump `BINDING_ATMOSPHERE_MAX`; add `STRUCTURED_BUFFER(...)` x2 and `TEXTURE2D(...)` to the slot macro list at lines ~174-184 (**this is the slot array whose omission gives `InvalidBinding` silently**) | `src/dxvk/shaders/rtx/pass/common_binding_indices.h` |
| `layout(binding=...) StructuredBuffer<StarEntry> StarEntries; StructuredBuffer<uint> StarCellOffsets; Texture2D<float4> AtmosphereNightDome;` | `src/dxvk/shaders/rtx/pass/common_bindings.slangh` |
| `StarEntry` struct shared with C++ | `src/dxvk/shaders/rtx/pass/atmosphere/star_entry.h` (new, included by both) |
| buffer creation (pattern: `m_aerialPerspectiveLightBuffer`, `DxvkBufferCreateInfo` + `m_device->createBuffer`, ~2487-2503), dome image (pattern: `m_cloudPlacementMap`, ~1785), bind in `bindResources` next to the existing `BINDING_ATMOSPHERE_*` lines (~4011-4038) with `ctx.bindResourceBuffer(slot, DxvkBufferSlice(buffer, 0, size))` for the two buffers and `bindResourceView` for the dome | `src/dxvk/rtx_render/rtx_atmosphere.cpp`, `.h` |
| dome bake shader + `ManagedShader` class + `PREWARM_SHADER_PIPELINE` + `dispatchNightDomeBake` | `src/dxvk/shaders/rtx/pass/atmosphere/night_dome_bake.comp.slang` (new), `rtx_atmosphere.cpp` |
| meson shader list | `src/dxvk/meson.build` |

Not touched: `atmosphere_bindings.slangh`, `cloud_render.comp.slang`,
`cloud_secondary_lut.comp.slang`, `composite.comp.slang` - none of them
evaluates stars or the dome (1.4). They receive `nightDomeMeanRadiance`,
`nightExposureEv` and the light-slot direction through `AtmosphereArgs`.

---

## 8. `AtmosphereArgs` changes

Rule kept: grow only by whole 16-byte rows; reuse retired slots first; every
field that feeds no bake is zeroed in `normalizeForSkyLutCache`; every field
that *does* feed a bake is **not**.

| Field | Slot | Feeds a bake? | Key handling |
|---|---|---|---|
| `nightExposureEv` (float) | new row A.x | yes (moon term in sky-view LUT) | keep in key |
| `airglowScale` (float) | new row A.y (or `nightSkyBrightness`'s slot, which is currently zeroed at line 689 and would have to be **un-zeroed** - prefer the new row so the zero list stays honest) | yes | keep in key |
| `nightLightSelect` (uint: 0 sun, 1..4 moon index for the cloud light slot) | new row A.z | yes (`D_sun` grid) | keep |
| `moonDiskExposureEv` | new row A.w | no | zero |
| `nightDomeMeanRadiance` (vec3) | reuse `nightSkyColor` (vec3, same row as `timeSeconds`) | no | zero (already zeroed) |
| `pixelAngleRad` | reuse `padStarAppearance0` | no | zero |
| `starGain` | reuse `starBrightness` | no | zero (already) |
| `starMagnitudeLimit` | reuse `starDensity` | no | zero |
| `starScintillation` | reuse `starTwinkleSpeed` | no | zero |
| `starPsfWidthPx` | reuse `starPsfSharpness` | no | zero |
| `glareStrength` | reuse `starHaloStrength` | no | zero |
| `starSpikeStrength` | unchanged | no | zero |
| `zodiacalScale`, `milkyWayScale` | reuse `milkyWayDensityBoost`, `milkyWayBackgroundBrightness` | dome bake only (own key) | zero in LUT key |
| `milkyWayDustAmount` | unchanged | dome bake only | zero in LUT key |
| retired: `starSizeMagnitudeScale`, `starCloudExtinctionPower`, `starAmbientCouplingStrength`, `milkyWayBackgroundColor/CoreColor/DustColor`, `moonNeeStrength`, `surfaceMoonBrightness`, `cloudMoonBrightness`, `haloMoonBrightness`, `moonCloudDiffuseGain`, `moonCloudPhaseGain`, `moonCloudAnisotropy`, `moonHaloMagnitude`, `moonAmbientAirglow` | left in place as named `padRetired*` per the file's own convention; written 0 | | |
| `MoonParams.darkSideBrightness` -> `earthshine` | same slot | disk only | n/a |
| bright-star block (32 x float4) | new rows, optional (alternative: buffer prefix) | no | zero |

Net growth: one row (A), plus 32 rows if the bright list lives in the CB.
Recommendation: keep the bright list in the entries buffer (offset 0, count
32) so the CB grows by exactly one row.

---

## 9. Migration

Mechanism: `RtxOptionImpl::migrateValuesTo(dest, transform)` already exists
and the codebase gates a `[Deprecated Config] please re-save your rtx config`
log on its return value. For every retired key, register a migration whose
transform **returns false** (declines) for any value whose meaning changed, so
the user sees exactly which saved keys were dropped and nothing is silently
re-scaled. Renamed-with-identical-meaning keys migrate 1:1. Game plugins that
push a retired key hit the same path: the value lands in a layer with no
consumer, and the same one-time log names the key.

Legend: **K** kept unchanged; **R** retired, value dropped with log;
**M** migrated 1:1 to a renamed key; **D** default changed, saved values honoured.

| Option (`rtx.atmosphere.`) | Fate | Replacement / note | Saved `starBrightness = 12`-class configs |
|---|---|---|---|
| `starBrightness` (0.5) | R | `starGain` (1.0 = physical; appearance only). The RemixSkyAPI "fade to 0 at sunrise" contract becomes unnecessary (twilight is physical); document in `docs/RemixSkyAPI.md` sections "Stars - pose & rotation" and the example at line ~466 | dropped with log; 12 was compensating for a hash field with no absolute scale |
| `starDensity` (0.5) | R | `starMagnitudeLimit` (6.5 = all naked-eye stars; 5.0 = light-polluted suburb) | dropped |
| `starTwinkleSpeed` (1.0) | R | `starScintillation` (1.0 = typical seeing; 0 = none) | dropped |
| `starRotation`, `starAxisElevation`, `starAxisRotation` | K | same frame semantics; content under it is now real | kept |
| `starPsfSharpness` (20) | R | `starPsfWidthPx` (0.7) | dropped |
| `starSizeMagnitudeScale` (0.8) | R | none; size follows flux through the glare skirt | dropped |
| `starHaloStrength` (0.055) | R | `glareStrength` (1.0 = Spencer weights; shared with moons) | dropped |
| `starSpikeStrength` (0.12) | K, D | default 0.05; bright list only | kept |
| `starCloudExtinctionPower` (2.5) | R | none; `T_cloud^1` with physically scaled stars. If cumulus cores show `T` floors well above 1e-3, that is a march clamp to fix, not a compositing exponent | dropped |
| `starAmbientCouplingStrength` (0.25) | R | none; ambient is derived (6.1) | dropped; 0.25 was compensating for `cloudMoonBrightness = 0.2` |
| `nightSkyBrightness` (0.002; weather 0.008-0.012) | R | `airglowScale` (1.0 = 22.0 mag/arcsec^2 dark site); weather X-macro field replaced in `rtx_weather.h` lines 57, 148, 215, 282, 349, 416, 483 and the two `docs/integrators/weather-presets-*.md` tables. All presets 1.0; fog/snow presets may raise it modestly to stand in for backscattered light pollution, labelled | dropped |
| `nightSkyColor` (0.15, 0.2, 0.4) | R | none in the light path; Purkinje blue belongs in the post stack (4.2) | dropped |
| `milkyWayEnabled` (false) | K, D | default **true**; a saved `false` still hides the dome's galactic term (same meaning) | kept |
| `milkyWayDensityBoost` (0.3) | R | none; band density is the catalogue's | dropped |
| `milkyWayBackgroundBrightness` (0.05) | R | `milkyWayScale` (1.0 = unresolved-starlight budget) | dropped |
| `milkyWayBackgroundColor`, `milkyWayCoreColor`, `milkyWayDustColor` | R | none; colours come from the model or the texture | dropped |
| `milkyWayDustAmount` (0.6) | K | appearance knob on the procedural dust lanes | kept |
| new `milkyWayTexture` (path, "") | new | optional CC BY asset (4.1) | |
| new `zodiacalScale` (1.0) | new | | |
| new `nightExposureEv` (5.6) | new | **the** illumination gain (2.3) | |
| new `moonDiskExposureEv` (0.0) | new | appearance only (2.4) | |
| `moonN.enabled/angularRadius/brightness/color/surfaceStyle/craterDensity/surfaceContrast/surfaceNoiseScale/roughnessAmount/elevation/rotation/phase` | K | `angularRadius` docstring and UI label corrected to "angular diameter" (1.5); `color` default D to (0.132, 0.120, 0.105) | kept |
| `moonN.darkSideBrightness` (0.005) | R | `moonN.earthshine` (1.0 = Earth) | dropped |
| `moonNeeStrength` (1.0; weather) | R | none; `nightExposureEv` is the master. Weather X-macro rows 59, 149, 216, 283, 350, 417 removed | dropped (all known configs at 1.0) |
| `moonAtmosphericCouplingStrength` (1.0; weather) | K | labelled artistic on the moon-sky term only; physical = 1 | kept |
| `surfaceMoonBrightness` (50) | R | absorbed into `nightExposureEv` default (2^5.6 = 48.5) | dropped; a saved 50 becomes the default gain, so the ground level is preserved for default-moon configs |
| `cloudMoonBrightness` (0.2) | R | none (6.2) | dropped; **this is the value that made the fake ambient necessary** |
| `haloMoonBrightness` (15), `moonHaloMagnitude` (0.0015), `moonHaloGlowStrength` (2.0) | R | `glareStrength` (5.5) | dropped |
| `moonAmbientAirglow` (1.0) | R | LUT moon-sky ambient (6.1) | dropped |
| `moonSilverLiningIntensity` (2.0), `moonCloudDiffuseGain` (0.1), `moonCloudPhaseGain` (1.0), `moonCloudAnisotropy` (0.85) | R | Nubis evaluator with `cloudPhaseG1/G2` (6.2) | dropped |
| `moonShadowSoftnessDeg` (0), `moonCloudShadowed` (true) | K | | kept |
| `directionalLightRadianceScale` (1.0) | K | | kept |
| `skyIndirectRadianceScale` | K | | kept |
| `skyViewRebakeGranularityDeg`, `skyViewAltitudeRebakeGranularityKm` | K | | kept |

Docs to update in lockstep: `docs/RemixSkyAPI.md` (sections at lines 82-116
and 157-160, example at ~455-476), `RtxOptions.md` (generated), `docs/CloudSystem.md`
item 4 ("The moon path is a byte-faithful port of the legacy analytical
lighting" becomes false at Stage 7), `docs/integrators/weather-presets-reference.md`
and `-customization.md`, `docs/fork-touchpoints.md` (a new workstream entry).

UI (`rtx_atmosphere_ui.cpp: showNightSettings`): reorganise into
**Exposure** (`nightExposureEv` with the two presets, `moonDiskExposureEv`),
**Moons** (per-moon, unchanged layout plus `earthshine`; the "angular
diameter" label), **Stars** (`starGain`, `starMagnitudeLimit`,
`starScintillation`, `starPsfWidthPx`, `glareStrength`, `starSpikeStrength`),
**Sky glow** (`airglowScale`, `zodiacalScale`, `milkyWayEnabled`,
`milkyWayScale`, `milkyWayDustAmount`, texture path), and a read-only
**Radiometric probe** panel printing, from the CPU mirror: moon irradiance
per enabled moon in frame units and lux-equivalent, adaptation value, `G_night`,
dark-sky floor, Vega's peak pixel radiance at the current render resolution,
and the LUT format. The probe is what makes each stage independently
testable without a photometer.

---

## 10. Staged implementation plan

Each stage builds, is testable in-game on its own, and lands the most visible
win first. Every stage runs the `debugSkyBisectFlags` bit-1 A/B and records
the path-tracing stage time.

**Stage 1 - Spine and the cloud fix (fixes the complaint).**
`atmosphere_common.slangh`: `nightRadiometry()`, `adaptation()`, `G_night`,
`moonIrradiance()`. `rtx_atmosphere.cpp`: `nightExposureEv`, C++ mirrors,
`syncDistantLights` on `moonIrradiance` (drop `moonNee * surfMoon *
nightFactor`), args row A, key handling. `cloud_march_common.slangh`: delete
the three `nightLight` terms; feed the legacy moon path `moonIrradiance`
(drop `cloudMoonBrightness`, `moonNeeStrength`); add a temporary
`airglowFloor` constant (1.1e-8 x G) until Stage 5 puts it in the LUT.
Retire the per-path gains with declining migrations. **Test**: full moon,
default 3.5 deg - ground level unchanged from a saved default config; clouds
have a lit side and a shadow side, no uniform blue; moonless - clouds dark.
Probe shows E_moon = 1.78e-3 frame / 13.9 lux-eq for the default moon, 4.0e-5 /
0.31 lux-eq at 0.52 deg.

**Stage 2 - Moon phase and disk.** `moonPhaseIllumination` -> K&S;
`getPhaseIllumination` -> L-S shape with `KS/Phi_LS` normalisation; remove
`limbDarkening`; earthshine; `earthshine` option; unit-mean detail constants;
warm albedo default; additive-over-inscatter compositing of the disk (2.5).
**Test**: quarter moon 1/11 of full on the ground (probe); crescent shows a
soft terminator and a faint earthshine disk at +3 EV disk exposure; the
daytime moon is veiled blue, not cut out.

**Stage 3 - Catalogue stars.** `star_entry.h`, `buildStarCatalogueBuffers`,
three bindings and the slot-array entries, `evalCatalogueStars` with the
flux-conserving core and windowed skirt, B-V table, `T_view` extinction,
scintillation, `pixelAngleRad`; delete `evalStarField`'s hash grid and
`starProfile`; retire the star options. **Test**: Orion, the Plough and the
Pleiades are recognisable; rotate the camera slowly - no boiling; toggle DLSS
ratios - Vega's total energy constant (debug view summing a 9x9 around it);
stars redden and dim toward the horizon; none below it.

**Stage 4 - Night dome.** `night_dome_bake.comp.slang`, dome binding,
`evalNightDome`, `nightDomeMeanRadiance` into args and into the cloud
context; galactic model from the catalogue density + disk + dust; zodiacal
band; delete the FBM Milky Way; `milkyWayEnabled` default true. **Test**: the
band runs through the dense catalogue regions (Cygnus to Sagittarius); its
glow is visible on a moonless night at +11 EV and gone under a full moon;
cloud ambient on a moonless night equals the probe's dome mean.

**Stage 5 - Airglow in the LUT, twilight, precision.** Sky-view LUT to
RGBA32F; airglow with van Rhijn in `sky_view_lut.comp.slang`; move the
deep-night fade into the bake on the sun term over -8..-18 deg; remove
`atmosphereFade`, `moonFade`, the cloud `dayFactor` gate and the Stage 1
`airglowFloor`; `airglowScale` option and weather field. **Test**: sweep the
sun from +5 to -20 deg - one continuous transition, stars appearing
brightest-first, no pop at -5 deg; the moonless zenith sky matches
22.0 mag/arcsec^2 x G in the probe; no banding at the horizon.

**Stage 6 - Glare.** `evalGlare` for the 32-star bright list and the moon
disks from displayed flux; retire the Gaussian halo and its three options;
`glareStrength`; spikes moved to the bright list. **Test**: Sirius and Vega
glow, m > 2 stars do not; the moon's halo follows phase and disk exposure;
`glareStrength = 0` gives bare discs.

**Stage 7 - Cloud light slot and shadow grid.** `lightDir/lightIrradiance`
in `CloudShadeContext`; `nightLightSelect`; `D_sun` grid along the chosen
light; moon through `evalNubisCubedSampleCore`; retire the legacy moon path
and its five gains; update `docs/CloudSystem.md`. **Test**: moonlit cumulus
casts terrain shadows; silver lining at the same `cloudPhaseG1` as the sun's;
cloud/ground brightness ratio equals the daytime ratio (probe compares).

**Stage 8 - Cleanup.** UI reorganisation and probe panel; docs; migration
log wording; remove `hash33`/`fbmNoise3D` uses that are now dead from
`atmosphere_sky.slangh`; `starFaintDesaturation` and `nightSkyTint` only if
asked for.

Stage 1 supersedes the uncommitted `moonPhaseIllumination` bodies only in
Stage 2 (the Lambert+surge stays as the interim law through Stage 1); Stage 3
supersedes the uncommitted blackbody tint, cubed distribution, size scale,
halo/spike profile and air-mass twinkle wholesale; the uncommitted moon disk
Lambert terminator is superseded in Stage 2; `moonShadowSoftnessDeg` and
`moonCloudShadowed` are kept as landed.

---

## 11. Open questions, risks, and what I could not verify

**11.1 Spencer et al. constants.** The `f0/f1/f2` forms and the
0.384/0.478/0.138 weights are from memory. The design depends only on the
shape (Gaussian core, `1/theta^3`, `1/theta^2`) and the rough 40/60 energy
split; the implementer must check the numbers against the 1995 paper before
they are hardcoded, and the CPU normalisation step makes any error in the
constants an error of *shape*, never of total flux.

**11.2 Integrated starlight total.** The catalogue sum (96.0 Vega whole sky)
is exact. The "3-4x for unresolved stars" multiplier is a literature-scale
estimate I could not pin to a single source in this session; it only sets the
mean of the dome's galactic term and is exposed as `milkyWayScale`.

**11.3 Look risk: moonlit clouds get much brighter.** 6.2 makes them ~250x
brighter relative to the ground than today's tuned configs. That is
physical, and the fake ambient's job disappears with it, but the user may
not like it. The design deliberately omits a per-path cloud gain; if one is
demanded after Stage 1 is seen in-game, add exactly one, labelled appearance,
default 1, and note in its docstring that it breaks the day/night consistency
argument.

**11.4 Cone spread for glossy reflections.** `PathState.coneSpreadAngle` is
declared "unused". If it is not populated at the sky-miss site, the
sigma-widening in 3.7 cannot be done and rough-specular sky misses keep
today's gating only.

**11.5 DLSS and sub-pixel HDR points.** Not measured. The flux-conserving
formulation is the best available defence; the composite-time alternative is
described but not recommended.

**11.6 Twilight LUT noise.** Extending the sun-term fade from -5 deg to
-8..-18 deg exposes whatever multiscatter noise `atmosphereFade` was hiding.
If it shows, the fix is in the bake (more samples at low sun, or clamp the
multiscatter term below a physical floor), not a return to the 0..-5 deg kill.

**11.7 Zodiacal light without elongation.** Latitude-only is a labelled
approximation; the Gegenschein and the twilight cones are absent.

**11.8 Airglow colour.** `(1.0, 0.83, 0.70)` is derived from the sky's B-V
~0.8 via a blackbody, which is a poor spectral model for emission lines.
Correct to first order (warm, not blue); refine from a spectrum if it matters.

**11.9 Auto-exposure interaction.** A 0.625-frame disk in view already pulls
the auto exposure down today; nothing here changes that, but `moonDiskExposureEv
> 0` makes it stronger. Note in the option docstring.

**11.10 `starRotation` semantics for saved configs.** Kept, but the content
under any saved angle is now different stars. Harmless; the API doc should
state where RA 0h lands at rotation 0.

**11.11 Weather presets.** Removing `nightSkyBrightness`, `nightSkyColor`,
`moonNeeStrength` from the `WeatherSnapshot` X-macro touches seven preset
blocks and the blend code; a saved custom preset carrying those keys hits the
same deprecation log. I did not trace the preset serialiser for other
consumers.

**11.12 Not verified in this session**: Kang et al. 2002 Planckian-locus
coefficients (compute the table with any accurate CIE method; the fit is a
convenience); whether `RtxMipmap`/`Resources::createImageResource` supports
the upload path a CC BY texture asset would need (the procedural bake does
not need it).

Verified while writing: `evalAtmosphereRadiance` has exactly one caller,
`sky_view_lut.comp.slang:85`, so airglow may be added in either place - the
design puts it in the bake shader to keep the shared function's contract
unchanged; binding 216 is retired in the header (7.3); structured buffers bind
through `ctx->bindResourceBuffer(slot, DxvkBufferSlice(...))` as the LUT
constant buffers already do (`rtx_atmosphere.cpp` ~2181-2236).

---

## Appendix A - Formulas collected

```
// Units
E_sun_frame     = args.sunIlluminance                          // 16.35 at defaults
kFramePerLux    = E_sun_frame / 128000
kFramePerWm2    = E_sun_frame / 1361
kFramePerNit    = kFramePerLux                                 // radiance and irradiance share the factor

// Gain
adaptation(y)   = smoothstep(sin(-5 deg), sin(-13 deg), -y)
G_night         = lerp(1, 2^nightExposureEv, adaptation)
G_disk          = 2^moonDiskExposureEv

// Stars
E_star(m)       = kFramePerLux * 2.54e-6 * 10^(-0.4 m)
T(B-V)          = 4600 * (1/(0.92 (B-V) + 1.7) + 1/(0.92 (B-V) + 0.62))      // Ballesteros 2012
psfCore(theta)  = exp(-theta^2 / 2 sigma^2) / (2 pi sigma^2),  sigma = starPsfWidthPx * pixelAngleRad
skirt(theta)    = glareStrength * (0.478 f1 + 0.138 f2) * window(theta / marginDeg)   // Spencer 1995, verify
scint           = 1 + clamp(0.12 X^0.9, 0, 0.6) * starScintillation * (0.6 sin(w1 t + p1) + 0.4 sin(w2 t + p2))

// Sky floor
S(mag/arcsec^2) = 10.8e4 * 10^(-0.4 mag) cd/m^2
L_ag(z)         = airglowScale * 1.1e-8 * vanRhijn(z) * T_layer(z),  vanRhijn(z) = 1/sqrt(1 - (R/(R+90))^2 sin^2 z)

// Moon
alpha           = pi |1 - 2 phase|
dm(alpha_deg)   = 0.026 alpha + 4e-9 alpha^4                                 // Krisciunas & Schaefer 1991
KS(alpha)       = 10^(-0.4 dm)
Phi_LS(alpha)   = 1 - sin(alpha/2) tan(alpha/2) ln(cot(alpha/4))             // Jensen et al. 2001 Eq. 5
L_mean_full     = E_sun_frame * m.color * m.brightness * T_atm(moonDir) / pi
Omega           = 2 pi (1 - cos m.angularRadius)
moonIrradiance  = L_mean_full * KS(alpha) * Omega * G_night
distantRadiance = moonIrradiance * directionalLightRadianceScale / pi
L_disk(p)       = L_mean_full * detail(p) * [2 cos_i / (cos_i + cos_e)] * KS/Phi_LS * G_disk
earthshine      = L_mean_full * detail(p) * m.earthshine * 5.0e-5 * Phi_LS(pi - alpha) * G_disk   // Jensen et al. 2001 Eq. 1

// Composite
radiance = LUT(dir) + T_view(dir) * [(1 - sum cov) * (dome + stars) * G_night + sum cov_i L_disk_i] + glare(dir)
```

## Appendix B - Catalogue statistics (computed from `rtx_star_catalogue_data.h`)

Script: `scratchpad/catstats.py`, run on the packed table, decode as in the
header comment.

```
count 9096
sum 10^(-0.4m) = 96.0 Vega-equivalents  -> 2.44e-4 lux whole sky, 1.22e-4 per hemisphere
paper Eq.6 whole-catalogue 2.41e-6 W/m^2 ; Sirius alone 9.64e-8 ; Vega 2.44e-8   (paper's "integrated starlight" is 3.0e-8)
  m<=0.0: n=    4  flux= 7.7 Vega ( 8%)
  m<=1.0: n=   15  flux=14.9 Vega (15%)
  m<=2.0: n=   52  flux=22.7 Vega (24%)
  m<=3.0: n=  180  flux=34.4 Vega (36%)
  m<=4.0: n=  536  flux=47.3 Vega (49%)
  m<=5.0: n= 1650  flux=63.5 Vega (66%)
  m<=6.0: n= 5197  flux=84.4 Vega (88%)
  m<=6.5: n= 8404  flux=94.5 Vega (98%)
brightest 32: m -1.44..1.76, flux share 20%          -> the bright list
B-V mean 0.59, median 0.47, range -0.28..5.75

stars per 10 deg |b| band, per steradian (galactic latitude, J2000 pole RA 192.859 Dec 27.128):
  |b|  0-10: 2400 stars  1100 /sr
  |b| 10-20: 1884 stars   890 /sr
  |b| 20-30: 1455 stars   733 /sr
  |b| 30-40: 1058 stars   590 /sr
  |b| 40-50:  807 stars   521 /sr
  |b| 50-60:  641 stars   510 /sr
  |b| 60-70:  490 stars   529 /sr
  |b| 70-80:  260 stars   459 /sr
  |b| 80-90:  101 stars   529 /sr                     -> 2.1:1 plane-to-pole contrast; the band is in the data

cube-face cell occupancy (N cells per face edge):
  N=32:  6144 cells mean 1.48 max 20 p99 7   3x3-max 79   +107% entries at 0.5 deg margin   cell 1.2-3.6 deg
  N=48: 13824 cells mean 0.66 max 15 p99 4   3x3-max 50   +172%                              cell 0.8-2.4 deg
  N=64: 24576 cells mean 0.37 max 11 p99 3   3x3-max 31   +246% (0.5 deg) / ~+77% (0.25 deg) cell 0.6-1.8 deg   <- chosen
  N=96: 55296 cells mean 0.16 max  8 p99 3   3x3-max 20   +429%                              cell 0.4-1.2 deg
```

## Appendix C - Where each change lands

| Change | File | Function / location |
|---|---|---|
| Units, adaptation, gain, moon irradiance, K&S phase | `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh` | new `nightRadiometry`, `adaptation`, `nightGain`, `moonIrradiance`; `moonPhaseIllumination` body (~470); `computeMoonSharedFactor` kept as `L_mean_full`; combined-moon block in `evalAtmosphereRadiance` (~585-625) |
| C++ mirrors, distant lights | `src/dxvk/rtx_render/rtx_atmosphere.cpp` | `fhMoonPhaseIllumination` (~4098), `syncDistantLights` (~4189, moon loop ~4248-4280) |
| Args fill, key zeroing | same | `getAtmosphereArgs` (~1060-1120), `normalizeForSkyLutCache` (~500-582), `normalizeForVoxelGridKey` (~686-700) |
| Star buffers, dome, bindings, LUT format | same | new `buildStarCatalogueBuffers`, `dispatchNightDomeBake`; `bindResources` (~4006-4040); sky-view LUT creation (~1595-1605) |
| Options | `src/dxvk/rtx_render/rtx_atmosphere.h` | night block (~685-770), moon macro (~779-818), moon gains (~820-878) |
| UI | `src/dxvk/rtx_render/rtx_atmosphere_ui.cpp` | `showNightSettings` (~1416), `renderStarsUI`, `renderMilkyWayUI`, `renderStarAppearanceUI`, `renderMoonGlobalLightingUI`, `renderMoonCloudLookUI`, `renderMoonUI` |
| Stars, dome, disk, glare, composite order | `src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh` | replace `hash33`/`starTint`/`starProfile`/`evalStarField`/`evalNightSky`; `getPhaseIllumination` -> L-S shape; `evalMoonDisk`; `evalSkyRadiance` body (~857-1093) |
| Cloud night ambient and light slot | `src/dxvk/shaders/rtx/pass/atmosphere/cloud_march_common.slangh` | `buildCloudShadeContext` (~496-660), `CloudShadeContext` (~200-247), `evalNubisCubedSampleCore` light inputs |
| Airglow in bake, sun-term twilight fade | `src/dxvk/shaders/rtx/pass/atmosphere/sky_view_lut.comp.slang` | after the `evalAtmosphereRadiance` call |
| Shadow grid light direction | `src/dxvk/shaders/rtx/pass/atmosphere/cloud_sun_density_grid.comp.slang` | direction input |
| Bindings | `src/dxvk/shaders/rtx/pass/common_binding_indices.h`, `common_bindings.slangh` | new indices + slot macro list (~174-184); declarations (~160-200) |
| Weather | `src/dxvk/rtx_render/rtx_weather.h` | X-macro rows listed in Section 9 |
| Docs | `docs/RemixSkyAPI.md`, `RtxOptions.md`, `docs/CloudSystem.md`, `docs/integrators/weather-presets-*.md`, `docs/fork-touchpoints.md` | |
