#pragma once

class MemoryBuffer
{
public:

    MemoryBuffer(size_t a_Size = 0);
    ~MemoryBuffer();

    unsigned long long& operator*();

    template<typename DataType>
    void Write(const DataType& a_Data, size_t a_Offset = 0)
    {
        Write(&a_Data, sizeof(DataType), a_Offset);
    }

    void Write(const void* a_Data, size_t a_Size, size_t a_Offset);

    void Read(void* a_Dst, size_t a_ReadSize, size_t a_SrcOffset) const;

    void CopyFrom(MemoryBuffer a_MemoryBuffer, size_t a_Size, size_t a_DstOffset = 0, size_t a_SrcOffset = 0);

    void Resize(size_t a_NewSize);

    size_t GetSize() const;

private:
    union
    {
        unsigned long long m_CudaPtr;
        void* m_DevPtr;        
    };
    size_t m_Size;
};