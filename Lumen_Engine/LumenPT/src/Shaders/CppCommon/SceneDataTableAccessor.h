#pragma once

#include "CudaDefines.h"

#include <cstdint>

class SceneDataTableAccessor
{
public:
    SceneDataTableAccessor(int a_Stride, void* a_MemoryBuffer)
        : m_Stride(a_Stride)
        , m_MemoryBuffer(a_MemoryBuffer)
    {}

    template <class InterpretationType = void>
    CPU_GPU InterpretationType* GetTableEntry(uint32_t a_Index)
    {
        return reinterpret_cast<InterpretationType*>(reinterpret_cast<uint64_t>(m_MemoryBuffer)
        + a_Index * m_Stride);
    }

    template <class InterpretationType = void>
    CPU_GPU const InterpretationType* GetTableEntry(uint32_t a_Index) const
    {
        return reinterpret_cast<const InterpretationType*>(reinterpret_cast<uint64_t>(m_MemoryBuffer) + a_Index * m_Stride);
    }

private:
    long long int m_Stride;
    void* m_MemoryBuffer;

};