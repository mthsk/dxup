#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstring>

namespace dxup {

  template <typename T>
  void InitReturnPtr(T* ptr) {
    if (ptr)
      *ptr = nullptr;
  }

  template <typename T, typename J>
  void pushObject(std::vector<J>& vec, T& thing) {
    J* ptrThing = (J*)&thing;
    for (J i = 0; i < sizeof(T) / sizeof(J); i++)
      vec.push_back(*(ptrThing + i));
  }

  template <typename T>
  T* lastPtr(std::vector<T>& vec) {
    return &vec[vec.size() - 1];
  }

  template <typename T>
  T* nextPtr(std::vector<T>& vec) {
    return lastPtr(vec) + 1;
  }

  template <typename T>
  constexpr T alignTo(T num, T align) {
    return ((num + align - 1) / align) * align;
  }

  template <typename T>
  constexpr T alignDown(T num, T align) {
    return (num / align) * align;
  }

  template <typename T>
  void pushAlignedString(std::vector<T>& vec, const char* str, size_t len) {
    const size_t paddedLen = alignTo(len + 1, sizeof(T));

    // Resizing may reallocate the vector: do not retain a pointer acquired
    // before resize. Write the string and terminator into the new storage.
    const size_t firstElement = vec.size();
    vec.resize(firstElement + paddedLen / sizeof(T));
    unsigned char* dest = reinterpret_cast<unsigned char*>(vec.data() + firstElement);
    std::memset(dest, 0xAB, paddedLen); // FXC padding
    if (len != 0)
      std::memcpy(dest, str, len);
    dest[len] = 0;
  }

  template <typename T>
  void pushAlignedString(std::vector<T>& vec, const std::string& str) {
    pushAlignedString(vec, str.c_str(), str.length());
  }

  template <typename T, typename J>
  void arrayCopyT(T* dst, const J* src, size_t tCount) {
    std::memcpy((void*)dst, (const void*)src, sizeof(T) * tCount);
  }

  template <typename T, typename J>
  void arrayCopyJ(T* dst, const J* src, size_t jCount) {
    std::memcpy((void*)dst, (const void*)src, sizeof(J) * jCount);
  }

}