#pragma once

#include <cstdint>
#include <functional>

namespace Engine
{

    struct AssetHandle
    {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool     IsValid() const { return Index != 0 && Generation != 0; }
        bool     operator==(const AssetHandle& o) const { return Index == o.Index && Generation == o.Generation; }
        bool     operator!=(const AssetHandle& o) const { return !(*this == o); }
        explicit operator bool() const { return IsValid(); }
    };

} // namespace Engine

template <> struct std::hash<Engine::AssetHandle>
{
    size_t operator()(const Engine::AssetHandle& h) const noexcept
    {
        size_t seed = std::hash<uint32_t>{}(h.Index);
        seed ^= std::hash<uint32_t>{}(h.Generation) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
