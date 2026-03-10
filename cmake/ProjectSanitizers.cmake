include_guard(GLOBAL)

function(engine_apply_sanitizers target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_apply_sanitizers: target '${target}' does not exist")
    endif()

    if(NOT ENGINE_ENABLE_ASAN AND NOT ENGINE_ENABLE_UBSAN)
        return()
    endif()

    if(MSVC)
        if(ENGINE_ENABLE_UBSAN)
            message(FATAL_ERROR "ENGINE_ENABLE_UBSAN is not supported with MSVC")
        endif()

        if(ENGINE_ENABLE_ASAN)
            message(FATAL_ERROR "ENGINE_ENABLE_ASAN is not supported with the current MSVC toolchain. Install the Visual Studio AddressSanitizer runtime or use a Clang/LLVM toolchain.")
        endif()
        return()
    endif()

    set(sanitizer_flags "")

    if(ENGINE_ENABLE_ASAN)
        list(APPEND sanitizer_flags -fsanitize=address)
    endif()

    if(ENGINE_ENABLE_UBSAN)
        list(APPEND sanitizer_flags -fsanitize=undefined)
    endif()

    target_compile_options(${target} PRIVATE ${sanitizer_flags})
    target_link_options(${target} PRIVATE ${sanitizer_flags})
endfunction()
