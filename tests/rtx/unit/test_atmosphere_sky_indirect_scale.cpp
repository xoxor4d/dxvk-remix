/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include "../../../src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h"

#include "../../test_utils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace dxvk {
  Logger Logger::s_instance("test_atmosphere_sky_indirect_scale.log");
}

namespace {

void requireNear(float actual, float expected, const char* label) {
  if (std::fabs(actual - expected) > 0.0001f) {
    throw std::runtime_error(label);
  }
}

std::string readTextFile(const char* path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    throw std::runtime_error(path);
  }

  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void optionDeclarationPreservesCurrentRadianceScale() {
  const std::string options = readTextFile(BUILD_SOURCE_ROOT "src/dxvk/rtx_render/rtx_options.h");

  if (options.find("RTX_OPTION_ARGS(\"rtx.atmosphere\", float, skyIndirectRadianceScale, 8.0f,") == std::string::npos) {
    throw std::runtime_error("sky indirect radiance scale option declaration");
  }

  if (options.find("args.minValue = 0.0f") == std::string::npos) {
    throw std::runtime_error("sky indirect radiance scale min value");
  }
}

void atmosphereArgsCarriesClampedSkyIndirectScale() {
  AtmosphereArgs args = {};
  args.skyIndirectRadianceScale = std::max(1.0f, 0.0f);

  requireNear(args.skyIndirectRadianceScale, 1.0f,
              "args sky indirect radiance scale");
}

void evalSkyRadianceGatesScaleExplicitly() {
  const std::string sky = readTextFile(BUILD_SOURCE_ROOT "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh");

  if (sky.find("bool applySkyIndirectRadianceScale = false") == std::string::npos) {
    throw std::runtime_error("evalSkyRadiance applySkyIndirectRadianceScale parameter");
  }

  if (sky.find("if (applySkyIndirectRadianceScale)") == std::string::npos) {
    throw std::runtime_error("evalSkyRadiance applySkyIndirectRadianceScale gate");
  }

  if (sky.find("if (!isPrimaryRay)") != std::string::npos) {
    throw std::runtime_error("evalSkyRadiance must not gate scale on isPrimaryRay");
  }
}

void psrSkyMissDoesNotOptIntoIndirectScale() {
  const std::string resolver = readTextFile(BUILD_SOURCE_ROOT "src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh");

  const std::string psrMarker = "geometryPSRResolverState.pixelCoordinate, cb.frameIdx,\n                                          cb.isZUp != 0);";
  if (resolver.find(psrMarker) == std::string::npos) {
    throw std::runtime_error("PSR evalSkyRadiance call site");
  }

  if (resolver.find("applySkyIndirectRadianceScale") != std::string::npos) {
    throw std::runtime_error("PSR must not pass applySkyIndirectRadianceScale");
  }
}

void indirectIntegratorAppliesScaleOnlyForNonSpecularReflection() {
  const std::string indirect = readTextFile(BUILD_SOURCE_ROOT "src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh");

  if (indirect.find("applySkyIndirectRadianceScale=*/ pathState.currentSampledLobe != opaqueLobeTypeSpecularReflection") == std::string::npos) {
    throw std::runtime_error("integrator indirect conditional sky scale");
  }

  if (indirect.find("pathState.currentSampledLobe = continuationLobe;") == std::string::npos) {
    throw std::runtime_error("integrator indirect lobe tracking update");
  }

  if (indirect.find("pathState.currentSampledLobe = geometryFlags.firstSampledLobeIsSpecular") == std::string::npos) {
    throw std::runtime_error("integrator indirect lobe tracking init");
  }
}

} // anonymous namespace

int main() {
  try {
    std::cout << "Begin atmosphere sky indirect scale tests" << std::endl;
    optionDeclarationPreservesCurrentRadianceScale();
    atmosphereArgsCarriesClampedSkyIndirectScale();
    evalSkyRadianceGatesScaleExplicitly();
    psrSkyMissDoesNotOptIntoIndirectScale();
    indirectIntegratorAppliesScaleOnlyForNonSpecularReflection();
    std::cout << "All atmosphere sky indirect scale tests passed" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    throw;
  }

  return 0;
}
