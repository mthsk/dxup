#pragma once
// Mirror of HLSL cbuffer(b1) layouts in d3d9_fixed_function.h.
// Portable so the memory layout can be compiled/tested without the Windows SDK.
#include <cstddef>
#include <cstdint>

namespace dxup {
  struct alignas(16) FFVertexConstants {
    float worldViewProjection[4][4];
    float viewport[4];
    float materialDiffuse[4];
    float world[4][4];
    float view[4][4];
    float materialAmbient[4];
    float materialEmissive[4];
    float globalAmbient[4];
    float lightingSettings[4];
    float fogSettings[4];
    float lightDirection[8][4];
    float lightDiffuse[8][4];
    float lightAmbient[8][4];
    float emissiveControl[4];
    float lightPosition[8][4];
    float lightAttenuation[8][4];
    float lightSpotAngles[8][4];
    // Appended to retain the established point/spot-light constant offsets.
    float materialSpecular[4];
    float specularSettings[4]; // enabled, source, power, local viewer
    float lightSpecular[8][4];
    float normalWorldView[4][4];
    float normalSettings[4]; // x: D3DRS_NORMALIZENORMALS
  };
  struct alignas(16) FFPixelConstants {
    float textureFactor[4];
    uint32_t colorConfig[4];
    uint32_t alphaConfig[4];
    uint32_t testConfig[4];
    float fogColor[4];
    uint32_t specularControl[4]; // additive specular enabled, reserved
    uint32_t colorConfig1[4]; // second stage op, arg1, arg2, has texture | coordinate selector
    uint32_t alphaConfig1[4]; // second stage alpha op, arg1, arg2, reserved
  };
  static_assert(sizeof(FFVertexConstants) == 1328, "HLSL vertex constant layout mismatch");
  static_assert(sizeof(FFPixelConstants) == 128, "HLSL pixel constant layout mismatch");
  static_assert(offsetof(FFVertexConstants, emissiveControl) == 688, "HLSL emissive offset mismatch");
  static_assert(offsetof(FFVertexConstants, lightDirection) == 304, "HLSL light offset mismatch");
  static_assert(offsetof(FFVertexConstants, lightPosition) == 704, "HLSL light position offset mismatch");
  static_assert(offsetof(FFVertexConstants, lightAttenuation) == 832, "HLSL attenuation offset mismatch");
  static_assert(offsetof(FFVertexConstants, lightSpotAngles) == 960, "HLSL spotlight offset mismatch");
  static_assert(offsetof(FFPixelConstants, fogColor) == 64, "HLSL fog offset mismatch");
  static_assert(offsetof(FFVertexConstants, materialSpecular) == 1088, "HLSL specular material offset mismatch");
  static_assert(offsetof(FFVertexConstants, specularSettings) == 1104, "HLSL specular settings offset mismatch");
  static_assert(offsetof(FFVertexConstants, lightSpecular) == 1120, "HLSL light specular offset mismatch");
  static_assert(offsetof(FFPixelConstants, specularControl) == 80, "HLSL specular control offset mismatch");
  static_assert(offsetof(FFPixelConstants, colorConfig1) == 96, "HLSL second stage color offset mismatch");
  static_assert(offsetof(FFPixelConstants, alphaConfig1) == 112, "HLSL second stage alpha offset mismatch");
  static_assert(offsetof(FFVertexConstants, normalWorldView) == 1248, "HLSL normal matrix offset mismatch");
  static_assert(offsetof(FFVertexConstants, normalSettings) == 1312, "HLSL normal controls offset mismatch");
}
