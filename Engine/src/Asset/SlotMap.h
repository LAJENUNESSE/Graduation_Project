#pragma once

#include "Core/Base.h"
#include "Asset/AssetHandle.h"

#include <vector>
#include <string>

namespace Engine
{

    template <typename T>
    class SlotMap
    {
    public:
        SlotMap()
        {
            // Slot 0 is reserved as invalid (generation=0 means never used)
            m_Slots.push_back({nullptr, "", 0});
        }

        AssetHandle Insert(Ref<T> resource, const std::string& path)
        {
            uint32_t index;
            if (!m_FreeList.empty())
            {
                index = m_FreeList.back();
                m_FreeList.pop_back();
                m_Slots[index].Resource = std::move(resource);
                m_Slots[index].Path = path;
                // Generation was already incremented on Remove
            }
            else
            {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.push_back({std::move(resource), path, 1});
            }
            return {index, m_Slots[index].Generation};
        }

        T* Get(AssetHandle h) const
        {
            if (h.Index >= m_Slots.size())
                return nullptr;
            auto& slot = m_Slots[h.Index];
            if (slot.Generation != h.Generation)
                return nullptr;
            return slot.Resource.get();
        }

        Ref<T> GetRef(AssetHandle h) const
        {
            if (h.Index >= m_Slots.size())
                return nullptr;
            auto& slot = m_Slots[h.Index];
            if (slot.Generation != h.Generation)
                return nullptr;
            return slot.Resource;
        }

        void Replace(AssetHandle h, Ref<T> resource)
        {
            if (h.Index >= m_Slots.size())
                return;
            auto& slot = m_Slots[h.Index];
            if (slot.Generation != h.Generation)
                return;
            slot.Resource = std::move(resource);
        }

        void Remove(AssetHandle h)
        {
            if (h.Index >= m_Slots.size())
                return;
            auto& slot = m_Slots[h.Index];
            if (slot.Generation != h.Generation)
                return;
            slot.Resource.reset();
            slot.Path.clear();
            slot.Generation++;
            m_FreeList.push_back(h.Index);
        }

        const std::string& GetPath(AssetHandle h) const
        {
            static const std::string empty;
            if (h.Index >= m_Slots.size())
                return empty;
            auto& slot = m_Slots[h.Index];
            if (slot.Generation != h.Generation)
                return empty;
            return slot.Path;
        }

        size_t Size() const { return m_Slots.size() - 1; } // exclude reserved slot 0

        void Clear()
        {
            m_Slots.clear();
            m_FreeList.clear();
            // Re-reserve slot 0
            m_Slots.push_back({nullptr, "", 0});
        }

    private:
        struct Slot
        {
            Ref<T> Resource;
            std::string Path;
            uint32_t Generation = 1;
        };

        std::vector<Slot> m_Slots;
        std::vector<uint32_t> m_FreeList;
    };

} // namespace Engine
