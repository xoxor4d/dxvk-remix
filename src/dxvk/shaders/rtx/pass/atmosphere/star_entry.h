/*
* Shared CPU/GPU layout for the star catalogue buffers.
*
* See docs/NightSkyRedesign.md sections 3.1-3.3. The catalogue itself is
* src/dxvk/rtx_render/rtx_star_catalogue_data.h (Bright Star Catalogue, CDS
* V/50); this is the form it takes on the GPU after RtxAtmosphere builds it
* once at startup.
*/
#pragma once

#include "rtx/utility/shader_types.h"

// Cells per cube face. 64 gives 24,576 cells at 0.6-1.8 degrees across, and a
// mean occupancy below one star, so a ray that reads a single cell reads
// roughly one entry.
#define STAR_CELLS_PER_FACE 64u
#define STAR_CELL_COUNT (6u * STAR_CELLS_PER_FACE * STAR_CELLS_PER_FACE)

// Stars are binned conservatively: an entry is written into every cell that
// comes within this angle of it, so a ray only ever reads its own cell and
// still sees every star whose profile could reach it.
//
// This is also the hard support limit of the drawn profile. The two numbers
// are the same number on purpose - a profile wider than the margin would be
// summed by pixels that fetched the star and not by pixels that did not, and
// that discontinuity takes the shape of whatever region was searched. An
// earlier procedural version of this had exactly that bug, with a 3x3 cell
// gather, and every star rendered as a bright square.
#define STAR_BIN_MARGIN_RAD 0.004363f   // 0.25 degrees

// The bright list lives at the head of the entries buffer: these are drawn
// with the wide glare skirt and the diffraction spikes, which are invisible on
// anything fainter and would otherwise cost an atan2 per catalogue star.
#define STAR_BRIGHT_COUNT 32u

struct StarEntry {
  // Unit vector in the celestial frame, +Y toward the celestial pole.
  vec3 dir;

  // Chromaticity in r8g8b8, normalised so its Rec.709 luminance is 1, plus the
  // catalogue's visual magnitude byte in the top 8 bits (m = v/25 - 2).
  //
  // Keeping colour as pure chromaticity is what stops brightness being counted
  // twice: the star's magnitude already *is* its luminance, so irradiance
  // times this chroma has exactly the irradiance's luminance. The previous
  // procedural field multiplied a tint by a separate brightness and drifted.
  uint packed;
};
