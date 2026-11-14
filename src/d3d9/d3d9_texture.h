#pragma once

#include "d3d9_d3d11_resource.h"
#include "d3d9_resource.h"
#include "d3d9_surface.h"
#include "d3d9_volume.h"
#include "d3d9_mipmap.h"
#include <vector>

namespace dxup {

  template <D3DRESOURCETYPE ResourceType, typename ID3D9BaseType>
  class Direct3DBaseTexture9 : public Direct3DResource9<ResourceType, ID3D9BaseType> {

  public:

    Direct3DBaseTexture9(Direct3DDevice9Ex* pDevice, DXUPResource* resource, const D3D9ResourceDesc& d3d9Desc)
      : Direct3DResource9<ResourceType, ID3D9BaseType>{ pDevice, resource, d3d9Desc }  {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) {
      InitReturnPtr(ppvObj);

      if (ppvObj == nullptr)
        return E_POINTER;

      if (riid == __uuidof(ID3D9BaseType) || riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(IDirect3DResource9) || riid == __uuidof(IUnknown)) {
        *ppvObj = ref(this);
        return D3D_OK;
      }

      return E_NOINTERFACE;
    }

    DWORD STDMETHODCALLTYPE GetLevelCount() override {
      CriticalSection cs(this->GetD3D9Device());
      // D3D9 only exposes level zero of an autogen mip chain to the application.
      return (this->m_d3d9Desc.Usage & D3DUSAGE_AUTOGENMIPMAP)
        ? 1 : this->GetDXUPResource()->GetMips();
    }

    void STDMETHODCALLTYPE GenerateMipSubLevels() override {
      CriticalSection cs(this->GetD3D9Device());
      if (!(this->m_d3d9Desc.Usage & D3DUSAGE_AUTOGENMIPMAP))
        return;

      ID3D11ShaderResourceView* srv = this->GetDXUPResource()->GetSRV(false);
      if (srv != nullptr)
        this->GetContext()->GenerateMips(srv);
      else
        log::warn("GenerateMipSubLevels called on an autogen texture with no SRV.");
    }

    DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override {
      CriticalSection cs(this->GetD3D9Device());
      if (this->m_d3d9Desc.Pool != D3DPOOL_MANAGED)
        return 0;

      m_lod = mipmap::clampLOD(LODNew, this->GetDXUPResource()->GetMips());
      // A texture may be bound at multiple pixel/vertex sampler stages.
      this->GetD3D9Device()->MarkTextureLODDirty(this);
      return m_lod;
    }
    DWORD STDMETHODCALLTYPE GetLOD() override {
      CriticalSection cs(this->GetD3D9Device());
      return this->m_d3d9Desc.Pool == D3DPOOL_MANAGED ? m_lod : 0;
    }

    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override {
      CriticalSection cs(this->GetD3D9Device());
      if (!(this->m_d3d9Desc.Usage & D3DUSAGE_AUTOGENMIPMAP))
        return D3DERR_INVALIDCALL;

      // D3D11 GenerateMips implements linear filtering only. Do not silently
      // claim point/anisotropic/other generation algorithms are supported.
      if (FilterType != D3DTEXF_LINEAR)
        return D3DERR_INVALIDCALL;

      m_autoGenFilter = FilterType;
      return D3D_OK;
    }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override {
      CriticalSection cs(this->GetD3D9Device());
      return (this->m_d3d9Desc.Usage & D3DUSAGE_AUTOGENMIPMAP)
        ? m_autoGenFilter : D3DTEXF_NONE;
    }

  private:
    DWORD m_lod = 0;
    D3DTEXTUREFILTERTYPE m_autoGenFilter = D3DTEXF_LINEAR;

  };

  using Direct3DTexture9Base = Direct3DBaseTexture9<D3DRTYPE_TEXTURE, IDirect3DTexture9>;
  class Direct3DTexture9 final : public Direct3DTexture9Base {

  public:

    ~Direct3DTexture9();

    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT* pDirtyRect) override;

    static HRESULT Create(Direct3DDevice9Ex* device,
                          UINT width,
				          UINT height,
                          UINT levels,
                          DWORD usage,
                          D3DFORMAT format,
                          D3DPOOL pool,
                          Direct3DTexture9** outTexture);

  private:

	Direct3DTexture9(Direct3DDevice9Ex* device, DXUPResource* resource, const D3D9ResourceDesc& desc);

    std::vector<IDirect3DSurface9*> m_surfaces;

  };

  using Direct3DCubeTexture9Base = Direct3DBaseTexture9<D3DRTYPE_CUBETEXTURE, IDirect3DCubeTexture9>;
  class Direct3DCubeTexture9 final : public Direct3DCubeTexture9Base {

  public:

    ~Direct3DCubeTexture9();

    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9** ppCubeMapSurface) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT* pDirtyRect) override;

    static HRESULT Create(Direct3DDevice9Ex* device,
                          UINT edgeLength,
                          UINT levels,
                          DWORD usage,
                          D3DFORMAT format,
                          D3DPOOL pool,
                          Direct3DCubeTexture9** outTexture);

  private:

    Direct3DCubeTexture9(Direct3DDevice9Ex* device, DXUPResource* resource, const D3D9ResourceDesc& desc);

    std::vector<IDirect3DSurface9*> m_surfaces;

  };

  using Direct3DVolumeTexture9Base = Direct3DBaseTexture9<D3DRTYPE_VOLUMETEXTURE, IDirect3DVolumeTexture9>;
  class Direct3DVolumeTexture9 final : public Direct3DVolumeTexture9Base {

  public:

    ~Direct3DVolumeTexture9();

    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DVOLUME_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT Level, IDirect3DVolume9** ppVolumeLevel) override;
    HRESULT STDMETHODCALLTYPE LockBox(UINT Level, D3DLOCKED_BOX* pLockedBox, const D3DBOX* pBox, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX* pDirtyBox) override;

    static HRESULT Create(Direct3DDevice9Ex* device,
                          UINT width,
                          UINT height,
                          UINT depth,
                          UINT levels,
                          DWORD usage,
                          D3DFORMAT format,
                          D3DPOOL pool,
                          Direct3DVolumeTexture9** outTexture);

  private:

    Direct3DVolumeTexture9(Direct3DDevice9Ex* device, DXUPResource* resource, const D3D9ResourceDesc& desc);

    std::vector<IDirect3DVolume9*> m_volumes;

  };

}