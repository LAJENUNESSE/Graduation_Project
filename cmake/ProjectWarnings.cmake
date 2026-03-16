include_guard(GLOBAL)

function(engine_apply_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_apply_warnings: target '${target}' does not exist")
    endif()

    if(NOT ENGINE_ENABLE_STRICT_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:/W4>
            $<$<COMPILE_LANGUAGE:C>:/FS>
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/FS>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
        )
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:/WX>
                $<$<COMPILE_LANGUAGE:CXX>:/WX>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/WX>
            )
        endif()
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra,-Wpedantic>
        )
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-Werror>
                $<$<COMPILE_LANGUAGE:CXX>:-Werror>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Werror>
            )
        endif()
    endif()
endfunction()
