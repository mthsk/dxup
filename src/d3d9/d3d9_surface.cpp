#include "d3d9_surface.h"
#include "d3d9_format.h"
#include "d3d9_texture.h"
#include "d3d9_format.h"
#include "../util/config.h"
#include <algorithm>

namespace dxup {

  Direct3DSurface9::Direct3DSurface9(UINT slice, UINT mip, Direct3DDevice9Ex* device, IUnknown* container, DXUPResource* resource, const D3D9ResourceDesc& desc)
    : Direct3DSurface9Base{ device, nullptr, desc }
    , m_container{ container }
    , m_slice{ slice }
    , m_mip{ mip }
    , m_rtView{ nullptr }
    , m_rtViewSRGB{ nullptr } {
    this->SetResource(resource);
  }

  HRESULT Direct3DSurface9::GetContainer(REFIID riid, void** ppContainer) {
    InitReturnPtr(ppContainer);

    if (ppContainer == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "GetContainer: ppContainer was nullptr.");

    if (m_container == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "GetContainer: m_container was nullptr.");

    return FAILED(m_container->QueryInterface(riid, ppContainer)) ? D3DERR_INVALIDCALL : D3D_OK;
  }

  HRESULT Direct3DSurface9::QueryInterface(REFIID riid, void** ppvObj) {
    InitReturnPtr(ppvObj);

    if (ppvObj == nullptr)
      return E_POINTER;

    if (riid == __uuidof(IDirect3DSurface9) || riid == __uuidof(IDirect3DResource9) || riid == __uuidof(IUnknown)) {
      *ppvObj = ref(this);
      return D3D_OK;
    }

    return E_NOINTERFACE;
  }

