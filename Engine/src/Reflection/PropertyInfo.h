#pragma once

#include "Reflection/PropertyTypes.h"
#include <cstddef>

namespace Engine
{

    struct PropertyInfo
    {
        const char*   Name;
        const char*   DisplayName;
        PropertyType  Type;
        size_t        Offset;
        PropertyHints Hints;
    };

} // namespace Engine
