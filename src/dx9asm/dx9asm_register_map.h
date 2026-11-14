#pragma once
#include "dx9asm_register_mapping.h"
#include "dx9asm_meta.h"

namespace dxup {

  namespace dx9asm {

    class ShaderCodeTranslator;

    struct TransientRegisterMapping {
      uint32_t dxbcRegNum;

      uint32_t d3d9UsageIndex;
      uint32_t d3d9Usage;

      const char* dxbcSemanticName;
      uint32_t dxbcSemanticIndex;
    };

    class RegisterMap {
    public:
      inline void reset() {
        m_registerMap.clear();
        m_highestInternalTemp = UINT32_MAX;
      }

      inline const RegisterMapping* getRegisterMapping(const DX9Operand& operand) const {
        return getRegisterMapping(operand.getRegType(), operand.getRegNumber());
      }

      inline RegisterMapping* getRegisterMapping(const DX9Operand& operand) {
        return getRegisterMapping(operand.getRegType(), operand.getRegNumber());
      }

      inline const RegisterMapping* getRegisterMapping(uint32_t type, uint32_t index) const {
        for (const RegisterMapping& mapping : m_registerMap) {
          if (mapping.dx9Type == type && mapping.dx9Id == index)
            return &mapping;
        }
        return nullptr;
      }

      inline RegisterMapping* getRegisterMapping(uint32_t type, uint32_t index) {
        for (RegisterMapping& mapping : m_registerMap) {
          if (mapping.dx9Type == type && mapping.dx9Id == index)
            return &mapping;
        }
        return nullptr;
      }

      inline uint32_t getHighestIdForDXBCType(uint32_t type) const {
        const uint32_t invalidId = UINT32_MAX;

        uint32_t highestIdForType = invalidId;

        for (const RegisterMapping& lum : m_registerMap) {
          const DXBCOperand& lumOperand = lum.dxbcOperand;

          if (lumOperand.getRegisterType() == type) {
            highestIdForType = highestIdForType == invalidId
              ? lumOperand.getRegNumber()
              : std::max(highestIdForType, lumOperand.getRegNumber());
          }
        }

        return highestIdForType;
      }

      static std::vector<TransientRegisterMapping>& getTransientMappings();

      inline uint32_t getDXBCTypeCount(uint32_t type) const {
        uint32_t highestId = getHighestIdForDXBCType(type);
        if (highestId == UINT32_MAX)
          return 0;

        return highestId + 1;
      }

      RegisterMapping* lookupOrCreateRegisterMapping(const ShaderCodeTranslator& translator, uint32_t regType, uint32_t regNum, uint32_t readMask, uint32_t writeMask, bool readingLikeVSOutput = false);

      RegisterMapping* lookupOrCreateRegisterMapping(const ShaderCodeTranslator& translator, const DX9Operand& operand, uint32_t regOffset = 0);

      uint32_t getTransientId(DclInfo& info);

      inline void addRegisterMapping(bool transient, bool generateDXBCId, RegisterMapping& mapping) {
        DXBCOperand& dxbcOperand = mapping.dxbcOperand;
        if (generateDXBCId) {
          uint32_t& regNumber = dxbcOperand.getRegNumber();

          if (!transient) {
            uint32_t highestIdForType = getHighestIdForDXBCType(dxbcOperand.getRegisterType());

            uint32_t next = highestIdForType == UINT32_MAX ? 0 : highestIdForType + 1;
            if (dxbcOperand.getRegisterType() == D3D10_SB_OPERAND_TYPE_TEMP &&
                m_highestInternalTemp != UINT32_MAX)
              next = std::max(next, m_highestInternalTemp + 1);
            regNumber = next;
          }
          else
            regNumber = getTransientId(mapping.dclInfo);
        }

        m_registerMap.push_back(mapping);
      }

      inline uint32_t getTotalTempCount() {
        uint32_t highestRealTempId = getHighestIdForDXBCType(D3D10_SB_OPERAND_TYPE_TEMP);
        if (highestRealTempId == UINT32_MAX && m_highestInternalTemp == UINT32_MAX)
          return 0;

        if (highestRealTempId == UINT32_MAX)
          return m_highestInternalTemp + 1;

        if (m_highestInternalTemp == UINT32_MAX)
          return highestRealTempId + 1;

        return std::max(highestRealTempId, m_highestInternalTemp) + 1;
      }

      inline DXBCOperand getNextInternalTemp() {
        uint32_t next = getDXBCTypeCount(D3D10_SB_OPERAND_TYPE_TEMP);
        if (m_highestInternalTemp != UINT32_MAX)
          next = std::max(next, m_highestInternalTemp + 1);
        m_highestInternalTemp = next;

        DXBCOperand op{ D3D10_SB_OPERAND_TYPE_TEMP, 1 };
        op.stripModifier();
        op.setRepresentation(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
        op.setData(&m_highestInternalTemp, 1);

        return op;
      }

      inline std::vector<RegisterMapping>& getRegisterMappings() {
        return m_registerMap;
      }
    private:
      uint32_t m_highestInternalTemp = UINT32_MAX;
      std::vector<RegisterMapping> m_registerMap;
    };

  }

}