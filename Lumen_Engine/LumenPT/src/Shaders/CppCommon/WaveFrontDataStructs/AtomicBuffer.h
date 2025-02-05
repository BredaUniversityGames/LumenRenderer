#pragma once
#include "../CudaDefines.h"
#include <Cuda/cuda/helpers.h>
#include <cassert>

#include "../../../Framework/MemoryBuffer.h"
#include "cuda_runtime_api.h"

namespace WaveFront
{
    /*
     * Buffer that allows atomic additions on a buffer.
     */
    template<typename T>
    struct AtomicBuffer
    {
        //Functions
    public:
        /*
         * Append data to the buffer.
         */
        GPU_ONLY INLINE void Add(T* a_Data)
        {
            //IMPORTANT: atomicAdd returns old value. https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#arithmetic-functions
            
            const uint32_t index = atomicAdd(&counter, 1);
            assert(index < maxSize);
            data[index] = *a_Data;
        }

        GPU_ONLY void Add(const T& a_Data)
        {
            const uint32_t index = atomicAdd(&counter, 1);
            assert(index < maxSize);
            data[index] = a_Data;
        }

        GPU_ONLY uint32_t ReserveIndices(uint32_t a_NumIndices)
        {
            const uint32_t currentIndex = atomicAdd(&counter, a_NumIndices);
            return currentIndex;
        }

        /*
         * Set data in the buffer, bypassing the atomic operation.
         */
        GPU_ONLY INLINE void Set(int a_Index, T* a_Data)
        {
            assert(a_Index < maxSize);
            data[a_Index] = *a_Data;
        }

        GPU_ONLY void Set(int a_Index, const T& a_Data)
        {
            if(!(a_Index < maxSize && a_Index < counter))
            {
                printf("Index: %i, MaxSize: %i, Counter: %i \n", a_Index, maxSize, counter);
            }
            assert(a_Index < maxSize && a_Index < counter);
            data[a_Index] = a_Data;
        }

        /*
         * Set the value of the counter.
         * This bypasses atomic operations and should only be used when data is added by setting at specific indices.
         */
        GPU_ONLY INLINE void SetCounter(int a_Value)
        {
            assert(a_Value < maxSize);
            counter = a_Value;
        }

        /*
         * Reset the counter back to 0.
         */
        GPU_ONLY INLINE void Reset()
        {
            counter = 0;
        }

        GPU_ONLY INLINE unsigned GetSize() const
        {
            return counter;
        }

        GPU_ONLY INLINE T* GetData(int a_Index)
        {
            assert(a_Index <= counter);
            return &data[a_Index];
        }

        GPU_ONLY INLINE const T* GetData(int a_Index) const
        {
            assert(a_Index <= counter);
            return &data[a_Index];
        }

        //Data
    public:
        uint32_t counter;
        uint32_t maxSize;
        T data[];
    };

    /*
     * Helper function to the set atomic counter on the GPU in an atomic buffer.
     */
    template<typename T>
    void SetAtomicCounter(MemoryBuffer* a_Buffer, unsigned a_Value)
    {
        a_Buffer->Write(&a_Value, sizeof(unsigned), 0);
    }

    /*
     * Helper function to set the atomic counter on the GPU to 0 in an atomic buffer.
     */
    template<typename T>
    void ResetAtomicBuffer(MemoryBuffer* a_Buffer)
    {
        SetAtomicCounter<T>(a_Buffer, 0);
    }

    /*
     * Helper function to create an atomic buffer on the GPU of a certain size.
     */
    template<typename T>
    void CreateAtomicBuffer(MemoryBuffer* a_Buffer, unsigned a_Size)
    {
        a_Buffer->Resize(sizeof(AtomicBuffer<T>) + sizeof(T) * a_Size);
        ResetAtomicBuffer<T>(a_Buffer);
        a_Buffer->Write(&a_Size, sizeof(unsigned), sizeof(unsigned));
    }

    template<typename T>
    unsigned GetAtomicCounter(const MemoryBuffer *const a_Buffer)
    {
        unsigned returned = 0;
        a_Buffer->Read(&returned, sizeof(unsigned), 0);
        return returned;
    }
}