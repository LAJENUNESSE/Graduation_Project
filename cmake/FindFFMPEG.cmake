# FindFFMPEG.cmake — pkg-config fallback for platforms without vcpkg
# When vcpkg is used, this module is skipped so vcpkg's own FindFFMPEG takes over.
# On non-vcpkg builds (macOS Homebrew, Alpine apk, etc.), find FFmpeg via pkg-config.
# Sets: FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARIES, FFMPEG_LIBRARY_DIRS

# vcpkg 环境下让 vcpkg 自己的 FindFFMPEG 接管
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswresample
    libswscale
)
