#pragma once

#include <d3dcompiler.h>

namespace dxup {

  namespace d3dcompiler {

    bool compile(HRESULT* result, const char* source, const char* entry, const char* profile, ID3DBlob** bytecode, ID3DBlob** errors);
    bool disassemble(HRESULT* result, LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags, LPCSTR szComments, ID3DBlob** ppDisassembly);

  }

  namespace d3dx {

    bool dissasembleShader(HRESULT* result, LPCVOID pShader, BOOL EnableColorCode, LPCSTR pComments, ID3DBlob** ppDisassembly);

  }

}