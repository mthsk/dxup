#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace dxup {

  // D3D9 supplies depth bias in normalized depth coordinates. D3D11 accepts
  // an integer number of depth-buffer units instead. For UNORM depth buffers
  // the constant bias is DepthBias * 2^(-depthBits).
  inline int32_t depthBiasToD3D11(float bias, unsigned depthBits) {
    if (!std::isfinite(bias) || depthBits == 0 || depthBits > 30)
      return 0;

    const double scaled = std::round(std::ldexp(static_cast<double>(bias), depthBits));
    if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
      return std::numeric_limits<int32_t>::max();
    if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
      return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(scaled);
  }

}
