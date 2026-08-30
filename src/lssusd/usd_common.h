#include "../dxvk/rtx_render/rtx_types.h"
#include "remix_category_names.h"

namespace dxvk {
  // Single source of truth mapping each InstanceCategories value to its USD
  // attribute, schema display label, and (optionally) the RtxOption whose
  // description documents it. getInstanceCategorySubKey() and the exported
  // schema.usda are both derived from this table.
  struct RemixCategoryEntry {
    InstanceCategories category;
    const char* attr;        // USD attribute, e.g. "remix_category:world_ui"
    const char* displayName; // Schema UI label, e.g. "World UI"
    const char* optionName;  // RtxOption full name documenting this category, or nullptr
  };

  static constexpr RemixCategoryEntry kRemixCategoryEntries[] = {
    { InstanceCategories::WorldUI,                 "remix_category:world_ui",                 "World UI",                  "rtx.worldSpaceUiTextures" },
    { InstanceCategories::WorldMatte,              "remix_category:world_matte",              "World Matte",               "rtx.worldSpaceUiBackgroundTextures" },
    { InstanceCategories::Sky,                     "remix_category:sky",                      "Sky",                       "rtx.skyBoxTextures" },
    { InstanceCategories::Ignore,                  "remix_category:ignore",                   "Ignore",                    "rtx.ignoreTextures" },
    { InstanceCategories::IgnoreLights,            "remix_category:ignore_lights",            "Ignore Lights",             "rtx.ignoreLights" },
    { InstanceCategories::IgnoreAntiCulling,       "remix_category:ignore_anti_culling",      "Ignore Anti Culling",       "rtx.antiCulling.antiCullingTextures" },
    { InstanceCategories::IgnoreMotionBlur,        "remix_category:ignore_motion_blur",       "Ignore Motion Blur",        "rtx.postfx.motionBlurMaskOutTextures" },
    { InstanceCategories::IgnoreOpacityMicromap,   "remix_category:ignore_opacity_micromap",  "Ignore Opacity Micromap",   "rtx.opacityMicromapIgnoreTextures" },
    { InstanceCategories::IgnoreAlphaChannel,      "remix_category:ignore_alpha_channel",     "Ignore Alpha Channel",      "rtx.ignoreAlphaOnTextures" },
    { InstanceCategories::Hidden,                  "remix_category:hidden",                   "Hidden",                    "rtx.hideInstanceTextures" },
    { InstanceCategories::Particle,                "remix_category:particle",                 "Particle",                  "rtx.particleTextures" },
    { InstanceCategories::Beam,                    "remix_category:beam",                     "Beam",                      "rtx.beamTextures" },
    { InstanceCategories::DecalStatic,             "remix_category:decal_Static",             "Decal Static",              "rtx.decalTextures" },
    { InstanceCategories::DecalDynamic,            "remix_category:decal_dynamic",            "Decal Dynamic",             "rtx.dynamicDecalTextures" },
    { InstanceCategories::DecalSingleOffset,       "remix_category:decal_single_offset",      "Decal Single Offset",       "rtx.singleOffsetDecalTextures" },
    { InstanceCategories::DecalNoOffset,           "remix_category:decal_no_offset",          "Decal No Offset",           "rtx.nonOffsetDecalTextures" },
    { InstanceCategories::AlphaBlendToCutout,      "remix_category:alpha_blend_to_cutout",    "Alpha Blend To Cutout",     nullptr },
    { InstanceCategories::Terrain,                 "remix_category:terrain",                  "Terrain",                   "rtx.terrainTextures" },
    { InstanceCategories::AnimatedWater,           "remix_category:animated_water",           "Animated Water",            "rtx.animatedWaterTextures" },
    { InstanceCategories::ThirdPersonPlayerModel,  "remix_category:third_person_player_model","Third Person Player Model", "rtx.playerModelTextures" },
    { InstanceCategories::ThirdPersonPlayerBody,   "remix_category:third_person_player_body", "Third Person Player Body",  "rtx.playerModelBodyTextures" },
    { InstanceCategories::IgnoreBakedLighting,     "remix_category:ignore_baked_lighting",    "Ignore Baked Lighting",     "rtx.ignoreBakedLightingTextures" },
    { InstanceCategories::ParticleEmitter,         "remix_category:particle_emitter",         "Particle Emitter",          "rtx.particleEmitterTextures" },
    { InstanceCategories::SmoothNormals,           "remix_category:smooth_normals",           "Smooth Normals",            "rtx.smoothNormalsTextures" },
    { InstanceCategories::HairCards,               "remix_category:hair_cards",               "Hair Cards",                "rtx.hairCardTextures" },
    { InstanceCategories::DisableBackfaceCulling,  "remix_category:disable_backface_culling", "Disable Backface Culling",  "rtx.disableBackfaceCullingTextures" },
  };

  // Table must have one entry per enum value.
  static_assert(sizeof(kRemixCategoryEntries) / sizeof(kRemixCategoryEntries[0]) == (size_t) InstanceCategories::Count,
                "Please add/remove the category to kRemixCategoryEntries above.");

  // Used when reading/writing with Remix USD mods.
  static const char* getInstanceCategorySubKey(InstanceCategories cat) {
    if (cat >= InstanceCategories::Count) {
      return "";
    }
    return kRemixCategoryNames[(uint32_t) cat];
  }
}
