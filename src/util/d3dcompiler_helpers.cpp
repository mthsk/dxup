#include "d3dcompiler_helpers.h"
#include <cstring>

namespace dxup {

  namespace d3dcompiler {

    HMODULE d3dcompilerModule = nullptr;

    void loadModule() {
      if (d3dcompilerModule == nullptr)
        d3dcompilerModule = LoadLibraryA("d3dcompiler.dll");

      if (d3dcompilerModule == nullptr)
          d3dcompilerModule = LoadLibraryA("d3dcompiler_47.dll");

      if (d3dcompilerModule == nullptr)
          d3dcompilerModule = LoadLibraryA("d3dcompiler_43.dll");
    }

    bool compile(HRESULT* result, const char* source, const char* entry,
                 const char* profile, ID3DBlob** bytecode, ID3DBlob** errors) {
      loadModule();
      if (!d3dcompilerModule) return false;
      using CompileProc = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
        const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
        ID3DBlob**, ID3DBlob**);
      auto proc = reinterpret_cast<CompileProc>(GetProcAddress(d3dcompilerModule, "D3DCompile"));
      if (!proc) return false;
      *result = proc(source, std::strlen(source), "dxup fixed function",
                     nullptr, nullptr, entry, profile, 0, 0, bytecode, errors);
      return true;
    }

    bool disassemble(HRESULT* result, LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags, LPCSTR szComments, ID3DBlob** ppDisassembly) {
      loadModule();

      if (!d3dcompilerModule)
        return false;

      typedef HRESULT(WINAPI *D3DDisassembleFunc)(LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags, LPCSTR szComments, ID3DBlob** ppDisassembly);
      D3DDisassembleFunc D3DDisassembleDynamic = (D3DDisassembleFunc)GetProcAddress(d3dcompilerModule, "D3DDisassemble");

      HRESULT foundResult = D3DDisassembleDynamic(pSrcData, SrcDataSize, Flags, szComments, ppDisassembly);
      *result = foundResult;

      return true;
    }

  }

  namespace d3dx {

    HMODULE d3dxModule = nullptr;

    void loadModule() {
      if (d3dxModule == nullptr)
        d3dxModule = LoadLibraryA("d3dx9.dll");

      if (d3dxModule == nullptr)
        d3dxModule = LoadLibraryA("D3DX9_40.dll");

      if (d3dxModule == nullptr)
        d3dxModule = LoadLibraryA("D3DX9_36.dll");

      if (d3dxModule == nullptr)
        d3dxModule = LoadLibraryA("d3dx9_32.dll");
    }

    bool dissasembleShader(HRESULT* result, LPCVOID pShader, BOOL EnableColorCode, LPCSTR pComments, ID3DBlob** ppDisassembly) {
      loadModule();

      if (!d3dxModule)
        return false;

      typedef HRESULT(WINAPI *D3DXDisassembleShaderFunc)(const DWORD* pShader, BOOL EnableColorCode, LPCSTR pComments, ID3DBlob** ppDisassembly); // It's not an ID3DBlob but the vtables match up.
      D3DXDisassembleShaderFunc D3DXDisassembleShaderDynamic = (D3DXDisassembleShaderFunc)GetProcAddress(d3dxModule, "D3DXDisassembleShader");

      HRESULT foundResult = D3DXDisassembleShaderDynamic((const DWORD*)pShader, EnableColorCode, pComments, ppDisassembly);
      *result = foundResult;

      return true;
    }

  }

}