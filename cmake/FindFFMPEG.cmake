# FindFFMPEG.cmake — pkg-config fallback for platforms without vcpkg
# When vcpkg is not used (macOS Homebrew, Alpine apk, etc.), find FFmpeg
# via pkg-config. Sets the same variables as vcpkg's FFMPEGConfig.cmake:
#   FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARIES, FFMPEG_LIBRARY_DIRS

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswresample
    libswscale
)
