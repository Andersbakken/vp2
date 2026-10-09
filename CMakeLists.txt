cmake_minimum_required(VERSION 3.16...3.31)
project(vp2)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 17)

option(VP2_WITH_PDF "Enable PDF rendering via Poppler" ON)
option(VP2_WITH_VIDEO "Enable video playback via FFmpeg" ON)
option(VP2_WITH_MAGIC "Enable content-based file type detection via libmagic" ON)

# Homebrew's prefix differs between Apple Silicon and Intel Macs. Discover Qt 6
# from the active Homebrew installation so CMake can find keg-only packages.
if(APPLE)
    find_program(VP2_BREW_EXECUTABLE brew HINTS /opt/homebrew/bin /usr/local/bin)
    if(VP2_BREW_EXECUTABLE)
        execute_process(
            COMMAND "${VP2_BREW_EXECUTABLE}" --prefix qt
            RESULT_VARIABLE VP2_BREW_QT_RESULT
            OUTPUT_VARIABLE VP2_BREW_QT_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(VP2_BREW_QT_RESULT EQUAL 0 AND EXISTS "${VP2_BREW_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
            list(PREPEND CMAKE_PREFIX_PATH "${VP2_BREW_QT_PREFIX}")
        endif()
    endif()
endif()

find_package(Qt6 6.0 COMPONENTS Widgets Network REQUIRED)
find_package(PkgConfig)

set(VP2_EXTRA_LIBS)
set(VP2_EXTRA_INCLUDES)
set(VP2_EXTRA_DEFINES)

if(VP2_WITH_PDF)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "VP2_WITH_PDF requires pkg-config")
    endif()

    # poppler-qt6 is keg-only in Homebrew, so make its .pc file visible to
    # pkg-config without requiring users to set PKG_CONFIG_PATH themselves.
    if(APPLE AND VP2_BREW_EXECUTABLE)
        execute_process(
            COMMAND "${VP2_BREW_EXECUTABLE}" --prefix poppler-qt6
            RESULT_VARIABLE VP2_BREW_POPPLER_QT6_RESULT
            OUTPUT_VARIABLE VP2_BREW_POPPLER_QT6_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        set(VP2_BREW_POPPLER_QT6_PKGCONFIG_DIR "${VP2_BREW_POPPLER_QT6_PREFIX}/lib/pkgconfig")
        if(VP2_BREW_POPPLER_QT6_RESULT EQUAL 0
           AND EXISTS "${VP2_BREW_POPPLER_QT6_PKGCONFIG_DIR}/poppler-qt6.pc")
            if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
                set(ENV{PKG_CONFIG_PATH}
                    "${VP2_BREW_POPPLER_QT6_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}")
            else()
                set(ENV{PKG_CONFIG_PATH} "${VP2_BREW_POPPLER_QT6_PKGCONFIG_DIR}")
            endif()
        endif()
    endif()

    pkg_check_modules(POPPLER_QT6 IMPORTED_TARGET poppler-qt6)
    if(POPPLER_QT6_FOUND)
        list(APPEND VP2_EXTRA_LIBS PkgConfig::POPPLER_QT6)
        list(APPEND VP2_EXTRA_DEFINES PDF_ENABLED)
        message(STATUS "PDF support: poppler-qt6 ${POPPLER_QT6_VERSION}")
    else()
        message(WARNING "VP2_WITH_PDF=ON but poppler-qt6 not found; PDF disabled. Install its development package (on macOS: brew install poppler-qt6).")
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
target_link_libraries(vp2 Qt6::Widgets Qt6::Network ${VP2_EXTRA_LIBS})
target_include_directories(vp2 PRIVATE ${VP2_EXTRA_INCLUDES})
target_compile_definitions(vp2 PRIVATE ${VP2_EXTRA_DEFINES})
