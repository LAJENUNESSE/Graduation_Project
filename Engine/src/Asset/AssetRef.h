#pragma once

#include "Asset/AssetHandle.h"

namespace Engine
{

    // Forward declaration — AssetManager is defined elsewhere
    class AssetManager;

    template <typename T> class AssetRef
    {
    public:
        AssetRef() = default;
        explicit AssetRef(AssetHandle h) : m_Handle(h) {}

        // Get() is implemented in AssetManager.h after AssetManager is fully defined
        T* Get() const;
        T* operator->() const { return Get(); }

        explicit    operator bool() const { return m_Handle.IsValid(); }
        AssetHandle GetHandle() const { return m_Handle; }
        bool        IsValid() const { return m_Handle.IsValid(); }

    private:
        AssetHandle m_Handle = {};
    };

} // namespace Engine
