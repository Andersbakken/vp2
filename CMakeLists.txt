cmake_minimum_required(VERSION 3.2...3.31)
project(vp2)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 14)

option(VP2_WITH_PDF "Enable PDF rendering via Poppler" ON)
option(VP2_WITH_VIDEO "Enable video playback via FFmpeg" ON)
option(VP2_WITH_MAGIC "Enable content-based file type detection via libmagic" ON)

# Homebrew on Apple Silicon installs Qt5 here; keg-only so not on default path.
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt@5")

find_package(Qt5 COMPONENTS Widgets Network REQUIRED)
find_package(PkgConfig)

set(VP2_EXTRA_LIBS)
set(VP2_EXTRA_INCLUDES)
set(VP2_EXTRA_DEFINES)

if(VP2_WITH_PDF)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "VP2_WITH_PDF requires pkg-config")
    endif()
    pkg_check_modules(POPPLER_QT5 IMPORTED_TARGET poppler-qt5)
    if(POPPLER_QT5_FOUND)
        list(APPEND VP2_EXTRA_LIBS PkgConfig::POPPLER_QT5)
        list(APPEND VP2_EXTRA_DEFINES PDF_ENABLED)
        message(STATUS "PDF support: poppler-qt5 ${POPPLER_QT5_VERSION}")
    else()
        message(WARNING "VP2_WITH_PDF=ON but poppler-qt5 not found; PDF disabled")
    endif()
endif()

if(VP2_WITH_VIDEO)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "VP2_WITH_VIDEO requires pkg-config")
    endif()
    pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    if(FFMPEG_FOUND)
        list(APPEND VP2_EXTRA_LIBS PkgConfig::FFMPEG)
        list(APPEND VP2_EXTRA_DEFINES VIDEO_ENABLED)
        message(STATUS "Video support: ffmpeg (libavformat ${FFMPEG_libavformat_VERSION})")
    else()
        message(WARNING "VP2_WITH_VIDEO=ON but ffmpeg dev libs not found; video disabled")
    endif()
endif()

if(VP2_WITH_MAGIC)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "VP2_WITH_MAGIC requires pkg-config")
    endif()
    pkg_check_modules(LIBMAGIC IMPORTED_TARGET libmagic)
    if(NOT LIBMAGIC_FOUND)
        # libmagic ships no .pc on some distros; fall back to plain link check.
        find_library(LIBMAGIC_LIBRARY NAMES magic)
        find_path(LIBMAGIC_INCLUDE_DIR NAMES magic.h)
        if(LIBMAGIC_LIBRARY AND LIBMAGIC_INCLUDE_DIR)
            set(LIBMAGIC_FOUND TRUE)
        endif()
    endif()
    if(LIBMAGIC_FOUND)
        if(TARGET PkgConfig::LIBMAGIC)
            list(APPEND VP2_EXTRA_LIBS PkgConfig::LIBMAGIC)
        else()
            list(APPEND VP2_EXTRA_LIBS ${LIBMAGIC_LIBRARY})
            list(APPEND VP2_EXTRA_INCLUDES ${LIBMAGIC_INCLUDE_DIR})
        endif()
        list(APPEND VP2_EXTRA_DEFINES MAGIC_ENABLED)
        message(STATUS "Content-based file type detection: libmagic")
    else()
        message(WARNING "VP2_WITH_MAGIC=ON but libmagic not found; magic disabled")
    endif()
endif()

add_executable(vp2
    flags.h
    magic.cpp magic.h
    main.cpp
    picture.cpp picture.h
    threads.cpp threads.h
    video.cpp video.h
    window.cpp window.h
)
target_link_libraries(vp2 Qt5::Widgets Qt5::Network ${VP2_EXTRA_LIBS})
target_include_directories(vp2 PRIVATE ${VP2_EXTRA_INCLUDES})
target_compile_definitions(vp2 PRIVATE ${VP2_EXTRA_DEFINES})
