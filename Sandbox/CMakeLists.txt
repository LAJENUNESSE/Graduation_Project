file(GLOB_RECURSE SANDBOX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(Sandbox ${SANDBOX_SOURCES})

target_link_libraries(Sandbox PRIVATE Engine)

target_include_directories(Sandbox PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
)

engine_apply_warnings(Sandbox)
engine_apply_sanitizers(Sandbox)

