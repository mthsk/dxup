#pragma once
// Portable subset of D3D9 Flexible Vertex Format layout rules. FVF bit and
// declaration-type values correspond to d3d9types.h / d3d9.h.
#include <cstdint>
#include <vector>

namespace dxup { namespace fvf {
  struct Element {
    uint16_t offset;
    uint8_t type;      // D3DDECLTYPE
    uint8_t usage;     // D3DDECLUSAGE
    uint8_t index;
  };
  struct Layout {
    std::vector<Element> elements;
    uint16_t stride = 0;
    bool transformed = false;
    bool hasDiffuse = false;
    bool hasTexcoord0 = false;
  };

  inline bool parse(uint32_t fvf, Layout& out) {
    Layout result;
    const uint32_t position = fvf & 0x400Eu;
    // XYZ, XYZRHW, and XYZW do not need unsupported blend-weight semantics.
    if (position != 0x002u && position != 0x004u && position != 0x4002u)
      return false;
    if ((fvf & 0xb001u) != 0) // LASTBETA flags and reserved FVF bits
      return false;
    result.transformed = (position == 0x004u);
    if (result.transformed && (fvf & 0x010u)) // RHW vertices cannot have normals
      return false;
    const uint32_t texCount = (fvf >> 8) & 0x0fu;
    if (texCount > 8)
      return false;
    auto add = [&](uint8_t type, uint8_t usage, uint8_t index, uint16_t bytes) {
      result.elements.push_back({result.stride, type, usage, index});
      result.stride = static_cast<uint16_t>(result.stride + bytes);
    };
    add(position == 0x002u ? 2 : 3, 0, 0, position == 0x002u ? 12 : 16);
    if (fvf & 0x010u) add(2, 3, 0, 12); // NORMAL float3
    if (fvf & 0x020u) add(0, 4, 0, 4);  // PSIZE float
    if (fvf & 0x040u) { add(4, 10, 0, 4); result.hasDiffuse = true; }
    if (fvf & 0x080u) add(4, 10, 1, 4); // SPECULAR D3DCOLOR
    for (uint8_t i = 0; i < texCount; ++i) {
      const uint32_t encoded = (fvf >> (16 + i * 2)) & 3u;
      const uint8_t components = encoded == 0 ? 2 : encoded == 1 ? 3 : encoded == 2 ? 4 : 1;
      add(static_cast<uint8_t>(components - 1), 5, i, static_cast<uint16_t>(4 * components));
      if (i == 0) result.hasTexcoord0 = true;
    }
    out = result;
    return true;
  }
}}
