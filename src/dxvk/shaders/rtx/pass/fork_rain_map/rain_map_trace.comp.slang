// Fork-owned. Rain map, step 1: one downward ray per texel against the static opaque geometry around the
// camera. Moving objects and alpha tested surfaces are looked through, so a car never dries the road and a
// tree canopy never counts as a roof. The window and the trace range come from RaytraceArgs
// (rtx_fork_rain_map.cpp). World Z is up (GTA IV).

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/fork_rain_map/rain_map_bindings.slangh"
#include "rtx/pass/raytrace_args.h"
#include "rtx/pass/instance_definitions.slangh"

#include "rtx/utility/math.slangh"
#include "rtx/utility/common.slangh"
#include "rtx/concept/surface/surface.slangh"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.wetMapResolution))
  {
    return;
  }

  const vec2 xy = vec2(cb.wetMapOriginX, cb.wetMapOriginY) + (vec2(threadId) + 0.5f) * cb.wetMapTexel;

  RayDesc rayDesc;
  rayDesc.Origin = vec3(xy, cb.wetMapTraceTop);
  rayDesc.Direction = vec3(0.0f, 0.0f, -1.0f);
  rayDesc.TMin = 0.0f;
  rayDesc.TMax = max(cb.wetMapTraceTop - cb.wetMapTraceBottom, 1.0f);

  // Every triangle is a candidate and nothing is committed, so every static, fully opaque surface along the ray is
  // visited: the lowest is the ground, the highest is cover only when it stands at least the rain shadow min height
  // above that ground. A parked car (static after a few frames), a bench or a truck never becomes a roof this way,
  // and the order the traversal finds hits in does not matter.
  const uint rayFlags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, rayFlags, OBJECT_MASK_OPAQUE, rayDesc);

  float top = cb.wetMapTraceBottom - 1.0f;
  float bottom = cb.wetMapTraceTop + 1.0f;
  while (rayQuery.Proceed())
  {
    if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
    {
      const uint surfaceIndex = (rayQuery.CandidateInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CandidateGeometryIndex();
      const Surface hitSurface = surfaces[nonuniformEXT(surfaceIndex)];
      if (hitSurface.isStatic && hitSurface.isFullyOpaque)
      {
        const float height = cb.wetMapTraceTop - rayQuery.CandidateTriangleRayT();
        top = max(top, height);
        bottom = min(bottom, height);
      }
    }
  }

  if (bottom > cb.wetMapTraceTop)
  {
    RainMapHeightOutput[threadId] = float2(cb.wetMapTraceBottom - 1.0f, cb.wetMapTraceBottom - 1.0f);
    return;
  }

  const float cover = top - bottom >= cb.wetOcclusionMinHeight ? top : bottom;
  RainMapHeightOutput[threadId] = float2(bottom, cover);
}
