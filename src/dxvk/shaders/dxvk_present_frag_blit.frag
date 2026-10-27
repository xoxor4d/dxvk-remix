#version 450

layout(constant_id = 1) const bool s_gamma_bound = true;

layout(binding = 0) uniform sampler2D s_image;
layout(binding = 1) uniform sampler1D s_gamma;

layout(location = 0) in  vec2 i_coord;
layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform present_info_t {
  ivec2 src_offset;
  uvec2 src_extent;
  // NV-DXVK start: HDR10 output for the alt tonemapper
  uint  hdr10;
  float white_nits;
  // NV-DXVK end
};

// NV-DXVK start: HDR10 output for the alt tonemapper
// Encodes a back buffer that is gamma 2.2 encoded with 1.0 at UI white to BT.2020 PQ with 1.0 at white_nits,
// then adds a light dither at the swap chain's 10-bit precision.
vec3 encodeHdr10(vec3 color) {
  vec3 linear709 = pow(max(color, vec3(0.0f)), vec3(2.2f));
  vec3 linear2020 = max(vec3(
    dot(vec3(0.6274038960f, 0.3292830384f, 0.0433130657f), linear709),
    dot(vec3(0.0690972894f, 0.9195403951f, 0.0113623156f), linear709),
    dot(vec3(0.0163914388f, 0.0880133078f, 0.8955952532f), linear709)), vec3(0.0f));

  const float m1 = 2610.0f / 16384.0f;
  const float m2 = 128.0f * (2523.0f / 4096.0f);
  const float c1 = 3424.0f / 4096.0f;
  const float c2 = 32.0f * (2413.0f / 4096.0f);
  const float c3 = 32.0f * (2392.0f / 4096.0f);
  vec3 y = pow(linear2020 * (white_nits / 10000.0f), vec3(m1));
  vec3 pq = pow((c1 + c2 * y) / (1.0f + c3 * y), vec3(m2));

  float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898f, 78.233f))) * 43758.5453f) - 0.5f;
  return clamp(pq + noise / 1023.0f, 0.0f, 1.0f);
}
// NV-DXVK end

void main() {
  vec2 coord = vec2(src_offset) + vec2(src_extent) * i_coord;
  o_color = textureLod(s_image, coord, 0.0f);

  // NV-DXVK start: HDR10 output for the alt tonemapper
  if (hdr10 != 0u) {
    o_color = vec4(encodeHdr10(o_color.rgb), 1.0f);
    return;
  }
  // NV-DXVK end

  if (s_gamma_bound) {
    o_color = vec4(
      texture(s_gamma, o_color.r).r,
      texture(s_gamma, o_color.g).g,
      texture(s_gamma, o_color.b).b,
      o_color.a);
  }
}