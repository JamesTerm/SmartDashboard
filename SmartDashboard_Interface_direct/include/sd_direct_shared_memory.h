#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>

namespace sd::direct
{
    class SharedMemoryRegion final
    {
    public:
        SharedMemoryRegion();
        ~SharedMemoryRegion();

        SharedMemoryRegion(const SharedMemoryRegion&) = delete;
        SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

        bool OpenOrCreate(const std::wstring& mappingName, std::size_t bytes, bool& created);
        void Close();

        void* Data() const;
        std::size_t Size() const;

    private:
        HANDLE m_mapping = nullptr;
        void* m_view = nullptr;
        std::size_t m_size = 0;
    };

    class NamedEvent final
    {
    public:
        NamedEvent();
        ~NamedEvent();

        NamedEvent(const NamedEvent&) = delete;
        NamedEvent& operator=(const NamedEvent&) = delete;

        bool OpenOrCreateAutoReset(const std::wstring& eventName, bool& created);
        bool Signal();
        DWORD Wait(DWORD timeoutMs) const;
        void Close();

        HANDLE Handle() const;

    private:
        HANDLE m_handle = nullptr;
    };
}
