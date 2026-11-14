#include "d3d9_renderer.h"
#include <cmath>
#include "d3d9_texture.h"
#include "d3d9_mipmap.h"
#include "d3d9_matrix_math.h"
#include "d3d9_depth_bias.h"
#include "d3d9_fixed_function.h"
#include "d3d9_trianglefan.h"
#include "../util/d3dcompiler_helpers.h"
#include "d3d9_util.h"
#include "shaders/blit.vs.dxbc.h"
#include "shaders/blit.ps.dxbc.h"

#include <cfloat>
#include <vector>
#include <string>
#include <cstring>
#include <limits>
#include <cstdlib>

namespace dxup {

  D3D9ImmediateRenderer::D3D9ImmediateRenderer(ID3D11Device1* device, ID3D11DeviceContext1* context, D3D9State* state)
    : m_device{ device }
    , m_context{ context }
    , m_state{ state }
    , m_upVertexBuffer{ device, D3D11_BIND_VERTEX_BUFFER }
    , m_upIndexBuffer{ device, D3D11_BIND_INDEX_BUFFER }
    , m_fanIndexBuffer{ device, D3D11_BIND_INDEX_BUFFER }
    , m_vsConstants{ device, context }
    , m_psConstants{ device, context } {

    D3D11_SAMPLER_DESC blitSampler;
    blitSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    blitSampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    blitSampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    blitSampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    blitSampler.MipLODBias = 0;
    blitSampler.MaxAnisotropy = 1;
    blitSampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    blitSampler.BorderColor[0] = 1.0f;
    blitSampler.BorderColor[1] = 1.0f;
    blitSampler.BorderColor[2] = 1.0f;
    blitSampler.BorderColor[3] = 1.0f;
    blitSampler.MinLOD = -FLT_MAX;
    blitSampler.MaxLOD = FLT_MAX;
    HRESULT result = m_device->CreateSamplerState(&blitSampler, &m_blitSampler);
    if (FAILED(result))
      log::warn("D3D9ImmediateRenderer: failed to create blit sampler state.");

    D3D11_BUFFER_DESC ffBufferDesc = {};
    ffBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    ffBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ffBufferDesc.ByteWidth = sizeof(FFVertexConstants);
    result = m_device->CreateBuffer(&ffBufferDesc, nullptr, &m_ffVertexBuffer);
    if (FAILED(result)) log::warn("Failed to allocate fixed-function VS constants.");
    ffBufferDesc.ByteWidth = sizeof(FFPixelConstants);
    result = m_device->CreateBuffer(&ffBufferDesc, nullptr, &m_ffPixelBuffer);
    if (FAILED(result)) log::warn("Failed to allocate fixed-function PS constants.");

    result = m_device->CreateVertexShader(g_blit_vs, sizeof(g_blit_vs), nullptr, &m_blitVS);
    if (FAILED(result))
      log::warn("D3D9ImmediateRenderer: failed to create blit vs.");

    result = m_device->CreatePixelShader(g_blit_ps, sizeof(g_blit_ps), nullptr, &m_blitPS);
    if (FAILED(result))
      log::warn("D3D9ImmediateRenderer: failed to create blit ps.");
  }

  void D3D9ImmediateRenderer::endFrame() {

    m_fanIndexBuffer.endFrame();
    m_upIndexBuffer.endFrame();
    m_upVertexBuffer.endFrame();

    m_vsConstants.endFrame();
    m_psConstants.endFrame();
  }

  HRESULT D3D9ImmediateRenderer::Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {

    // D3D9 supports rectangle-scoped clears. D3D11 ClearRenderTargetView does
    // not, but ID3D11DeviceContext1::ClearView does for color render targets.
    // Do not silently clear the *entire* depth/stencil buffer when rectangles
    // were requested: depth/stencil rectangle clears need a separate draw path.
    if (Count && !pRects)
      return log::d3derr(D3DERR_INVALIDCALL, "Clear: Count > 0, pRects is nullptr.");

    // A one-rect full-surface clear can still take the fast D3D11 depth/stencil
    // path. Check the target dimensions, not just the viewport size.
    bool fullSurfaceRect = false;
    if (Count == 1 && m_state->renderTargets[0] != nullptr) {
      D3DSURFACE_DESC target = {};
      if (SUCCEEDED(m_state->renderTargets[0]->GetDesc(&target)))
        fullSurfaceRect = pRects[0].x1 == 0 && pRects[0].y1 == 0 &&
          pRects[0].x2 == static_cast<LONG>(target.Width) &&
          pRects[0].y2 == static_cast<LONG>(target.Height);
    }
    const bool partialClear = Count != 0 && !fullSurfaceRect;
    if (partialClear && (Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))) {
      log::warn("Clear: partial depth/stencil clear is not implemented.");
      return D3DERR_INVALIDCALL;
    }

    std::vector<D3D11_RECT> rects;
    if (partialClear) {
      rects.reserve(Count);
      for (DWORD i = 0; i < Count; ++i) {
        const D3DRECT& r = pRects[i];
        if (r.x2 <= r.x1 || r.y2 <= r.y1)
          continue;
        rects.push_back({ r.x1, r.y1, r.x2, r.y2 });
      }
      if (rects.empty())
        return D3D_OK;
    }

    FLOAT color[4];
    convert::color(Color, color);

    if (config::getBool(config::RandomClearColour)) {
      for (uint32_t i = 0; i < 4; i++)
        color[i] = ((float)(rand() % 255)) / 255.0f;
    }

    if (Flags & D3DCLEAR_TARGET) {
      for (uint32_t i = 0; i < 4; i++)
      {
        if (m_state->renderTargets[i] == nullptr)
          continue;

        ID3D11RenderTargetView* rtv = m_state->renderTargets[i]->GetD3D11RenderTarget(m_state->renderState[D3DRS_SRGBWRITEENABLE] == TRUE);
        if (rtv) {
          if (partialClear)
            m_context->ClearView(rtv, color, rects.data(), static_cast<UINT>(rects.size()));
          else
            m_context->ClearRenderTargetView(rtv, color);
        }
      }
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (m_state->depthStencil != nullptr)
      dsv = m_state->depthStencil->GetD3D11DepthStencil();

    if ((Flags & D3DCLEAR_STENCIL || Flags & D3DCLEAR_ZBUFFER) && dsv != nullptr) {
      uint32_t clearFlags = Flags & D3DCLEAR_STENCIL ? D3D11_CLEAR_STENCIL : 0;
      clearFlags |= Flags & D3DCLEAR_ZBUFFER ? D3D11_CLEAR_DEPTH : 0;

      m_context->ClearDepthStencilView(dsv, clearFlags, std::clamp(Z, 0.0f, 1.0f), Stencil);
    }
    return D3D_OK;
  }

