#pragma once

#include "d3d9_base.h"
#include "d3d9_state.h"
#include "d3d11_dynamic_buffer.h"
#include "d3d9_fixed_function_constants.h"
#include <map>
#include <vector>

namespace dxup {

  class D3D9ImmediateRenderer {

  public:

    D3D9ImmediateRenderer(ID3D11Device1* device, ID3D11DeviceContext1* context, D3D9State* state);

    HRESULT Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount);

    void undirtyContext();
    void handleDepthStencilDiscard();

    void blit(Direct3DSurface9* dst, Direct3DSurface9* src);
    void endFrame();

  private:

    HRESULT drawTriangleFan(bool indexed, UINT StartIndex, UINT PrimitiveCount, INT BaseVertexIndex,
                            const void* userIndices = nullptr, D3DFORMAT indexFormat = D3DFMT_INDEX16);

    UINT getInstanceCount() const;
    bool canDraw();

    bool preDraw(); // Returns CanDraw
    void postDraw();

    std::array<UINT, 16> getInstanceStepRates() const;
    void updateViewport();
    void updateScissorRect();
    void updateVertexShaderAndInputLayout();
    bool updateFixedFunctionVertexShader();
    bool updateFixedFunctionConstants();
    bool fixedFunctionSupported() const;
    void updateDepthStencilState();
    void updateRasterizer();
    void updateBlendState();
    void updateSampler(uint32_t sampler);
    void updateSamplers();
    void updateTextures();
    void updateRenderTargets();
    void updatePixelShader();
    void updateVertexBuffer();
    void updateIndexBuffer();
    void updateVertexConstants();
    void updatePixelConstants();

    ID3D11Device1* m_device;
    ID3D11DeviceContext1* m_context;
    D3D9State* m_state;

    D3D11DynamicBuffer m_upVertexBuffer;
    D3D11DynamicBuffer m_upIndexBuffer;
    D3D11DynamicBuffer m_fanIndexBuffer;

    D3D9ConstantBuffer<false> m_vsConstants;
    D3D9ConstantBuffer<true> m_psConstants;

    D3D9StateCaches m_caches;

    struct FFVertexProgram {
      Com<ID3D11VertexShader> shader;
      std::vector<uint8_t> bytecode;
    };
    std::map<uint32_t, FFVertexProgram> m_ffVertexShaders;
    ComPrivate<Direct3DVertexDeclaration9> m_ffInputDecl;
    Com<ID3D11InputLayout> m_ffInputLayout;
    Com<ID3D11PixelShader> m_ffPixelShader;
    Com<ID3D11Buffer> m_ffVertexBuffer;
    Com<ID3D11Buffer> m_ffPixelBuffer;
    uint32_t m_ffInputKey = 0xffffffffu;
    std::array<UINT, 16> m_ffInstanceStepRates = {};

    Com<ID3D11SamplerState> m_blitSampler;
    Com<ID3D11VertexShader> m_blitVS;
    Com<ID3D11PixelShader> m_blitPS;
  };

}