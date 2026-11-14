#pragma once

#include "d3d9_base.h"
#include "d3d9_device_unknown.h"
#include <vector>

namespace dxup {

  class Direct3DVertexDeclaration9 : public D3D9DeviceUnknown<IDirect3DVertexDeclaration9> {

  public:

    Direct3DVertexDeclaration9(Direct3DDevice9Ex* device, std::vector<D3D11_INPUT_ELEMENT_DESC>& d3d11Descs, std::vector<D3DVERTEXELEMENT9>& d3d9Descs)
      : D3D9DeviceUnknown<IDirect3DVertexDeclaration9>{ device }
      , m_d3d11Descs{ d3d11Descs }
      , m_d3d9Descs{ d3d9Descs } { }

    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* pElement, UINT* pNumElements) override {
      if (pNumElements == nullptr)
        return D3DERR_INVALIDCALL;

      const UINT required = static_cast<UINT>(m_d3d9Descs.size()) + 1; // D3DDECL_END
      if (pElement == nullptr) {
        *pNumElements = required;
        return D3D_OK;
      }
      if (*pNumElements < required) {
        *pNumElements = required;
        return D3DERR_INVALIDCALL;
      }
      if (!m_d3d9Descs.empty())
        std::memcpy(pElement, m_d3d9Descs.data(), sizeof(D3DVERTEXELEMENT9) * m_d3d9Descs.size());
      pElement[m_d3d9Descs.size()] = D3DDECL_END();
      *pNumElements = required;
      return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
      InitReturnPtr(ppv);

      if (ppv == nullptr)
        return E_POINTER;

      if (riid == __uuidof(IDirect3DVertexDeclaration9) || riid == __uuidof(IUnknown))
        *ppv = ref(this);

      return E_NOINTERFACE;
    }

    void SetFVF(DWORD fvf) { m_fvf = fvf; }
    DWORD GetFVF() const { return m_fvf; }
    const std::vector<D3DVERTEXELEMENT9>& GetD3D9Descs() const { return m_d3d9Descs; }

    const std::vector<D3D11_INPUT_ELEMENT_DESC>& GetD3D11Descs() const {
      return m_d3d11Descs;
    }

  private:
    DWORD m_fvf = 0;
    std::vector<D3D11_INPUT_ELEMENT_DESC> m_d3d11Descs;
    std::vector<D3DVERTEXELEMENT9> m_d3d9Descs;

  };

}