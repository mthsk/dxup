#pragma once
// Baseline, intentionally scoped fixed-function shader generators.
// Supports the first texture stage, directional/point/spot fixed-function
// lighting, D3D9 material color sources, additive specular and vertex fog.
// Multi-stage combiners and table fog remain unsupported.
// Both shaders are SM4 and compiled at runtime through d3dcompiler.dll.
#include <cstdint>
#include <string>

namespace dxup { namespace fixed_function {
  // VS layout key: pretransformed / diffuse / texcoord0 / XYZW / normal / specular.
  inline uint32_t vertexKey(uint32_t fvf) {
    return ((fvf & 0x400Eu) == 0x004u ? 1u : 0u)
       | ((fvf & 0x040u) ? 2u : 0u)
       | ((fvf & 0x0f00u) ? 4u : 0u)
       | ((fvf & 0x400Eu) == 0x4002u ? 8u : 0u)
       | ((fvf & 0x010u) ? 16u : 0u)
       | ((fvf & 0x080u) ? 32u : 0u);
  }

  inline std::string vertexSource(uint32_t key) {
    std::string source = R"HLSL(
cbuffer FFVertexConstants : register(b1) {
  row_major float4x4 mWorldViewProjection;
  float4 mViewport; // x, y, width, height
  float4 mMaterialDiffuse;
  row_major float4x4 mWorld;
  row_major float4x4 mView;
  float4 mMaterialAmbient;
  float4 mMaterialEmissive;
  float4 mGlobalAmbient;
  float4 mLightingSettings; // x enabled, y light count, z diffuse from COLOR0, w ambient from COLOR0
  float4 mFogSettings; // x start, y end, z density, w D3DFOG mode
  float4 mLightDirection[8];
  float4 mLightDiffuse[8];
  float4 mLightAmbient[8];
  float4 mEmissiveControl; // x: emissive from COLOR0 if enabled
  float4 mLightPosition[8];     // xyz position in world space, w range
  float4 mLightAttenuation[8];  // att0, att1, att2, falloff
  float4 mLightSpotAngles[8];   // cos(theta/2), cos(phi/2), reserved
  float4 mMaterialSpecular;
  float4 mSpecularSettings; // x specular enabled, y material source, z power, w local viewer
  float4 mLightSpecular[8];
  row_major float4x4 mNormalWorldView;
  float4 mNormalSettings; // x: normalize transformed normals
};
float4 selectMaterial(float source, float4 materialColor, float4 diffuseColor, float4 specularColor) {
  if (source > 1.5) return specularColor;
  if (source > 0.5) return diffuseColor;
  return materialColor;
}
struct VSInput { float4 position : POSITION0;
)HLSL";
    if (key & 2u) source += "  float4 diffuse : COLOR0;\n";
    if (key & 16u) source += "  float3 normal : NORMAL0;\n";
    if (key & 32u) source += "  float4 specular : COLOR1;\n";
    if (key & 4u) source += "  float4 texcoord : TEXCOORD0;\n";
    source += R"HLSL(};
struct VSOutput { float4 position : SV_Position; float4 diffuse : COLOR0; float4 specular : COLOR1; float2 texcoord : TEXCOORD0; float fog : TEXCOORD7; };
VSOutput main(VSInput input) {
  VSOutput output;
)HLSL";
    if (key & 1u) {
      // D3D9 XYZRHW is post-transform in pixel coordinates. Undo the
      // viewport transform and restore perspective-correct interpolation.
      source += R"HLSL(
  float2 ndc = (input.position.xy - mViewport.xy) / mViewport.zw;
  ndc = float2(ndc.x * 2.0 - 1.0, 1.0 - ndc.y * 2.0);
  float reciprocalW = max(abs(input.position.w), 1.0e-20);
  output.position = float4(ndc, input.position.z, 1.0) / reciprocalW;
)HLSL";
    } else if (key & 8u) {
      source += "  output.position = mul(input.position, mWorldViewProjection);\n";
    } else {
      source += "  output.position = mul(float4(input.position.xyz, 1.0), mWorldViewProjection);\n";
    }
    source += (key & 2u) ? "  output.diffuse = input.diffuse;\n" : "  output.diffuse = mMaterialDiffuse;\n";
    // Unlit FVF specular RGB is still used when SPECULARENABLE is true.
    source += (key & 32u) ? "  output.specular = input.specular;\n" :
                             "  output.specular = float4(0,0,0,1);\n";
    source += "  float4 vertexDiffuse = output.diffuse;\n";
    source += "  float4 vertexSpecular = output.specular;\n";
    if (!(key & 1u)) {
      // Without D3DFVF_NORMAL D3D9 uses a zero normal-light dot product.
      // Ambient and emissive terms still contribute; the draw must not be rejected.
      if (key & 16u) {
        source += "  float3 normalView = mul(float4(input.normal, 0.0), mNormalWorldView).xyz;\n";
        source += "  if (mNormalSettings.x > 0.5) normalView = normalize(normalView);\n";
      }
      source += R"HLSL(
  if (mLightingSettings.x > 0.5) {
    float3 worldPosition = mul(float4(input.position.xyz, 1.0), mWorld).xyz;
    float4 matDiffuse = selectMaterial(mLightingSettings.z, mMaterialDiffuse, vertexDiffuse, vertexSpecular);
    float4 matAmbient = selectMaterial(mLightingSettings.w, mMaterialAmbient, vertexDiffuse, vertexSpecular);
    float3 emissive = selectMaterial(mEmissiveControl.x, mMaterialEmissive, vertexDiffuse, vertexSpecular).rgb;
    float4 matSpecular = selectMaterial(mSpecularSettings.y, mMaterialSpecular, vertexDiffuse, vertexSpecular);
    float3 lit = emissive + matAmbient.rgb * mGlobalAmbient.rgb;
    float3 specularLit = float3(0,0,0);
    float3 viewPosition = mul(float4(worldPosition, 1.0), mView).xyz;
    float3 toViewer = mSpecularSettings.w > 0.5 ? normalize(-viewPosition) : float3(0,0,-1);
    [loop] for (int i = 0; i < (int)mLightingSettings.y; ++i) {
      float3 toLight = -normalize(mLightDirection[i].xyz);
      float attenuation = 1.0;
      if (mLightDirection[i].w < 2.5) {
        float3 delta = mLightPosition[i].xyz - worldPosition;
        float dist = length(delta);
        toLight = delta / max(dist, 1.0e-8);
        attenuation = 0.0;
        if (dist <= mLightPosition[i].w) {
          float4 att = mLightAttenuation[i];
          attenuation = 1.0 / max(att.x + att.y * dist + att.z * dist * dist, 1.0e-8);
          if (mLightDirection[i].w > 1.5) {
            float rho = dot(normalize(mLightDirection[i].xyz), -toLight);
            float innerCone = mLightSpotAngles[i].x;
            float outerCone = mLightSpotAngles[i].y;
            float cone = rho >= innerCone ? 1.0 :
              (rho <= outerCone ? 0.0 :
                pow(saturate((rho - outerCone) / max(innerCone - outerCone, 1.0e-8)), max(att.w, 0.0)));
            attenuation *= cone;
          }
        }
      }
      float3 lightView = normalize(mul(float4(toLight, 0.0), mView).xyz);
      float lambert = 0.0;
)HLSL";
      if (key & 16u) source += "      lambert = saturate(dot(normalView, lightView));\n";
      source += R"HLSL(
      lit += attenuation * (matAmbient.rgb * mLightAmbient[i].rgb +
        matDiffuse.rgb * mLightDiffuse[i].rgb * lambert);
      if (mSpecularSettings.x > 0.5 && lambert > 0.0) {
        float3 halfway = normalize(lightView + toViewer);
        float reflection = 0.0;
        // With no NORMAL, D3D9 generates no diffuse or specular highlight.
)HLSL";
      if (key & 16u) source += "        reflection = saturate(dot(normalView, halfway));\n";
      source += R"HLSL(
        if (reflection > 0.0)
          specularLit += attenuation * mLightSpecular[i].rgb * pow(reflection, max(mSpecularSettings.z, 0.0));
      }
    }
    output.diffuse = float4(saturate(lit), matDiffuse.a);
    output.specular = float4(saturate(matSpecular.rgb * specularLit), 1.0);
  }
)HLSL";
    }
    // Fog is interpolated, so the vertex shader computes its factor.
    // Pretransformed vertices conventionally carry it in specular alpha.
    if (key & 1u) {
      source += (key & 32u) ? "  float fogDistance = input.specular.a;\n"
                            : "  float fogDistance = 1.0;\n";
      source += "  output.fog = fogDistance;\n";
    } else {
      source += R"HLSL(
  float fogDistance = mul(mul(float4(input.position.xyz, 1.0), mWorld), mView).z;
  float fogFactor = 1.0;
  if (mFogSettings.w == 3.0) fogFactor = saturate((mFogSettings.y - fogDistance) / max(mFogSettings.y - mFogSettings.x, 1.0e-8));
  if (mFogSettings.w == 1.0) fogFactor = saturate(exp(-max(fogDistance, 0.0) * mFogSettings.z));
  if (mFogSettings.w == 2.0) { float d = max(fogDistance, 0.0) * mFogSettings.z; fogFactor = saturate(exp(-d*d)); }
  output.fog = fogFactor;
)HLSL";
    }
    source += (key & 4u) ? "  output.texcoord = input.texcoord.xy;\n" : "  output.texcoord = float2(0, 0);\n";
    source += "  return output;\n}\n";
    return source;
  }

  // Packed operations are D3DTOP_*, arguments D3DTA_*, and tests D3DCMP_*.
  // C++ validates the supported subset before binding this shader.
  inline const char* pixelSource() {
    return R"HLSL(
Texture2D tex0 : register(t0);
SamplerState sampler0 : register(s0);
cbuffer FFPixelConstants : register(b1) {
  float4 textureFactor;
  uint4 colorConfig; // op, arg1, arg2, hasTexture
  uint4 alphaConfig; // op, arg1, arg2, alphaTestEnabled
  uint4 testConfig;  // alphaCompare, alphaRef, fogEnable, reserved
  float4 fogColor;
  uint4 specularControl; // x D3DRS_SPECULARENABLE, other components reserved
};
struct PSInput { float4 position : SV_Position; float4 diffuse : COLOR0; float4 specular : COLOR1; float2 texcoord : TEXCOORD0; float fog : TEXCOORD7; };
float4 selectArgument(uint source, float4 current, float4 diffuse, float4 textureColor, float4 specular) {
  float4 value = current;
  if ((source & 15u) == 0u) value = diffuse;
  if ((source & 15u) == 2u) value = textureColor;
  if ((source & 15u) == 3u) value = textureFactor;
  if ((source & 15u) == 4u) value = specular;
  if ((source & 16u) != 0u) value = 1.0 - value;
  if ((source & 32u) != 0u) value = value.aaaa;
  return value;
}
float4 combine(uint operation, float4 a, float4 b) {
  if (operation == 2u) return a; // SELECTARG1
  if (operation == 3u) return b; // SELECTARG2
  if (operation == 4u) return a * b; // MODULATE
  if (operation == 5u) return saturate(a * b * 2.0);
  if (operation == 6u) return saturate(a * b * 4.0);
  if (operation == 7u) return saturate(a + b); // ADD
  if (operation == 10u) return saturate(a - b); // SUBTRACT
  return a;
}
float4 main(PSInput input) : SV_Target {
  float4 texel = float4(1,1,1,1);
  if (colorConfig.w != 0u) texel = tex0.Sample(sampler0, input.texcoord);
  float4 color = input.diffuse;
  if (colorConfig.x != 1u) {
    float4 a = selectArgument(colorConfig.y, color, input.diffuse, texel, input.specular);
    float4 b = selectArgument(colorConfig.z, color, input.diffuse, texel, input.specular);
    color.rgb = combine(colorConfig.x, a, b).rgb;
  }
  if (alphaConfig.x != 1u) {
    float4 a = selectArgument(alphaConfig.y, input.diffuse, input.diffuse, texel, input.specular);
    float4 b = selectArgument(alphaConfig.z, input.diffuse, input.diffuse, texel, input.specular);
    color.a = combine(alphaConfig.x, a, b).a;
  }
  if (alphaConfig.w != 0u) {
    float reference = testConfig.y / 255.0;
    if (testConfig.x == 1u) discard;
    if (testConfig.x == 2u && !(color.a < reference)) discard;
    if (testConfig.x == 3u && !(abs(color.a - reference) < 0.5 / 255.0)) discard;
    if (testConfig.x == 4u && !(color.a <= reference)) discard;
    if (testConfig.x == 5u && !(color.a > reference)) discard;
    if (testConfig.x == 6u && !(abs(color.a - reference) >= 0.5 / 255.0)) discard;
    if (testConfig.x == 7u && !(color.a >= reference)) discard;
  }
  // In the D3D9 fixed-function pipeline specular RGB is added after texture
  // stages, before fog, rather than modulating the texture as diffuse does.
  if (specularControl.x != 0u) color.rgb = saturate(color.rgb + input.specular.rgb);
  if (testConfig.z != 0u) color.rgb = lerp(fogColor.rgb, color.rgb, saturate(input.fog));
  return saturate(color);
}
)HLSL";
  }

  inline bool operationNeedsTexture(unsigned op, unsigned arg1, unsigned arg2) {
    if (op == 1u) return false; // DISABLE
    if (op == 2u) return (arg1 & 15u) == 2u; // SELECTARG1
    if (op == 3u) return (arg2 & 15u) == 2u; // SELECTARG2
    return (arg1 & 15u) == 2u || (arg2 & 15u) == 2u;
  }

  inline bool supportedColorOp(unsigned op) {
    return op == 1 || op == 2 || op == 3 || op == 4 || op == 5 ||
           op == 6 || op == 7 || op == 10;
  }
  inline bool supportedArgument(unsigned arg) {
    return (arg & ~0x3fu) == 0 && ((arg & 0xfu) <= 4u);
  }
}}
