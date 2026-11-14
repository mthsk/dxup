#pragma once

// Small, platform-independent D3D9 mipmap policy helpers. Shared with the
// source-level conformance tests (no Windows SDK needed for these functions).
#include <algorithm>
#include <cstdint>

namespace dxup { namespace mipmap {

  inline uint32_t clampLOD(uint32_t requestedLOD, uint32_t levelCount) {
    return std::min(requestedLOD, levelCount ? levelCount - 1 : 0);
  }

  // D3DSAMP_MAXMIPLEVEL is the smallest *index* accessible to the sampler,
  // not the maximum LOD. D3D11 expresses this as the sampler's MinLOD.
  inline float minSamplerLOD(uint32_t maxMipLevel, uint32_t textureLOD) {
    return static_cast<float>(std::max(maxMipLevel, textureLOD));
  }

} }
