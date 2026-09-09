file(GLOB_RECURSE SERVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(Server ${SERVER_SOURCES})

target_link_libraries(Server PRIVATE Engine)

target_include_directories(Server PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

engine_apply_warnings(Server)
engine_apply_sanitizers(Server)

if(MSVC)
    target_compile_options(Server PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()
