#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
namespace dxup { namespace trianglefan {
  inline bool expand(uint32_t triangleCount, const void* indices, uint32_t indexCount,
                     uint32_t firstIndex, bool indices32, std::vector<uint32_t>& out) {
    if (triangleCount > std::numeric_limits<uint32_t>::max() / 3u ||
        triangleCount > std::numeric_limits<uint32_t>::max() - 2u)
      return false;
    const uint32_t vertices = triangleCount + 2u;
    if (indices && (firstIndex > indexCount || vertices > indexCount - firstIndex))
      return false;
    std::vector<uint32_t> result(static_cast<size_t>(triangleCount) * 3);
    auto getIndex = [&](uint32_t index) {
      if (!indices) return index;
      uint32_t value = 0;
      const auto* data = static_cast<const uint8_t*>(indices) +
        static_cast<size_t>(index + firstIndex) * (indices32 ? 4u : 2u);
      if (indices32) std::memcpy(&value, data, sizeof(value));
      else { uint16_t narrow; std::memcpy(&narrow, data, sizeof(narrow)); value = narrow; }
      return value;
    };
    for (uint32_t i = 0; i < triangleCount; ++i) {
      result[3u * i] = getIndex(0);
      result[3u * i + 1u] = getIndex(i + 1u);
      result[3u * i + 2u] = getIndex(i + 2u);
    }
    out.swap(result);
    return true;
  }
}}
