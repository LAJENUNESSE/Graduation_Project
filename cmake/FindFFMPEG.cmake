# FindFFMPEG.cmake — pkg-config fallback for platforms without vcpkg
# When vcpkg is used, this module is skipped so vcpkg's own FindFFMPEG takes over.
# On non-vcpkg builds (macOS Homebrew, Alpine apk, etc.), find FFmpeg via pkg-config.
# Sets: FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARIES, FFMPEG_LIBRARY_DIRS

# vcpkg 环境下直接 include vcpkg 自带的 FindFFMPEG
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    set(_ffmpeg_vcpkg_find "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg/FindFFMPEG.cmake")
    if(EXISTS "${_ffmpeg_vcpkg_find}")
        include("${_ffmpeg_vcpkg_find}")
    endif()
    unset(_ffmpeg_vcpkg_find)
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