  HRESULT D3D9ImmediateRenderer::drawTriangleFan(bool indexed, UINT StartIndex,
      UINT PrimitiveCount, INT BaseVertexIndex, const void* userIndices, D3DFORMAT indexFormat) {
    const void* indices = userIndices;
    uint32_t indexCount = PrimitiveCount + 2u;
    bool indices32 = indexFormat == D3DFMT_INDEX32;
    ID3D11Resource* staging = nullptr;
    bool mapped = false;
    if (indexed && !userIndices) {
      if (m_state->indexBuffer == nullptr) return D3DERR_INVALIDCALL;
      auto* resource = m_state->indexBuffer->GetDXUPResource();
      staging = resource->GetStaging();
      if (!staging) return D3DERR_INVALIDCALL;
      D3DINDEXBUFFER_DESC desc = {};
      if (FAILED(m_state->indexBuffer->GetDesc(&desc))) return D3DERR_INVALIDCALL;
      indices32 = desc.Format == D3DFMT_INDEX32;
      if (!indices32 && desc.Format != D3DFMT_INDEX16) return D3DERR_INVALIDCALL;
      indexCount = desc.Size / (indices32 ? 4u : 2u);
      if (StartIndex > indexCount || PrimitiveCount > indexCount - StartIndex ||
          indexCount - StartIndex < 2u) return D3DERR_INVALIDCALL;
      // Index buffer contents may have been modified on the GPU.
      m_context->CopyResource(staging, resource->GetResource());
      D3D11_MAPPED_SUBRESOURCE mappedIndices = {};
      if (FAILED(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mappedIndices)))
        return D3DERR_INVALIDCALL;
      indices = mappedIndices.pData;
      mapped = true;
    }
    std::vector<uint32_t> fan;
    const bool valid = trianglefan::expand(PrimitiveCount,
      indexed ? indices : nullptr, indexCount, indexed ? StartIndex : 0,
      indices32, fan);
    if (mapped) m_context->Unmap(staging, 0);
    if (!valid) return D3DERR_INVALIDCALL;
    if (fan.empty()) { postDraw(); return D3D_OK; }

    const UINT bytes = static_cast<UINT>(fan.size() * sizeof(uint32_t));
    m_fanIndexBuffer.reserve(bytes);
    if (!m_fanIndexBuffer.getBuffer()) return D3DERR_INVALIDCALL;
    const UINT offset = m_fanIndexBuffer.update(m_context, fan.data(), bytes);
    ID3D11Buffer* buffer = m_fanIndexBuffer.getBuffer();
    m_context->IASetIndexBuffer(buffer, DXGI_FORMAT_R32_UINT, offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT instanceCount = getInstanceCount();
    if (instanceCount)
      m_context->DrawIndexedInstanced(static_cast<UINT>(fan.size()), instanceCount, 0, BaseVertexIndex, 0);
    else
      m_context->DrawIndexed(static_cast<UINT>(fan.size()), 0, BaseVertexIndex);
    m_state->dirtyFlags |= dirtyFlags::indexBuffer;
    postDraw();
    return D3D_OK;
  }

  HRESULT D3D9ImmediateRenderer::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {

    if (!preDraw()) {
      postDraw();
      return D3DERR_INVALIDCALL; // Unsupported shader/declaration combination.
    }

    if (PrimitiveType == D3DPT_TRIANGLEFAN)
      return this->drawTriangleFan(false, 0, PrimitiveCount, StartVertex);

    D3D_PRIMITIVE_TOPOLOGY topology;
    UINT drawCount = convert::primitiveData(PrimitiveType, PrimitiveCount, topology);

    m_context->IASetPrimitiveTopology(topology);
    const UINT instanceCount = getInstanceCount();
    if (instanceCount) m_context->DrawInstanced(drawCount, instanceCount, StartVertex, 0);
    else m_context->Draw(drawCount, StartVertex);

    postDraw();

    return D3D_OK;
  }
  HRESULT D3D9ImmediateRenderer::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {

    if (pVertexStreamZeroData == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "DrawPrimitiveUP: pVertexStreamZeroData was nullptr.");

    if (!preDraw()) {
      postDraw();
      return D3DERR_INVALIDCALL; // Unsupported shader/declaration combination.
    }
    if (VertexStreamZeroStride == 0) return D3DERR_INVALIDCALL;
    if (PrimitiveCount == 0) { postDraw(); return D3D_OK; }


    D3D_PRIMITIVE_TOPOLOGY topology;
    UINT drawCount = convert::primitiveData(PrimitiveType, PrimitiveCount, topology);
    if (drawCount > std::numeric_limits<UINT>::max() / VertexStreamZeroStride)
      return D3DERR_INVALIDCALL;
    UINT length = drawCount * VertexStreamZeroStride;

    m_upVertexBuffer.reserve(length);
    if (!m_upVertexBuffer.getBuffer()) return D3DERR_INVALIDCALL;
    uint32_t offset = m_upVertexBuffer.update(m_context, pVertexStreamZeroData, length);

    ID3D11Buffer* buffer = m_upVertexBuffer.getBuffer();
    m_context->IASetVertexBuffers(0, 1, &buffer, &VertexStreamZeroStride, &offset);

    if (PrimitiveType == D3DPT_TRIANGLEFAN) {
      m_state->dirtyFlags |= dirtyFlags::vertexBuffers;
      return drawTriangleFan(false, 0, PrimitiveCount, 0);
    }
    m_context->IASetPrimitiveTopology(topology);
    m_context->Draw(drawCount, 0);

    m_state->dirtyFlags |= dirtyFlags::vertexBuffers;

    postDraw();

    return D3D_OK;
  }
  HRESULT D3D9ImmediateRenderer::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {

    if (pVertexStreamZeroData == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "DrawIndexedPrimitiveUP: pVertexStreamZeroData was nullptr.");

    if (pIndexData == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "DrawIndexedPrimitiveUP: pIndexData was nullptr.");

    if (!preDraw()) {
      postDraw();
      return D3DERR_INVALIDCALL; // Unsupported shader/declaration combination.
    }

    if (IndexDataFormat != D3DFMT_INDEX16 && IndexDataFormat != D3DFMT_INDEX32)
      return D3DERR_INVALIDCALL;
    if (VertexStreamZeroStride == 0) return D3DERR_INVALIDCALL;
    if (PrimitiveCount == 0) { postDraw(); return D3D_OK; }


    D3D_PRIMITIVE_TOPOLOGY topology;
    UINT drawCount = convert::primitiveData(PrimitiveType, PrimitiveCount, topology);
    if (MinVertexIndex > std::numeric_limits<UINT>::max() - NumVertices ||
        MinVertexIndex + NumVertices > std::numeric_limits<UINT>::max() / VertexStreamZeroStride)
      return D3DERR_INVALIDCALL;
    UINT length = (MinVertexIndex + NumVertices) * VertexStreamZeroStride;

    m_upVertexBuffer.reserve(length);
    if (!m_upVertexBuffer.getBuffer()) return D3DERR_INVALIDCALL;
    uint32_t offset = m_upVertexBuffer.update(m_context, pVertexStreamZeroData, length);

    ID3D11Buffer* buffer = m_upVertexBuffer.getBuffer();
    m_context->IASetVertexBuffers(0, 1, &buffer, &VertexStreamZeroStride, &offset);

    if (PrimitiveType == D3DPT_TRIANGLEFAN) {
      m_state->dirtyFlags |= dirtyFlags::vertexBuffers;
      return drawTriangleFan(true, 0, PrimitiveCount, 0, pIndexData, IndexDataFormat);
    }

    // Index data size depends on the *index count*, not vertex-buffer bytes.
    const UINT indexSize = IndexDataFormat == D3DFMT_INDEX32 ? 4u : 2u;
    if (drawCount > std::numeric_limits<UINT>::max() / indexSize)
      return D3DERR_INVALIDCALL;
    const UINT indexLength = drawCount * indexSize;
    m_upIndexBuffer.reserve(indexLength);
    if (!m_upIndexBuffer.getBuffer()) return D3DERR_INVALIDCALL;
    const UINT indexOffset = m_upIndexBuffer.update(m_context, pIndexData, indexLength);
    m_context->IASetIndexBuffer(m_upIndexBuffer.getBuffer(),
      IndexDataFormat == D3DFMT_INDEX32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, indexOffset);

    m_context->IASetPrimitiveTopology(topology);
    m_context->DrawIndexed(drawCount, 0, 0);

    m_state->dirtyFlags |= dirtyFlags::vertexBuffers;
    m_state->dirtyFlags |= dirtyFlags::indexBuffer;

    postDraw();

    return D3D_OK;
  }
  HRESULT D3D9ImmediateRenderer::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {

    if (!preDraw()) {
      postDraw();
      return D3DERR_INVALIDCALL; // Unsupported shader/declaration combination.
    }

    if (PrimitiveType == D3DPT_TRIANGLEFAN)
      return this->drawTriangleFan(true, startIndex, primCount, BaseVertexIndex);

    D3D_PRIMITIVE_TOPOLOGY topology;
    UINT drawCount = convert::primitiveData(PrimitiveType, primCount, topology);

    m_context->IASetPrimitiveTopology(topology);
    const UINT instanceCount = getInstanceCount();
    if (instanceCount) m_context->DrawIndexedInstanced(drawCount, instanceCount, startIndex, BaseVertexIndex, 0);
    else m_context->DrawIndexed(drawCount, startIndex, BaseVertexIndex);

    postDraw();
    return D3D_OK;
  }

