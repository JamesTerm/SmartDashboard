#include "sd_direct_shared_memory.h"

namespace sd::direct
{
    SharedMemoryRegion::SharedMemoryRegion() = default;

    SharedMemoryRegion::~SharedMemoryRegion()
    {
        Close();
    }

    bool SharedMemoryRegion::OpenOrCreate(const std::wstring& mappingName, std::size_t bytes, bool& created)
    {
        Close();

        // Win32 named shared-memory segment (cross-process memory transport).
        m_mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>((static_cast<unsigned long long>(bytes) >> 32U) & 0xFFFFFFFFULL),
            static_cast<DWORD>(static_cast<unsigned long long>(bytes) & 0xFFFFFFFFULL),
            mappingName.c_str()
        );
        if (m_mapping == nullptr)
        {
            return false;
        }

        created = (GetLastError() != ERROR_ALREADY_EXISTS);
        m_view = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (m_view == nullptr)
        {
            Close();
            return false;
        }

        m_size = bytes;
        return true;
    }

    void SharedMemoryRegion::Close()
    {
        if (m_view != nullptr)
        {
            UnmapViewOfFile(m_view);
            m_view = nullptr;
        }

        if (m_mapping != nullptr)
        {
            CloseHandle(m_mapping);
            m_mapping = nullptr;
        }

        m_size = 0;
    }

    void* SharedMemoryRegion::Data() const
    {
        return m_view;
    }

    std::size_t SharedMemoryRegion::Size() const
    {
        return m_size;
    }

    NamedEvent::NamedEvent() = default;

    NamedEvent::~NamedEvent()
    {
        Close();
    }

    bool NamedEvent::OpenOrCreateAutoReset(const std::wstring& eventName, bool& created)
    {
        Close();

        // Auto-reset event synchronization primitive (one waiter released per signal).
        m_handle = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
        if (m_handle == nullptr)
        {
            return false;
        }

        created = (GetLastError() != ERROR_ALREADY_EXISTS);
        return true;
    }

    bool NamedEvent::Signal()
    {
        if (m_handle == nullptr)
        {
            return false;
        }

        return SetEvent(m_handle) != FALSE;
    }

    DWORD NamedEvent::Wait(DWORD timeoutMs) const
    {
        if (m_handle == nullptr)
        {
            return WAIT_FAILED;
        }

        return WaitForSingleObject(m_handle, timeoutMs);
    }

    void NamedEvent::Close()
    {
        if (m_handle != nullptr)
        {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
    }

    HANDLE NamedEvent::Handle() const
    {
        return m_handle;
    }
}
