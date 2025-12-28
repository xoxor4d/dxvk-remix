/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx/pass/particles/particle_system_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/procedural_noise.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"

// Ray-query fallback for cases where the surface is not available in screen-space (e.g. single-sided ceiling viewed from the back side)
#include "rtx/pass/instance_definitions.h"
#include "rtx/concept/ray/ray.h"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/concept/ray/ray_interaction.slangh"
#include "rtx/concept/surface/surface_interaction.slangh"
    
layout(binding = PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT)
Texture2D<float4> PreviousWorldPosition_WorldTriangleNormal;

layout(binding = PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
Texture2D<float2> PreviousPrimaryScreenSpaceMotionVector;

// Outputs

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
RWStructuredBuffer<GpuParticle> Particles;

layout(binding = PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT)
RWStructuredBuffer<int> Counter;

// small bias to avoid re-penetration
static const float kCollisionBias = 1e-3f;

bool collideParticleWithSceneTLAS(inout GpuParticle p)
{
  // Always use a TLAS ray query for collision so results do not depend on what's currently visible in screen-space.
  // This is more robust for rain indoors (single-sided ceilings), but more expensive than sampling the GBuffer.

  const float dt = particleCb.deltaTimeSecs;
  const float thickness = particleCb.particleSystem.desc.collisionThickness;

  // Sweep along this frame's motion segment (continuous collision / no tunneling).
  const float3 motion = p.velocity * dt;
  const float motionLen = length(motion);

  if (motionLen < 1e-6f)
    return false;

  Ray ray;
  ray.origin = p.position;
  ray.coneRadius = 0.h;
  ray.spreadAngle = 0.h;
  ray.direction = f16vec3(motion / motionLen);
  ray.tMax = motionLen + thickness;

  RayDesc rayDesc = rayToRayDesc(ray);

  // Note: Do NOT cull backfaces - we want two-sided collision (ceilings block rain from below).
  // Also allow non-opaque candidates so we can decide what to commit.
  const uint rayFlags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
  const uint8_t rayMask = OBJECT_MASK_ALL_STANDARD;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, rayFlags, rayMask, rayDesc);

  while (rayQuery.Proceed())
  {
    if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
    {
      const uint customIndex = rayQuery.CandidateInstanceID();
      const uint geometryIndex = rayQuery.CandidateGeometryIndex();
      const uint surfaceIndex = (customIndex & CUSTOM_INDEX_SURFACE_MASK) + geometryIndex;

      const Surface hitSurface = surfaces[nonuniformEXT(surfaceIndex)];

      // For collision we accept typical scene geometry even if not flagged fully opaque (alpha-test etc),
      // but avoid colliding with particles/decals if they exist as geometry.
      if (!hitSurface.isParticle && !hitSurface.isDecal)
      {
        rayQuery.CommitNonOpaqueTriangleHit();
      }
    }
  }

  if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    return false;

  // Build a surface interaction so we can get a stable triangle normal for reflection.
  RAY_HIT_INFO_CREATE_RAY_QUERY_COMMITTED(rayHitInfo, rayQuery);
  RayInteraction rayInteraction = rayInteractionCreate(ray, rayHitInfo);
  Surface surface = surfaces[nonuniformEXT(rayInteraction.surfaceIndex)];
  SurfaceInteraction surfaceInteraction = surfaceInteractionCreate<false>(surface, rayInteraction, ray, false);

  const float3 n = normalize(float3(surfaceInteraction.triangleNormal));

  // Choose normal to oppose motion (two-sided)
  const float3 collisionNormal = (dot(motion, n) <= 0.0f) ? n : -n;

  // Snap to hit position (slightly offset) and reflect velocity.
  p.position = surfaceInteraction.position + collisionNormal * kCollisionBias;
  p.velocity = reflect(p.velocity, collisionNormal) * particleCb.particleSystem.desc.collisionRestitution;

  return true;
}

