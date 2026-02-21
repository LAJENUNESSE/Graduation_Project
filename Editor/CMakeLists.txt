file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(Editor ${EDITOR_SOURCES})

target_link_libraries(Editor PRIVATE Engine)

target_include_directories(Editor PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
