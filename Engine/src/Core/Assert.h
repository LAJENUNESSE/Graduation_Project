#pragma once

#include "Core/Log.h"

#ifdef _MSC_VER
    #define ENGINE_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define ENGINE_DEBUGBREAK() raise(SIGTRAP)
#endif

#define ENGINE_ASSERT(check, ...)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(check))                                                                                                  \
        {                                                                                                              \
            ENGINE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                        \
            ENGINE_DEBUGBREAK();                                                                                       \
        }                                                                                                              \
    } while (false)

#define ENGINE_CORE_ASSERT(check, ...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(check))                                                                                                  \
        {                                                                                                              \
            ENGINE_CORE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                   \
            ENGINE_DEBUGBREAK();                                                                                       \
        }                                                                                                              \
    } while (false)
