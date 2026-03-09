#pragma once

#include "Core/Log.h"

#include <cstdlib>

#ifdef _MSC_VER
    #define ENGINE_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define ENGINE_DEBUGBREAK() raise(SIGTRAP)
#endif

#ifdef NDEBUG
    #define ENGINE_INTERNAL_DEBUG_TRAP() ((void)0)
#else
    #define ENGINE_INTERNAL_DEBUG_TRAP() ENGINE_DEBUGBREAK()
#endif

#define ENGINE_INTERNAL_VERIFY_IMPL(logMacro, check, ...)                                                                  \
    do                                                                                                                     \
    {                                                                                                                      \
        if (!(check))                                                                                                      \
        {                                                                                                                  \
            logMacro("Verify failed: {0}", __VA_ARGS__);                                                                 \
        }                                                                                                                  \
    } while (false)

#define ENGINE_INTERNAL_RELEASE_ASSERT_IMPL(logMacro, check, ...)                                                         \
    do                                                                                                                     \
    {                                                                                                                      \
        if (!(check))                                                                                                      \
        {                                                                                                                  \
            logMacro("Fatal assertion failed: {0}", __VA_ARGS__);                                                        \
            ENGINE_INTERNAL_DEBUG_TRAP();                                                                                  \
            std::abort();                                                                                                  \
        }                                                                                                                  \
    } while (false)

#define ENGINE_VERIFY(check, ...) ENGINE_INTERNAL_VERIFY_IMPL(ENGINE_ERROR, check, __VA_ARGS__)
#define ENGINE_CORE_VERIFY(check, ...) ENGINE_INTERNAL_VERIFY_IMPL(ENGINE_CORE_ERROR, check, __VA_ARGS__)
#define ENGINE_RELEASE_ASSERT(check, ...) ENGINE_INTERNAL_RELEASE_ASSERT_IMPL(ENGINE_ERROR, check, __VA_ARGS__)
#define ENGINE_CORE_RELEASE_ASSERT(check, ...) ENGINE_INTERNAL_RELEASE_ASSERT_IMPL(ENGINE_CORE_ERROR, check, __VA_ARGS__)

#ifdef NDEBUG
    // Release: log only, no process termination
    #define ENGINE_ASSERT(check, ...)                                                                                      \
        do                                                                                                                 \
        {                                                                                                                  \
            if (!(check))                                                                                                  \
            {                                                                                                              \
                ENGINE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                       \
            }                                                                                                              \
        } while (false)

    #define ENGINE_CORE_ASSERT(check, ...)                                                                                 \
        do                                                                                                                 \
        {                                                                                                                  \
            if (!(check))                                                                                                  \
            {                                                                                                              \
                ENGINE_CORE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                  \
            }                                                                                                              \
        } while (false)
#else
    // Debug: log + break into debugger
    #define ENGINE_ASSERT(check, ...)                                                                                      \
        do                                                                                                                 \
        {                                                                                                                  \
            if (!(check))                                                                                                  \
            {                                                                                                              \
                ENGINE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                       \
                ENGINE_DEBUGBREAK();                                                                                       \
            }                                                                                                              \
        } while (false)

    #define ENGINE_CORE_ASSERT(check, ...)                                                                                 \
        do                                                                                                                 \
        {                                                                                                                  \
            if (!(check))                                                                                                  \
            {                                                                                                              \
                ENGINE_CORE_ERROR("Assertion failed: {0}", __VA_ARGS__);                                                  \
                ENGINE_DEBUGBREAK();                                                                                       \
            }                                                                                                              \
        } while (false)
#endif