  //

  void D3D9ImmediateRenderer::blit(Direct3DSurface9* dst, Direct3DSurface9* src) {
    D3DSURFACE_DESC desc;
    src->GetDesc(&desc);

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = (float)desc.Width;
    viewport.Height = (float)desc.Height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_state->dirtyFlags |= dirtyFlags::viewport;

    m_context->PSSetSamplers(0, 1, &m_blitSampler);
    m_state->dirtySamplers |= 1;

    m_context->RSSetState(nullptr);
    m_state->dirtyFlags |= dirtyFlags::rasterizer;

    m_context->OMSetDepthStencilState(nullptr, 0);
    m_state->dirtyFlags |= dirtyFlags::depthStencilState;

    // TODO! Do I need to do any SRGB-ness here.
    ID3D11RenderTargetView* dstRTV = dst->GetD3D11RenderTarget(false);
    m_context->OMSetRenderTargets(1, &dstRTV, nullptr);
    m_state->dirtyFlags |= dirtyFlags::renderTargets;
    
    ID3D11ShaderResourceView* srcSRV = src->GetDXUPResource()->GetSRV(false);
    m_context->PSSetShaderResources(0, 1, &srcSRV);
    m_state->dirtyFlags |= dirtyFlags::textures;

    m_context->VSSetShader(m_blitVS.ptr(), nullptr, 0);
    m_state->dirtyFlags |= dirtyFlags::vertexShader;

    m_context->PSSetShader(m_blitPS.ptr(), nullptr, 0);
    m_state->dirtyFlags |= dirtyFlags::pixelShader;

    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->IASetInputLayout(nullptr);
    m_state->dirtyFlags |= dirtyFlags::vertexDecl;

    m_context->Draw(3, 0);
  }

  void D3D9ImmediateRenderer::updateScissorRect() {
    m_context->RSSetScissorRects(1, (D3D11_RECT*)&m_state->scissorRect);
  }

  void D3D9ImmediateRenderer::updateViewport() {
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = (FLOAT)m_state->viewport.X;
    viewport.TopLeftY = (FLOAT)m_state->viewport.Y;
    viewport.MinDepth = m_state->viewport.MinZ;
    viewport.MaxDepth = m_state->viewport.MaxZ;
    viewport.Width = (FLOAT)m_state->viewport.Width;
    viewport.Height = (FLOAT)m_state->viewport.Height;
    m_context->RSSetViewports(1, &viewport);
  }

  std::array<UINT, 16> D3D9ImmediateRenderer::getInstanceStepRates() const {
    std::array<UINT, 16> steps = {};
    for (size_t i = 0; i < steps.size(); ++i) {
      const UINT setting = m_state->streamFrequencies[i];
      if ((setting & D3DSTREAMSOURCE_INSTANCEDATA) == D3DSTREAMSOURCE_INSTANCEDATA)
        steps[i] = setting & 0x3fffffffu;
    }
    return steps;
  }

  UINT D3D9ImmediateRenderer::getInstanceCount() const {
    const UINT setting = m_state->streamFrequencies[0];
    return (setting & D3DSTREAMSOURCE_INDEXEDDATA) ? (setting & 0x3fffffffu) : 0;
  }

  void D3D9ImmediateRenderer::updateVertexShaderAndInputLayout() {
    if (m_state->vertexShader == nullptr) {
      updateFixedFunctionVertexShader();
      return;
    }
    if (m_state->vertexDecl == nullptr)
      return;
    const auto& originalElements = m_state->vertexDecl->GetD3D11Descs();
    if (originalElements.empty()) return;
    const auto steps = getInstanceStepRates();
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements(originalElements);
    for (auto& element : elements) {
      if (element.InputSlot < steps.size() && steps[element.InputSlot]) {
        element.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
        element.InstanceDataStepRate = steps[element.InputSlot];
      } else {
        element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
      }
    }
    auto* vertexShdrBytecode = m_state->vertexShader->GetTranslation();

    ID3D11InputLayout* layout = m_state->vertexShader->GetLinkedInput(m_state->vertexDecl.ptr(), steps);

    if (layout == nullptr) {
      HRESULT result = m_device->CreateInputLayout(&elements[0], elements.size(), vertexShdrBytecode->getBytecode(), vertexShdrBytecode->getByteSize(), &layout);

      if (!FAILED(result)) {
        m_state->vertexShader->LinkInput(layout, m_state->vertexDecl.ptr(), steps);

        layout->Release();
      }
    }

    if (layout == nullptr)
      return;

    m_state->dirtyFlags &= ~dirtyFlags::vertexDecl;
    m_state->dirtyFlags &= ~dirtyFlags::vertexShader;

    m_context->IASetInputLayout(layout);

    m_context->VSSetShader(m_state->vertexShader->GetD3D11Shader(), nullptr, 0);
  }