bool collideParticleWithSceneScreenSpace(inout GpuParticle p)
{
  const float dt = particleCb.deltaTimeSecs;
  const float thickness = particleCb.particleSystem.desc.collisionThickness;

  // Project current world-space particle position into previous frame clip space and sample previous primary GBuffer.
  float4 prevClip = mul(particleCb.prevWorldToProjection, float4(p.position, 1.0f));
  float2 prevNdc  = prevClip.xy / prevClip.w;
  float2 prevUv   = prevNdc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

  if (prevUv.x < 0.0f || prevUv.x > 1.0f || prevUv.y < 0.0f || prevUv.y > 1.0f)
    return false;

  uint2 pixel = uint2(
    prevUv.x * particleCb.renderingWidth  + 0.5f,
    prevUv.y * particleCb.renderingHeight + 0.5f
  );

  MinimalSurfaceInteraction si = minimalSurfaceInteractionReadFromGBuffer(pixel, PreviousWorldPosition_WorldTriangleNormal);
  if (!si.isValid)
    return false;

  float3 n = normalize(float3(si.triangleNormal));

  // Approximate scene velocity using the previous screen-space motion vector.
  float2 screenVelocity = PreviousPrimaryScreenSpaceMotionVector[pixel];
  MinimalSurfaceInteraction si2 = minimalSurfaceInteractionReadFromGBuffer(pixel + screenVelocity, PreviousWorldPosition_WorldTriangleNormal);

  float3 sceneVel = 0..xxx;
  if (si2.isValid)
    sceneVel = (si.position - si2.position);

  float3 relVel = p.velocity - sceneVel;

  // Continuous collision against the GBuffer plane (two-sided).
  const float d0 = dot(p.position - si.position, n);
  const float d1 = dot((p.position + relVel * dt) - si.position, n);

  const float3 collisionNormal = (d0 >= 0.0f) ? n : -n;

  if (dot(relVel, collisionNormal) >= 0.0f)
    return false;

  const float distToPlane = abs(d0);
  const bool withinBand = distToPlane <= thickness;
  const bool crossesPlane = (d0 > 0.0f && d1 <= 0.0f) || (d0 < 0.0f && d1 >= 0.0f);

  if (!withinBand && !crossesPlane)
    return false;

  // Push out and reflect relative velocity.
  const float push = max(distToPlane - kCollisionBias, 0.0f);
  p.position -= collisionNormal * push;

  const float3 vRef = reflect(relVel, collisionNormal);
  p.velocity = vRef * particleCb.particleSystem.desc.collisionRestitution + sceneVel;

  return true;
}

void collideParticleWithScene(inout GpuParticle p)
{
  const ParticleCollisionMode mode = particleCb.particleSystem.desc.collisionMode;

  if (mode == ParticleCollisionMode::TLAS)
  {
    collideParticleWithSceneTLAS(p);
    return;
  }

  if (mode == ParticleCollisionMode::ScreenSpace)
  {
    collideParticleWithSceneScreenSpace(p);
    return;
  }

  // Hybrid (default): screen-space when valid, otherwise TLAS.
  if (!collideParticleWithSceneScreenSpace(p))
    collideParticleWithSceneTLAS(p);
}

// Updates a particles state for one time step
void evolve(inout GpuParticle particle, const GpuParticleSystem particleSystem)
{
  if (particleSystem.desc.useTurbulence)
  {
    particle.velocity += curlOfValueNoise(particle.position * particleSystem.desc.turbulenceFrequency, particleCb.absoluteTimeSecs * particleSystem.desc.turbulenceFrequency) * particleSystem.desc.turbulenceForce * particleCb.deltaTimeSecs;
  }
  
  particle.velocity += particleCb.upDirection * particleSystem.desc.gravityForce * particleCb.deltaTimeSecs;
  
  if (particleSystem.desc.enableCollisionDetection)
  {
    collideParticleWithScene(particle);
  }
  
  if (dot(particle.velocity, particle.velocity) > float16_t(0.f) && particleSystem.desc.maxSpeed >= 0)
  {
    particle.velocity = min(particleSystem.desc.maxSpeed, length(particle.velocity)) * normalize(particle.velocity);
  }
  
  particle.position += particle.velocity * particleCb.deltaTimeSecs;
  
  if (!particleSystem.desc.enableMotionTrail)
  {
    if (particleSystem.desc.alignParticlesToVelocity)
    {
      particle.rotation = 0.h;
    }
    else
    {
      particle.rotation = (particle.rotation + particle.rotationSpeed(particleSystem) * particleCb.deltaTimeSecs) % twoPi;
    }
  }
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if (threadIdx.x >= particleSystem.simulateParticleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.particleTailOffset) % particleSystem.desc.maxNumParticles;
  
  GpuParticle particle = Particles[particleIdx];
  
  if (!WaveActiveAllTrue(particle.isSleeping() || particle.isDead()))
  {
    // Save cycles if there are whole warps of sleeping particles
    evolve(particle, particleSystem);
  }
  
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  // Our counting strategy is conservative, therefore, we must also account for the non-random, max lifetime
  float targetLifetimeForCounter = (particle.initialTimeToLive(particleSystem) - particleSystem.desc.maxTtl);
  if (particle.timeToLive <= targetLifetimeForCounter && !particle.isDead()) 
  {
    particle.setDead();
    
    // Decrease the running count for this frame by 1
    InterlockedAdd(Counter[0], -1);
  }
  
  Particles[particleIdx] = particle;
}