  HRESULT Direct3DSurface9::GetDesc(D3DSURFACE_DESC *pDesc) {
    if (!pDesc)
      return log::d3derr(D3DERR_INVALIDCALL, "GetDesc: pDesc was nullptr.");

    D3D11_TEXTURE2D_DESC desc;
    m_resource->GetResourceAs<ID3D11Texture2D>()->GetDesc(&desc);

    pDesc->Format = m_d3d9Desc.Format;
    pDesc->Height = std::max(1u, desc.Height >> m_mip);
    pDesc->Width = std::max(1u, desc.Width >> m_mip);
    pDesc->Pool = m_d3d9Desc.Pool;
    uint32_t sampleCount = desc.SampleDesc.Count;
    if (sampleCount == 1)
      sampleCount = 0; // Accounts for 0 samples being 1 in D3D9 and 1 being some random thing that doesnt make sense.

    pDesc->MultiSampleType = (D3DMULTISAMPLE_TYPE)sampleCount;
    pDesc->MultiSampleQuality = desc.SampleDesc.Quality;
    pDesc->Type = D3DRTYPE_SURFACE;
    pDesc->Usage = m_d3d9Desc.Usage;

    return D3D_OK;
  }
  HRESULT Direct3DSurface9::LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) {
    if (pLockedRect == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::LockRect: pLockedRect was nullptr.");

    D3DLOCKED_BOX lockedBox;
    D3DBOX box;

    if (pRect != nullptr) {
      box.Top = pRect->top;
      box.Left = pRect->left;
      box.Bottom = pRect->bottom;
      box.Right = pRect->right;
      box.Front = 0;
      box.Back = 1;
    }
    HRESULT result = m_resource->D3D9LockBox(m_slice, m_mip, &lockedBox, pRect != nullptr ? &box : nullptr, Flags, m_d3d9Desc.Usage);
    pLockedRect->pBits = lockedBox.pBits;
    pLockedRect->Pitch = lockedBox.RowPitch;

    return result;
  }
  HRESULT Direct3DSurface9::UnlockRect() {
    return m_resource->D3D9UnlockBox(m_slice, m_mip);
  }

  HRESULT Direct3DSurface9::GetDC(HDC *phdc) {
    InitReturnPtr(phdc);

    if (!config::getBool(config::GDICompatible))
      return log::d3derr(D3DERR_INVALIDCALL, "GetDC: GDI compatibility not enabled.");

    HRESULT result = D3DERR_INVALIDCALL;
    if (m_surface != nullptr && phdc != nullptr)
      result = m_surface->GetDC(FALSE, phdc);

    if (FAILED(result))
      return log::d3derr(D3DERR_INVALIDCALL, "GetDC: failed to get DC.");

    return D3D_OK;
  }
  HRESULT Direct3DSurface9::ReleaseDC(HDC hdc) {
    if (!config::getBool(config::GDICompatible))
      return log::d3derr(D3DERR_INVALIDCALL, "ReleaseDC: GDI compatibility not enabled.");

    HRESULT result = D3DERR_INVALIDCALL;
    if (m_surface != nullptr && hdc != nullptr)
      result = m_surface->ReleaseDC(nullptr);

    if (FAILED(result))
      return log::d3derr(D3DERR_INVALIDCALL, "ReleaseDC: failed to release DC.");

    return D3D_OK;
  }

  ULONG STDMETHODCALLTYPE Direct3DSurface9::AddRef() {
    if (m_container != nullptr)
      m_container->AddRef();

    return Direct3DSurface9Base::AddRef();
  }
  ULONG STDMETHODCALLTYPE Direct3DSurface9::Release() {
    if (m_container != nullptr)
      m_container->Release();

    return Direct3DSurface9Base::Release();
  }

  IDXGISurface1* Direct3DSurface9::GetDXGISurface() {
    return m_surface.ptr();
  }
  ID3D11RenderTargetView* Direct3DSurface9::GetD3D11RenderTarget(bool srgb) {
    if (srgb && m_rtViewSRGB != nullptr)
      return m_rtViewSRGB.ptr();

    return m_rtView.ptr();
  }
  ID3D11DepthStencilView* Direct3DSurface9::GetD3D11DepthStencil() {
    return m_dsView.ptr();
  }

  UINT Direct3DSurface9::GetMip() {
    return m_mip;
  }
  UINT Direct3DSurface9::GetSlice() {
    return m_slice;
  }
  UINT Direct3DSurface9::GetSubresource() {
    return D3D11CalcSubresource(m_mip, m_slice, this->GetDXUPResource()->GetMips());
  }

  void Direct3DSurface9::ClearResource() {
    m_resource = nullptr;
    m_dsView = nullptr;
    m_rtView = nullptr;
    m_rtViewSRGB = nullptr;
    m_surface = nullptr;
  }
  void Direct3DSurface9::SetResource(DXUPResource* resource) {
    this->ClearResource();

    m_resource = resource;

    if (GetSubresource() == 0 &&
        ((m_d3d9Desc.Usage & D3DUSAGE_DEPTHSTENCIL) || m_d3d9Desc.Format == D3DFMT_INTZ)) {
      if (m_d3d9Desc.Format == D3DFMT_INTZ) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = m_mip;
        HRESULT hr = GetD3D11Device()->CreateDepthStencilView(resource->GetResource(), &dsvDesc, &m_dsView);
        if (FAILED(hr))
          log::warn("Failed to create INTZ depth stencil view: 0x%08x", (unsigned)hr);
      } else if (FAILED(GetD3D11Device()->CreateDepthStencilView(resource->GetResource(), nullptr, &m_dsView))) {
        log::warn("Failed to create depth stencil for surface!");
      }
    }

    // Render targets can be texture mip levels and cube/array slices. The old
    // subresource-zero test silently skipped all other surfaces, leaving RTVs
    // null when games render into mip > 0 or faces other than the first.
    if (m_d3d9Desc.Usage & D3DUSAGE_RENDERTARGET) {
      D3D11_TEXTURE2D_DESC textureDesc = {};
      resource->GetResourceAs<ID3D11Texture2D>()->GetDesc(&textureDesc);
      D3D11_RENDER_TARGET_VIEW_DESC desc = {};
      if (textureDesc.SampleDesc.Count > 1) {
        if (textureDesc.ArraySize > 1) {
          desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
          desc.Texture2DMSArray.FirstArraySlice = m_slice;
          desc.Texture2DMSArray.ArraySize = 1;
        } else {
          desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        }
      } else if (textureDesc.ArraySize > 1) {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = m_mip;
        desc.Texture2DArray.FirstArraySlice = m_slice;
        desc.Texture2DArray.ArraySize = 1;
      } else {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = m_mip;
      }

      desc.Format = convert::makeUntypeless(convert::makeTypeless(resource->GetDXGIFormat()), false);
      HRESULT hr = GetD3D11Device()->CreateRenderTargetView(resource->GetResource(), &desc, &m_rtView);
      if (FAILED(hr)) {
        log::warn("RTV creation failed: D3D9 format=%u DXGI=%u mip=%u slice=%u usage=0x%08x hr=0x%08x",
          (unsigned)m_d3d9Desc.Format, (unsigned)resource->GetDXGIFormat(), m_mip, m_slice,
          (unsigned)m_d3d9Desc.Usage, (unsigned)hr);
      }

      const DXGI_FORMAT linearFormat = desc.Format;
      desc.Format = convert::makeUntypeless(convert::makeTypeless(resource->GetDXGIFormat()), true);
      if (desc.Format == linearFormat) {
        m_rtViewSRGB = m_rtView;
      } else if (FAILED(GetD3D11Device()->CreateRenderTargetView(resource->GetResource(), &desc, &m_rtViewSRGB))) {
        // Some formats have no sRGB rendering variant. Preserve the linear RTV
        // instead of leaving an otherwise valid D3D9 surface unrenderable.
        m_rtViewSRGB = m_rtView;
      }
    }

    if (config::getBool(config::GDICompatible))
      resource->GetResource()->QueryInterface(__uuidof(IDXGISurface1), (void**)&m_surface);
  }
  bool Direct3DSurface9::isRectValid(const RECT* rect) {
    if (rect == nullptr)
      return true;

    if (rect->right <= rect->left || rect->bottom <= rect->top)
      return false;

    if (rect->left < 0 || rect->top < 0)
      return false;

    D3DSURFACE_DESC desc;
    this->GetDesc(&desc);
    if (rect->right > (int)desc.Width || rect->bottom > (int)desc.Height)
      return false;

    return true;
  }

  bool Direct3DSurface9::isBoxValid(const D3D11_BOX* box) {
    if (box == nullptr)
      return true;

    if (box->front != 0 || box->back != 1)
      return false;

    RECT rect;
    rect.left = box->left;
    rect.top = box->top;
    rect.right = box->right;
    rect.bottom = box->bottom;
    return this->isRectValid(&rect);
  }

  HRESULT Direct3DSurface9::Create(Direct3DDevice9Ex* device,
                                   UINT width,
                                   UINT height,
                                   DWORD usage,
                                   D3DFORMAT format,
                                   D3DMULTISAMPLE_TYPE multisample,
                                   BOOL discard,
                                   Direct3DSurface9** outSurface) {
    InitReturnPtr(outSurface);

    if (width == 0)
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: width was 0.");

    if (height == 0)
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: height was 0.");

    if (!device->checkFormat(usage, D3DRTYPE_SURFACE, format))
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: unsupported format (%d).", format);

    if (!outSurface)
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: outSurface was nullptr.");

    D3D11_TEXTURE2D_DESC desc;
    desc.Width = width;
    desc.Height = height;
    desc.Format = convert::format(format);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;

    desc.SampleDesc.Count = std::clamp((UINT)multisample, 1u, 16u);
    desc.SampleDesc.Quality = 0;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    if (!(usage & D3DUSAGE_DEPTHSTENCIL))
      desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    desc.BindFlags |= (usage & D3DUSAGE_RENDERTARGET) ? D3D11_BIND_RENDER_TARGET : 0;
    desc.BindFlags |= (usage & D3DUSAGE_DEPTHSTENCIL) ? D3D11_BIND_DEPTH_STENCIL : 0;

    Com<ID3D11Texture2D> texture;
    HRESULT result = device->GetD3D11Device()->CreateTexture2D(&desc, nullptr, &texture);

    if (result == E_OUTOFMEMORY)
      return log::d3derr(D3DERR_OUTOFVIDEOMEMORY, "Direct3DSurface9::Create: out of vram.");

    if (FAILED(result))
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: failed to create D3D11 texture. D3DFORMAT: %d, DXGI_FORMAT: %d", format, desc.Format); // TODO: stringify

    DXUPResource * resource = DXUPResource::Create(device, texture.ptr(), usage, format);
    if (resource == nullptr)
      return log::d3derr(D3DERR_INVALIDCALL, "Direct3DSurface9::Create: failed to create DXUP resource.");

    D3D9ResourceDesc d3d9Desc;
    d3d9Desc.Format = format;
    d3d9Desc.Pool = D3DPOOL_DEFAULT;
    d3d9Desc.Usage = usage;
    d3d9Desc.Discard = discard;

    *outSurface = ref(Direct3DSurface9::Wrap(0, 0, device, device, resource, d3d9Desc));

    return D3D_OK;
  }

  Direct3DSurface9* Direct3DSurface9::Wrap(UINT slice, UINT mip, Direct3DDevice9Ex* device, IUnknown* container, DXUPResource* resource, const D3D9ResourceDesc& desc) {
    return new Direct3DSurface9(slice, mip, device, container, resource, desc);
  }
}