  bool D3D9ImmediateRenderer::fixedFunctionSupported() const {
    const auto* declaration = m_state->vertexDecl.ptr();
    const DWORD fvf = declaration ? declaration->GetFVF() : 0;
    uint64_t missing = 0;
    if (!declaration || !fvf) missing |= 1ull << 0; // Always fatal.
    const bool transformed = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    const bool lighting = !transformed && m_state->renderState[D3DRS_LIGHTING];
    // Specular is emulated by the generated vertex/pixel pair.
    if (m_state->renderState[D3DRS_VERTEXBLEND] != D3DVBF_DISABLE) missing |= 1ull << 2;
    if (m_state->renderState[D3DRS_INDEXEDVERTEXBLENDENABLE]) missing |= 1ull << 3;
    if (m_state->renderState[D3DRS_CLIPPLANEENABLE]) missing |= 1ull << 4;
    if (m_state->textureStageStates[0][D3DTSS_TEXCOORDINDEX] != 0) missing |= 1ull << 5;
    if (m_state->textureStageStates[0][D3DTSS_TEXTURETRANSFORMFLAGS] != D3DTTFF_DISABLE) missing |= 1ull << 6;
    if (lighting) {
      // D3D9 lights vertices without NORMAL using zero diffuse dot product.
      // Ambient and emissive lighting remain active; it is not an invalid FVF.
      for (DWORD source : {m_state->renderState[D3DRS_DIFFUSEMATERIALSOURCE],
                          m_state->renderState[D3DRS_AMBIENTMATERIALSOURCE]}) {
        if (source != D3DMCS_MATERIAL && source != D3DMCS_COLOR1 && source != D3DMCS_COLOR2)
          missing |= 1ull << 8;
      }
      const DWORD emissive = m_state->renderState[D3DRS_EMISSIVEMATERIALSOURCE];
      if (emissive != D3DMCS_MATERIAL && emissive != D3DMCS_COLOR1 && emissive != D3DMCS_COLOR2)
        missing |= 1ull << 9;
      const DWORD specular = m_state->renderState[D3DRS_SPECULARMATERIALSOURCE];
      if (specular != D3DMCS_MATERIAL && specular != D3DMCS_COLOR1 && specular != D3DMCS_COLOR2)
        missing |= 1ull << 1;
      unsigned activeLights = 0;
      for (const auto& entry : m_state->lights) {
        if (!entry.second.enabled) continue;
        if (!entry.second.parametersCaptured ||
            (entry.second.parameters.Type != D3DLIGHT_DIRECTIONAL &&
             entry.second.parameters.Type != D3DLIGHT_POINT &&
             entry.second.parameters.Type != D3DLIGHT_SPOT) || ++activeLights > 8)
          missing |= 1ull << 10;
      }
    }
    if (m_state->renderState[D3DRS_FOGENABLE]) {
      if (m_state->renderState[D3DRS_FOGTABLEMODE] != D3DFOG_NONE ||
          m_state->renderState[D3DRS_RANGEFOGENABLE]) missing |= 1ull << 11;
      const DWORD mode = m_state->renderState[D3DRS_FOGVERTEXMODE];
      if (mode != D3DFOG_NONE && mode != D3DFOG_LINEAR &&
          mode != D3DFOG_EXP && mode != D3DFOG_EXP2) missing |= 1ull << 12;
      if (mode != D3DFOG_NONE && transformed && !(fvf & D3DFVF_SPECULAR)) missing |= 1ull << 13;
      if (mode != D3DFOG_NONE && (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZW)
        missing |= 1ull << 14;
    }
    for (unsigned i = 1; i < 8; ++i)
      if (m_state->textureStageStates[i][D3DTSS_COLOROP] != D3DTOP_DISABLE)
        missing |= 1ull << 15;
    if (m_state->textures[0] && m_state->textures[0]->GetType() != D3DRTYPE_TEXTURE)
      missing |= 1ull << 16;
    if (!fixed_function::supportedColorOp(m_state->textureStageStates[0][D3DTSS_COLOROP]) ||
        !fixed_function::supportedColorOp(m_state->textureStageStates[0][D3DTSS_ALPHAOP]))
      missing |= 1ull << 17;
    for (auto arg : {m_state->textureStageStates[0][D3DTSS_COLORARG1],
                     m_state->textureStageStates[0][D3DTSS_COLORARG2],
                     m_state->textureStageStates[0][D3DTSS_ALPHAARG1],
                     m_state->textureStageStates[0][D3DTSS_ALPHAARG2]})
      if (!fixed_function::supportedArgument(arg)) missing |= 1ull << 18;

    return !missing;
  }

  bool D3D9ImmediateRenderer::updateFixedFunctionVertexShader() {
    if (!fixedFunctionSupported()) return false;
    auto* decl = m_state->vertexDecl.ptr();
    const uint32_t key = fixed_function::vertexKey(decl->GetFVF());
    auto found = m_ffVertexShaders.find(key);
    if (found == m_ffVertexShaders.end()) {
      std::string source = fixed_function::vertexSource(key);
      Com<ID3DBlob> shaderBytecode;
      Com<ID3DBlob> errors;
      HRESULT result = E_FAIL;
      if (!d3dcompiler::compile(&result, source.c_str(), "main", "vs_4_0",
                                 &shaderBytecode, &errors) || FAILED(result)) {
        if (errors != nullptr)
          log::warn("Fixed-function VS compile failed: %s", (char*)errors->GetBufferPointer());
        else
          log::warn("Fixed-function VS: D3DCompiler unavailable or compile failed.");
        return false;
      }
      FFVertexProgram compiled;
      result = m_device->CreateVertexShader(shaderBytecode->GetBufferPointer(),
        shaderBytecode->GetBufferSize(), nullptr, &compiled.shader);
      if (FAILED(result)) return false;
      auto* start = static_cast<const uint8_t*>(shaderBytecode->GetBufferPointer());
      compiled.bytecode.assign(start, start + shaderBytecode->GetBufferSize());
      found = m_ffVertexShaders.emplace(key, std::move(compiled)).first;
    }

    const auto steps = getInstanceStepRates();
    if (decl != m_ffInputDecl.ptr() || key != m_ffInputKey || m_ffInputLayout == nullptr ||
        steps != m_ffInstanceStepRates) {
      const auto& originalElements = decl->GetD3D11Descs();
      if (originalElements.empty()) return false;
      std::vector<D3D11_INPUT_ELEMENT_DESC> elements(originalElements);
      for (auto& element : elements) {
        if (element.InputSlot < steps.size() && steps[element.InputSlot]) {
          element.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
          element.InstanceDataStepRate = steps[element.InputSlot];
        } else {
          element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
          element.InstanceDataStepRate = 0;
        }
      }
      Com<ID3D11InputLayout> layout;
      HRESULT result = m_device->CreateInputLayout(elements.data(),
        static_cast<UINT>(elements.size()), found->second.bytecode.data(),
        found->second.bytecode.size(), &layout);
      if (FAILED(result)) {
        log::warn("Fixed-function VS: cannot create FVF input layout.");
        return false;
      }
      m_ffInputLayout = layout;
      m_ffInputDecl = decl;
      m_ffInputKey = key;
      m_ffInstanceStepRates = steps;
    }
    m_context->IASetInputLayout(m_ffInputLayout.ptr());
    m_context->VSSetShader(found->second.shader.ptr(), nullptr, 0);
    m_state->dirtyFlags &= ~(dirtyFlags::vertexDecl | dirtyFlags::vertexShader);
    return true;
  }

  bool D3D9ImmediateRenderer::updateFixedFunctionConstants() {
    if (m_ffVertexBuffer == nullptr || m_ffPixelBuffer == nullptr) return false;
    FFVertexConstants vs = {};
    if (m_state->vertexShader == nullptr) {
      D3DMATRIX world, view, projection, temp;
      m_state->GetTransform(D3DTS_WORLD, &world);
      m_state->GetTransform(D3DTS_VIEW, &view);
      m_state->GetTransform(D3DTS_PROJECTION, &projection);
      matrix_math::multiply(world.m, view.m, temp.m);
      matrix_math::inverseTranspose3x3(temp.m, vs.normalWorldView);
      vs.normalSettings[0] = m_state->renderState[D3DRS_NORMALIZENORMALS] ? 1.0f : 0.0f;
      matrix_math::multiply(temp.m, projection.m, vs.worldViewProjection);
      vs.viewport[0] = float(m_state->viewport.X);
      vs.viewport[1] = float(m_state->viewport.Y);
      vs.viewport[2] = float(m_state->viewport.Width);
      vs.viewport[3] = float(m_state->viewport.Height);
      if (!vs.viewport[2] || !vs.viewport[3]) return false;
      // Unlit fixed function uses white when no vertex diffuse is supplied.
      vs.materialDiffuse[0] = vs.materialDiffuse[1] =
        vs.materialDiffuse[2] = vs.materialDiffuse[3] = 1.0f;
      std::memcpy(vs.world, world.m, sizeof(vs.world));
      std::memcpy(vs.view, view.m, sizeof(vs.view));
      const bool transformed = (m_state->vertexDecl->GetFVF() & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
      bool lighting = !transformed && m_state->renderState[D3DRS_LIGHTING];
      vs.specularSettings[0] = m_state->renderState[D3DRS_SPECULARENABLE] ? 1.0f : 0.0f;
      if (lighting) {
        const auto& mat = m_state->material;
        const auto copyColor = [](float* dst, const D3DCOLORVALUE& c) {
          dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = c.a;
        };
        copyColor(vs.materialDiffuse, mat.Diffuse);
        copyColor(vs.materialAmbient, mat.Ambient);
        copyColor(vs.materialEmissive, mat.Emissive);
        copyColor(vs.materialSpecular, mat.Specular);
        vs.specularSettings[2] = mat.Power;
        convert::color(m_state->renderState[D3DRS_AMBIENT], vs.globalAmbient);
        vs.lightingSettings[0] = 1.0f;
        const DWORD fvf = m_state->vertexDecl->GetFVF();
        const bool hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;
        const bool hasSpecular = (fvf & D3DFVF_SPECULAR) != 0;
        const bool colorVertex = m_state->renderState[D3DRS_COLORVERTEX] != 0;
        const auto materialSource = [&](DWORD source) -> float {
          if (!colorVertex) return 0.0f;
          if (source == D3DMCS_COLOR1 && hasDiffuse) return 1.0f;
          if (source == D3DMCS_COLOR2 && hasSpecular) return 2.0f;
          // D3D9 uses the material color if a selected vertex color is absent.
          return 0.0f;
        };
        vs.lightingSettings[2] = materialSource(m_state->renderState[D3DRS_DIFFUSEMATERIALSOURCE]);
        vs.lightingSettings[3] = materialSource(m_state->renderState[D3DRS_AMBIENTMATERIALSOURCE]);
        vs.emissiveControl[0] = materialSource(m_state->renderState[D3DRS_EMISSIVEMATERIALSOURCE]);
        vs.specularSettings[1] = materialSource(m_state->renderState[D3DRS_SPECULARMATERIALSOURCE]);
        vs.specularSettings[3] = m_state->renderState[D3DRS_LOCALVIEWER] ? 1.0f : 0.0f;
        unsigned count = 0;
        for (const auto& entry : m_state->lights) {
          const auto& light = entry.second;
          if (!light.enabled) continue;
          if (count == 8) return false;
          vs.lightDirection[count][0] = light.parameters.Direction.x;
          vs.lightDirection[count][1] = light.parameters.Direction.y;
          vs.lightDirection[count][2] = light.parameters.Direction.z;
          vs.lightDirection[count][3] = static_cast<float>(light.parameters.Type);
          vs.lightPosition[count][0] = light.parameters.Position.x;
          vs.lightPosition[count][1] = light.parameters.Position.y;
          vs.lightPosition[count][2] = light.parameters.Position.z;
          vs.lightPosition[count][3] = light.parameters.Range;
          vs.lightAttenuation[count][0] = light.parameters.Attenuation0;
          vs.lightAttenuation[count][1] = light.parameters.Attenuation1;
          vs.lightAttenuation[count][2] = light.parameters.Attenuation2;
          vs.lightAttenuation[count][3] = light.parameters.Falloff;
          vs.lightSpotAngles[count][0] = std::cos(light.parameters.Theta * 0.5f);
          vs.lightSpotAngles[count][1] = std::cos(light.parameters.Phi * 0.5f);
          copyColor(vs.lightDiffuse[count], light.parameters.Diffuse);
          copyColor(vs.lightAmbient[count], light.parameters.Ambient);
          copyColor(vs.lightSpecular[count], light.parameters.Specular);
          ++count;
        }
        vs.lightingSettings[1] = static_cast<float>(count);
      }
      const DWORD fogMode = m_state->renderState[D3DRS_FOGVERTEXMODE];
      std::memcpy(&vs.fogSettings[0], &m_state->renderState[D3DRS_FOGSTART], sizeof(float));
      std::memcpy(&vs.fogSettings[1], &m_state->renderState[D3DRS_FOGEND], sizeof(float));
      std::memcpy(&vs.fogSettings[2], &m_state->renderState[D3DRS_FOGDENSITY], sizeof(float));
      vs.fogSettings[3] = static_cast<float>(fogMode);
      m_context->UpdateSubresource(m_ffVertexBuffer.ptr(), 0, nullptr, &vs, 0, 0);
      ID3D11Buffer* buffer = m_ffVertexBuffer.ptr();
      m_context->VSSetConstantBuffers(1, 1, &buffer);
    }
    if (m_state->pixelShader == nullptr) {
      FFPixelConstants ps = {};
      convert::color(m_state->renderState[D3DRS_TEXTUREFACTOR], ps.textureFactor);
      const auto& stage = m_state->textureStageStates[0];
      ps.colorConfig[0] = stage[D3DTSS_COLOROP];
      ps.colorConfig[1] = stage[D3DTSS_COLORARG1];
      ps.colorConfig[2] = stage[D3DTSS_COLORARG2];
      ps.colorConfig[3] = m_state->textures[0] != nullptr;
      ps.alphaConfig[0] = stage[D3DTSS_ALPHAOP];
      // With no bound texture, a stage requiring TEXTURE has no input.
      // Preserve diffuse (including alpha) rather than multiplying by a
      // synthetic white texel and selecting its alpha.
      if (!m_state->textures[0]) {
        if (fixed_function::operationNeedsTexture(ps.colorConfig[0], ps.colorConfig[1], ps.colorConfig[2]))
          ps.colorConfig[0] = D3DTOP_DISABLE;
        if (fixed_function::operationNeedsTexture(ps.alphaConfig[0],
            stage[D3DTSS_ALPHAARG1], stage[D3DTSS_ALPHAARG2]))
          ps.alphaConfig[0] = D3DTOP_DISABLE;
      }
      ps.alphaConfig[1] = stage[D3DTSS_ALPHAARG1];
      ps.alphaConfig[2] = stage[D3DTSS_ALPHAARG2];
      ps.alphaConfig[3] = m_state->renderState[D3DRS_ALPHATESTENABLE];
      ps.testConfig[0] = m_state->renderState[D3DRS_ALPHAFUNC];
      ps.testConfig[1] = m_state->renderState[D3DRS_ALPHAREF] & 255u;
      ps.testConfig[2] = m_state->renderState[D3DRS_FOGENABLE] &&
        m_state->renderState[D3DRS_FOGVERTEXMODE] != D3DFOG_NONE;
      ps.specularControl[0] = m_state->renderState[D3DRS_SPECULARENABLE] != 0;
      convert::color(m_state->renderState[D3DRS_FOGCOLOR], ps.fogColor);
      m_context->UpdateSubresource(m_ffPixelBuffer.ptr(), 0, nullptr, &ps, 0, 0);
      ID3D11Buffer* buffer = m_ffPixelBuffer.ptr();
      m_context->PSSetConstantBuffers(1, 1, &buffer);
    }
    return true;
  }

  void D3D9ImmediateRenderer::updateDepthStencilState() {
    D3D11_DEPTH_STENCIL_DESC desc;
    desc.BackFace.StencilDepthFailOp = convert::stencilOp(m_state->renderState[D3DRS_CCW_STENCILZFAIL]);
    desc.BackFace.StencilFailOp = convert::stencilOp(m_state->renderState[D3DRS_CCW_STENCILFAIL]);
    desc.BackFace.StencilPassOp = convert::stencilOp(m_state->renderState[D3DRS_CCW_STENCILPASS]);
    desc.BackFace.StencilFunc = convert::func(m_state->renderState[D3DRS_CCW_STENCILFUNC]);

    desc.DepthEnable = (m_state->renderState[D3DRS_ZENABLE] == D3DZB_FALSE) ? FALSE : TRUE;
    desc.DepthFunc = convert::func(m_state->renderState[D3DRS_ZFUNC]);
    desc.DepthWriteMask = m_state->renderState[D3DRS_ZWRITEENABLE] == TRUE ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;

    desc.FrontFace.StencilDepthFailOp = convert::stencilOp(m_state->renderState[D3DRS_STENCILZFAIL]);
    desc.FrontFace.StencilFailOp = convert::stencilOp(m_state->renderState[D3DRS_STENCILFAIL]);
    desc.FrontFace.StencilPassOp = convert::stencilOp(m_state->renderState[D3DRS_STENCILPASS]);
    desc.FrontFace.StencilFunc = convert::func(m_state->renderState[D3DRS_STENCILFUNC]);

    // D3D9 uses the clockwise stencil setup for both faces unless
    // D3DRS_TWOSIDEDSTENCILMODE explicitly enables the CCW states.
    if (!m_state->renderState[D3DRS_TWOSIDEDSTENCILMODE])
      desc.BackFace = desc.FrontFace;

    desc.StencilEnable = m_state->renderState[D3DRS_STENCILENABLE] == TRUE ? TRUE : FALSE;
    desc.StencilReadMask = (UINT8)(m_state->renderState[D3DRS_STENCILMASK] & 0x000000FF); // I think we can do this.
    desc.StencilWriteMask = (UINT8)(m_state->renderState[D3DRS_STENCILWRITEMASK] & 0x000000FF);

    size_t hash = m_caches.depthStencil.hash(desc);
    ID3D11DepthStencilState* state = m_caches.depthStencil.lookupObject(hash);

    if (state == nullptr) {
      Com<ID3D11DepthStencilState> comState;

      HRESULT result = m_device->CreateDepthStencilState(&desc, &comState);
      if (FAILED(result)) {
        log::fail("Failed to create depth stencil state.");
        return;
      }

      m_caches.depthStencil.pushState(hash, desc, comState.ptr());
      state = comState.ptr();
    }

    m_context->OMSetDepthStencilState(state, (UINT)m_state->renderState[D3DRS_STENCILREF]);

    m_state->dirtyFlags &= ~dirtyFlags::depthStencilState;
  }
  void D3D9ImmediateRenderer::updateRasterizer() {
    D3D11_RASTERIZER_DESC1 desc;
    desc.AntialiasedLineEnable = m_state->renderState[D3DRS_ANTIALIASEDLINEENABLE] == TRUE;
    desc.CullMode = convert::cullMode(m_state->renderState[D3DRS_CULLMODE]);
    // D3D9 expresses constant depth bias directly in normalized Z units,
    // while D3D11 expresses it in integer depth-buffer steps. A cast to INT
    // drops virtually every useful polygon offset to zero, hiding coplanar
    // surfaces (including projected shadows) behind the depth buffer.
    unsigned depthBits = 24;
    if (m_state->depthStencil != nullptr) {
      switch (m_state->depthStencil->GetD3D9Desc().Format) {
      case D3DFMT_D16:
      case D3DFMT_D16_LOCKABLE:
        depthBits = 16;
        break;
      default:
        // D24 and D24S8 use a 24-bit UNORM depth plane. Floating-point
        // depth formats have a Z-dependent ULP; use the 24-bit near-midrange
        // approximation until a format-specific shader bias is available.
        break;
      }
    }
    desc.DepthBias = depthBiasToD3D11(
      reinterpret::dwordToFloat(m_state->renderState[D3DRS_DEPTHBIAS]), depthBits);
    desc.DepthBiasClamp = D3D11_DEFAULT_DEPTH_BIAS_CLAMP;
    // D3D9 allows applications to disable primitive clipping. D3D11 can
    // disable z clipping while retaining mandatory xy/w clipping; this is
    // particularly important for shadow volumes extending past the far plane.
    desc.DepthClipEnable = m_state->renderState[D3DRS_CLIPPING] != FALSE;
    desc.FillMode = convert::fillMode(m_state->renderState[D3DRS_FILLMODE]);
    desc.ForcedSampleCount = 0;
    desc.FrontCounterClockwise = false;
    desc.MultisampleEnable = false;
    desc.ScissorEnable = m_state->renderState[D3DRS_SCISSORTESTENABLE] == TRUE ? TRUE : FALSE;
    desc.SlopeScaledDepthBias = reinterpret::dwordToFloat(m_state->renderState[D3DRS_SLOPESCALEDEPTHBIAS]);

    size_t hash = m_caches.rasterizer.hash(desc);
    ID3D11RasterizerState1* state = m_caches.rasterizer.lookupObject(hash);

    if (state == nullptr) {
      Com<ID3D11RasterizerState1> comState;

      HRESULT result = m_device->CreateRasterizerState1(&desc, &comState);
      if (FAILED(result)) {
        log::fail("Failed to create rasterizer state.");
        return;
      }

      m_caches.rasterizer.pushState(hash, desc, comState.ptr());
      state = comState.ptr();
    }

    m_context->RSSetState(state);

    m_state->dirtyFlags &= ~dirtyFlags::rasterizer;
  }
  void D3D9ImmediateRenderer::updateBlendState() {
    D3D11_BLEND_DESC1 desc = {};
    desc.AlphaToCoverageEnable = false;
    desc.IndependentBlendEnable = false;

    bool separateAlpha = m_state->renderState[D3DRS_SEPARATEALPHABLENDENABLE] == TRUE;

    // Change me if we do independent blending at some point.
    for (uint32_t i = 0; i < 1; i++) {
      desc.RenderTarget[i].BlendEnable = m_state->renderState[D3DRS_ALPHABLENDENABLE] == TRUE;

      desc.RenderTarget[i].BlendOp = convert::blendOp(m_state->renderState[D3DRS_BLENDOP]);
      desc.RenderTarget[i].BlendOpAlpha = separateAlpha ? convert::blendOp(m_state->renderState[D3DRS_BLENDOPALPHA]) : D3D11_BLEND_OP_ADD;

      desc.RenderTarget[i].DestBlend = convert::blend(m_state->renderState[D3DRS_DESTBLEND]);
      desc.RenderTarget[i].DestBlendAlpha = separateAlpha ? convert::blend(m_state->renderState[D3DRS_DESTBLENDALPHA]) : D3D11_BLEND_ZERO;

      desc.RenderTarget[i].LogicOp = D3D11_LOGIC_OP_NOOP;
      desc.RenderTarget[i].LogicOpEnable = false;

      uint32_t writeIndex;
      switch (i) {
      default:
      case 0: writeIndex = D3DRS_COLORWRITEENABLE; break;
      case 1: writeIndex = D3DRS_COLORWRITEENABLE1; break;
      case 2: writeIndex = D3DRS_COLORWRITEENABLE2; break;
      case 3: writeIndex = D3DRS_COLORWRITEENABLE3; break;
      }

      desc.RenderTarget[i].RenderTargetWriteMask = m_state->renderState[writeIndex];

      desc.RenderTarget[i].SrcBlend = convert::blend(m_state->renderState[D3DRS_SRCBLEND]);
      desc.RenderTarget[i].SrcBlendAlpha = separateAlpha ? convert::blend(m_state->renderState[D3DRS_SRCBLENDALPHA]) : D3D11_BLEND_ONE;
    }

    size_t hash = m_caches.blendState.hash(desc);
    ID3D11BlendState1* state = m_caches.blendState.lookupObject(hash);

    if (state == nullptr) {
      Com<ID3D11BlendState1> comState;

      HRESULT result = m_device->CreateBlendState1(&desc, &comState);
      if (FAILED(result)) {
        log::fail("Failed to create blend state.");
        return;
      }

      m_caches.blendState.pushState(hash, desc, comState.ptr());
      state = comState.ptr();
    }

    float blendFactor[4];
    convert::color((D3DCOLOR)m_state->renderState[D3DRS_BLENDFACTOR], blendFactor);
    m_context->OMSetBlendState(state, blendFactor, 0xFFFFFFFF);
  }
  void D3D9ImmediateRenderer::updateSampler(uint32_t sampler) {
    auto& samplerState = m_state->samplerStates[sampler];

    D3D11_SAMPLER_DESC desc;
    desc.AddressU = convert::textureAddressMode(samplerState[D3DSAMP_ADDRESSU]);
    desc.AddressV = convert::textureAddressMode(samplerState[D3DSAMP_ADDRESSV]);
    desc.AddressW = convert::textureAddressMode(samplerState[D3DSAMP_ADDRESSW]);
    convert::color(samplerState[D3DSAMP_BORDERCOLOR], desc.BorderColor);
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.Filter = convert::filter(samplerState[D3DSAMP_MAGFILTER], samplerState[D3DSAMP_MINFILTER], samplerState[D3DSAMP_MIPFILTER]);
    desc.MaxAnisotropy = std::clamp((UINT)samplerState[D3DSAMP_MAXANISOTROPY], 0u, 16u);
    desc.MipLODBias = std::clamp(reinterpret::dwordToFloat(samplerState[D3DSAMP_MIPMAPLODBIAS]), -16.0f, 15.99f);
    DWORD textureLOD = 0;
    IDirect3DBaseTexture9* texture = m_state->textures[sampler];
    if (texture != nullptr)
      textureLOD = texture->GetLOD();
    desc.MinLOD = mipmap::minSamplerLOD(samplerState[D3DSAMP_MAXMIPLEVEL], textureLOD);
    desc.MaxLOD = FLT_MAX;

    size_t hash = m_caches.sampler.hash(desc);
    ID3D11SamplerState* state = m_caches.sampler.lookupObject(hash);

    if (state == nullptr) {
      Com<ID3D11SamplerState> comState;

      HRESULT result = m_device->CreateSamplerState(&desc, &comState);
      if (FAILED(result)) {
        log::fail("Failed to create sampler state.");
        return;
      }

      m_caches.sampler.pushState(hash, desc, comState.ptr());
      state = comState.ptr();
    }

    if (sampler < 16)
      m_context->PSSetSamplers(sampler, 1, &state);
    else
      m_context->VSSetSamplers(sampler - 16, 1, &state);

    // Clear the original bit index, including vertex samplers 16-19.
    m_state->dirtySamplers &= ~(1u << sampler);
  }
  void D3D9ImmediateRenderer::updateSamplers() {
    for (uint32_t i = 0; i < 20; i++) {
      if (m_state->dirtySamplers & (1ull << i))
        updateSampler(i);
    }
  }
  void D3D9ImmediateRenderer::updateTextures() {
    std::array<ID3D11ShaderResourceView*, 20> srvs;

    for (uint32_t i = 0; i < 20; i++) {
      IDirect3DBaseTexture9* pTexture = m_state->textures[i];
      bool srgb = m_state->samplerStates[i][D3DSAMP_SRGBTEXTURE] == TRUE;

      srvs[i] = nullptr;

      if (pTexture != nullptr) {
        switch (pTexture->GetType()) {

        case D3DRTYPE_TEXTURE: {
          Direct3DTexture9* tex = reinterpret_cast<Direct3DTexture9*>(pTexture);
          srvs[i] = tex->GetDXUPResource()->GetSRV(srgb);
          break;
        }

        case D3DRTYPE_CUBETEXTURE: {
          Direct3DCubeTexture9* tex = reinterpret_cast<Direct3DCubeTexture9*>(pTexture);
          srvs[i] = tex->GetDXUPResource()->GetSRV(srgb);
          break;
        }

        case D3DRTYPE_VOLUMETEXTURE: {
          Direct3DVolumeTexture9* tex = reinterpret_cast<Direct3DVolumeTexture9*>(pTexture);
          srvs[i] = tex->GetDXUPResource()->GetSRV(srgb);
          break;
        }

        default: log::warn("updateTextures: unknown resource type as a texture."); break;

        }
      }
    }

    m_context->PSSetShaderResources(0, 16, &srvs[0]);
    m_context->VSSetShaderResources(0, 4, &srvs[16]);
    m_state->dirtyFlags &= ~dirtyFlags::textures;
  }
  void D3D9ImmediateRenderer::updateRenderTargets() {
    std::array<ID3D11RenderTargetView*, 4> rtvs = { nullptr, nullptr, nullptr, nullptr };
    for (uint32_t i = 0; i < 4; i++)
    {
      if (m_state->renderTargets[i] != nullptr) {
        rtvs[i] = m_state->renderTargets[i]->GetD3D11RenderTarget(m_state->renderState[D3DRS_SRGBWRITEENABLE] == TRUE);
        m_state->renderTargets[i]->GetDXUPResource()->MarkDirty(0, 0); // Mark dirty so we copy to staging when read on the CPU again.
        if (rtvs[i] == nullptr)
          log::warn("No render target view for bound render target surface.");
      }
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (m_state->depthStencil != nullptr) {
      dsv = m_state->depthStencil->GetD3D11DepthStencil();
      if (dsv == nullptr)
        log::warn("No depth stencil view for bound depth stencil surface.");
    }

    m_context->OMSetRenderTargets(4, &rtvs[0], dsv);

    m_state->dirtyFlags &= ~dirtyFlags::renderTargets;
  }
  void D3D9ImmediateRenderer::updatePixelShader() {
    if (m_state->pixelShader != nullptr) {
      m_context->PSSetShader(m_state->pixelShader->GetD3D11Shader(), nullptr, 0);
    } else {
      if (m_ffPixelShader == nullptr) {
        Com<ID3DBlob> bytecode, errors;
        HRESULT result = E_FAIL;
        if (!d3dcompiler::compile(&result, fixed_function::pixelSource(), "main", "ps_4_0",
                                   &bytecode, &errors) || FAILED(result)) {
          if (errors != nullptr)
            log::warn("Fixed-function PS compile failed: %s", (char*)errors->GetBufferPointer());
          else
            log::warn("Fixed-function PS: D3DCompiler unavailable or compile failed.");
          return;
        }
        result = m_device->CreatePixelShader(bytecode->GetBufferPointer(),
          bytecode->GetBufferSize(), nullptr, &m_ffPixelShader);
        if (FAILED(result)) {
          log::fail("Fixed-function PS: CreatePixelShader failed hr=0x%08lx", (unsigned long)result);
          return;
        }
          }
      m_context->PSSetShader(m_ffPixelShader.ptr(), nullptr, 0);
    }
    m_state->dirtyFlags &= ~dirtyFlags::pixelShader;
  }
  void D3D9ImmediateRenderer::updateVertexBuffer() {
    std::array<ID3D11Buffer*, 16> buffers;
    for (uint32_t i = 0; i < 16; i++) {
      Direct3DVertexBuffer9* buffer = m_state->vertexBuffers[i].ptr();
      if (buffer != nullptr)
        buffers[i] = buffer->GetDXUPResource()->GetResourceAs<ID3D11Buffer>();
      else
        buffers[i] = nullptr;
    }
    m_context->IASetVertexBuffers(0, 16, buffers.data(), m_state->vertexStrides.data(), m_state->vertexOffsets.data());
    m_state->dirtyFlags &= ~dirtyFlags::vertexBuffers;
  }
  void D3D9ImmediateRenderer::updateIndexBuffer() {
    DXGI_FORMAT format = DXGI_FORMAT_R16_UINT;

    ID3D11Buffer* buffer = nullptr;
    if (m_state->indexBuffer != nullptr) {
      if (m_state->indexBuffer->GetD3D9Desc().Format == D3DFMT_INDEX32)
        format = DXGI_FORMAT_R32_UINT;

      buffer = m_state->indexBuffer->GetDXUPResource()->GetResourceAs<ID3D11Buffer>();
    }

    m_context->IASetIndexBuffer(buffer, format, 0);
    m_state->dirtyFlags &= ~dirtyFlags::indexBuffer;
  }
  void D3D9ImmediateRenderer::updateVertexConstants() {
    m_vsConstants.update(m_state->vsConstants);
    m_state->dirtyFlags &= ~dirtyFlags::vsConstants;
  }
  void D3D9ImmediateRenderer::updatePixelConstants() {
    m_psConstants.update(m_state->psConstants);
    m_state->dirtyFlags &= ~dirtyFlags::psConstants;
  }

  //

  void D3D9ImmediateRenderer::undirtyContext() {
    if (m_state->dirtyFlags & dirtyFlags::viewport)
      updateViewport();

    if (m_state->dirtyFlags & dirtyFlags::scissorRect)
      updateScissorRect();

    if (m_state->dirtyFlags & dirtyFlags::vertexBuffers)
      updateVertexBuffer();

    if (m_state->dirtyFlags & dirtyFlags::indexBuffer)
      updateIndexBuffer();

    if (m_state->dirtyFlags & dirtyFlags::vsConstants)
      updateVertexConstants();

    if (m_state->dirtyFlags & dirtyFlags::psConstants)
      updatePixelConstants();

    if (m_state->dirtyFlags & dirtyFlags::vertexDecl || m_state->dirtyFlags & dirtyFlags::vertexShader)
      updateVertexShaderAndInputLayout();

    if (m_state->dirtySamplers != 0)
      updateSamplers();

    if (m_state->dirtyFlags & dirtyFlags::textures)
      updateTextures();

    if (m_state->dirtyFlags & dirtyFlags::renderTargets)
      updateRenderTargets();

    if (m_state->dirtyFlags & dirtyFlags::rasterizer)
      updateRasterizer();

    if (m_state->dirtyFlags & dirtyFlags::blendState)
      updateBlendState();

    if (m_state->dirtyFlags & dirtyFlags::depthStencilState)
      updateDepthStencilState();

    if (m_state->dirtyFlags & dirtyFlags::pixelShader)
      updatePixelShader();
  }

  //

  void D3D9ImmediateRenderer::handleDepthStencilDiscard() {
    if (m_state->depthStencil != nullptr && m_state->depthStencil->GetD3D9Desc().Discard) {
      ID3D11DepthStencilView* dsv = m_state->depthStencil->GetD3D11DepthStencil();
      if (dsv)
        // Discarded depth is undefined in D3D9. Clearing to near (0.0)
        // makes ordinary LESS/LESSEQUAL world geometry fail its depth test.
        // Start the recycled surface at far depth instead.
        m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    }
  }

  //

  bool D3D9ImmediateRenderer::canDraw() {
    const uint32_t pending = dirtyFlags::vertexDecl | dirtyFlags::vertexShader | dirtyFlags::pixelShader;
    if (m_state->dirtyFlags & pending) return false;
    if (m_state->vertexShader == nullptr && !fixedFunctionSupported()) return false;
    return true;
  }
  bool D3D9ImmediateRenderer::preDraw() {
    // D3D9 defaults to fixed-function pixel shading when SetPixelShader was
    // never called. The initial state has no PS dirty bit, which previously
    // left the D3D11 pixel stage unbound for games like Obscure. Ensure the
    // fixed-function shader is created on the first such draw. Explicit D3D9
    // pixel shaders and subsequent state transitions follow the normal path.
    if (m_state->pixelShader == nullptr && m_ffPixelShader.ptr() == nullptr)
      m_state->dirtyFlags |= dirtyFlags::pixelShader;
    undirtyContext();
    bool can = canDraw();
    if (can && (m_state->vertexShader == nullptr || m_state->pixelShader == nullptr))
      can = updateFixedFunctionConstants();
    return can;
  }
  void D3D9ImmediateRenderer::postDraw() {
    // Nothing here yet!
  }

}