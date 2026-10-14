cmake_minimum_required(VERSION 3.24)

project(kin-galeri
    VERSION 0.1.0
    DESCRIPTION "Kin Galeri - fast local photo & video library for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Sql
    Concurrent
    Network
)

find_package(SQLite3 REQUIRED)

# ---------------------------------------------------------------------------
# FFmpeg (video poster frames)
#
# Used directly rather than via Qt Multimedia so frame extraction also works
# headless, with no GL context, inside the ThumbnailService worker pool.
# ---------------------------------------------------------------------------
option(KG_ENABLE_VIDEO_THUMBS "Extract video poster frames with FFmpeg" ON)

if(KG_ENABLE_VIDEO_THUMBS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KG_FFMPEG QUIET IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale)
    endif()

    if(TARGET PkgConfig::KG_FFMPEG)
        message(STATUS "kin-galeri: FFmpeg found (${KG_FFMPEG_libavformat_VERSION}) - video poster frames enabled")
    else()
        message(STATUS "kin-galeri: FFmpeg dev libs NOT found - video frames disabled")
        set(KG_ENABLE_VIDEO_THUMBS OFF)
    endif()
endif()

# CMake renamed the imported target; support both spellings.
if(TARGET SQLite3::SQLite3)
    set(KG_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(KG_SQLITE_TARGET SQLite::SQLite3)
endif()

# ---------------------------------------------------------------------------
# Optional ML backend (M3 face grouping)
#
# ONNX Runtime is deliberately optional: the core gallery never needs it, and a
# default build stays dependency-free. When enabled, the face backend is compiled
# in and the Settings UI can turn it on; without it, the Settings page says so.
#
# Point KG_ORT_ROOT at an extracted onnxruntime-linux-x64-* archive (the CPU
# build, ~17 MB), or have it installed system-wide.
# ---------------------------------------------------------------------------
option(KG_ENABLE_ML "Enable face grouping via ONNX Runtime" OFF)
if(KG_ENABLE_ML)
    set(KG_ORT_ROOT "" CACHE PATH "Path to an extracted ONNX Runtime distribution")
    find_path(KG_ORT_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS "${KG_ORT_ROOT}/include" "$ENV{ONNXRUNTIME_ROOT}/include")
    find_library(KG_ORT_LIB onnxruntime
        HINTS "${KG_ORT_ROOT}/lib" "$ENV{ONNXRUNTIME_ROOT}/lib")
    if(NOT KG_ORT_INCLUDE_DIR OR NOT KG_ORT_LIB)
        message(WARNING "kin-galeri: ONNX Runtime not found - ML disabled (set -DKG_ORT_ROOT=...)")
        set(KG_ENABLE_ML OFF)
    else()
        message(STATUS "kin-galeri: ONNX Runtime found - face grouping enabled")
    endif()
endif()

# Optional: video playback. Off by default in M0.
option(KG_ENABLE_MULTIMEDIA "Enable Qt Multimedia video playback" ON)

# Video backend switch (PRD 5.7). "mpv" uses libmpv via mpvqt and takes
# precedence when available; "qtmultimedia" is the fallback and the PRD 10.1
# default. Both may be compiled in; only one drives the viewer.
set(KG_VIDEO_BACKEND "auto" CACHE STRING "Video backend: auto | mpv | qtmultimedia")
set_property(CACHE KG_VIDEO_BACKEND PROPERTY STRINGS auto mpv qtmultimedia)

# --- libmpv backend -------------------------------------------------------
# mpvqt ships libMpvQt.so + headers but NO qmldir/plugin/QML type, so the type is
# registered by hand in main.cpp. On Arch/Artix libmpv is provided by the `mpv`
# package; there is no package named libmpv. Debian: libmpv-dev. Fedora: mpv-libs-devel.
option(KG_ENABLE_MPV "Enable libmpv video playback (requires mpvqt)" ON)

if(KG_ENABLE_MPV AND NOT KG_VIDEO_BACKEND STREQUAL "qtmultimedia")
    find_package(MpvQt QUIET)
    if(MpvQt_FOUND)
        message(STATUS "kin-galeri: mpvqt found - libmpv backend available")
    else()
        message(STATUS "kin-galeri: mpvqt NOT found - libmpv backend disabled")
        set(KG_ENABLE_MPV OFF)
    endif()
else()
    set(KG_ENABLE_MPV OFF)
endif()

# Resolve "auto" now that availability is known: prefer mpv when built in.
if(KG_VIDEO_BACKEND STREQUAL "auto")
    if(KG_ENABLE_MPV)
        set(KG_VIDEO_BACKEND "mpv")
    elseif(KG_ENABLE_MULTIMEDIA)
        set(KG_VIDEO_BACKEND "qtmultimedia")
    else()
        set(KG_VIDEO_BACKEND "none")
    endif()
endif()

if(KG_VIDEO_BACKEND STREQUAL "mpv" AND NOT KG_ENABLE_MPV)
    message(WARNING "kin-galeri: KG_VIDEO_BACKEND=mpv but mpvqt is unavailable; falling back")
    if(KG_ENABLE_MULTIMEDIA)
        set(KG_VIDEO_BACKEND "qtmultimedia")
    else()
        set(KG_VIDEO_BACKEND "none")
    endif()
endif()

if(KG_ENABLE_MULTIMEDIA)
    find_package(Qt6 6.5 QUIET COMPONENTS Multimedia)
    if(Qt6Multimedia_FOUND)
        message(STATUS "kin-galeri: Qt Multimedia found (backend: ${KG_VIDEO_BACKEND})")
    else()
        message(STATUS "kin-galeri: Qt Multimedia NOT found - video playback disabled")
        set(KG_ENABLE_MULTIMEDIA OFF)
    endif()
endif()

qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
qt_add_executable(kin-galeri
    src/main.cpp
)

set(KG_SOURCES
    src/app/Paths.cpp
    src/app/Paths.h
    src/app/CliOptions.cpp
    src/app/CliOptions.h
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/AppSettings.cpp
    src/app/AppSettings.h
    src/app/ShortcutRegistry.cpp
    src/app/ShortcutRegistry.h
    src/app/PerfMonitor.cpp
    src/app/PerfMonitor.h

    src/core/db/Database.cpp
    src/core/db/Database.h
    src/core/db/Migrations.cpp
    src/core/db/Migrations.h
    src/core/db/LibraryMaintenance.cpp
    src/core/db/LibraryMaintenance.h

    src/core/model/MediaItem.h
    src/core/model/SourceAlbum.cpp
    src/core/model/SourceAlbum.h

    src/core/grouping/DateGrouper.cpp
    src/core/grouping/DateGrouper.h
    src/core/grouping/GeoClusterer.cpp
    src/core/grouping/GeoClusterer.h
    src/core/grouping/EventClusterer.cpp
    src/core/grouping/EventClusterer.h
    src/core/grouping/GroupingService.cpp
    src/core/grouping/GroupingService.h

    src/core/ml/FaceEngine.cpp
    src/core/ml/FaceEngine.h
    src/core/ml/FacePipeline.cpp
    src/core/ml/FacePipeline.h

    src/core/metadata/ExifReader.cpp
    src/core/metadata/ExifReader.h

    src/core/thumbs/ThumbnailService.cpp
    src/core/thumbs/ThumbnailService.h

    src/core/thumbs/ImageLoader.cpp
    src/core/thumbs/ImageLoader.h

    src/core/thumbs/VideoFrameExtractor.cpp
    src/core/thumbs/VideoFrameExtractor.h

    src/core/indexer/FolderIndexer.cpp
    src/core/indexer/FolderIndexer.h
    src/core/indexer/LibraryWatcher.cpp
    src/core/indexer/LibraryWatcher.h

    src/core/benchmark/BenchmarkSeeder.cpp
    src/core/benchmark/BenchmarkSeeder.h

    src/models/MediaGridModel.cpp
    src/models/MediaGridModel.h

    src/ui/quick/ThumbnailProvider.cpp
    src/ui/quick/ThumbnailProvider.h

    src/ui/quick/ThemeIconProvider.cpp
    src/ui/quick/ThemeIconProvider.h

    src/ui/quick/FullImageProvider.cpp
    src/ui/quick/FullImageProvider.h
    src/ui/quick/KinMpvItem.cpp
    src/ui/quick/KinMpvItem.h
)

target_sources(kin-galeri PRIVATE ${KG_SOURCES})

# Face backend, only when ONNX Runtime was found.
if(KG_ENABLE_ML)
    target_include_directories(kin-galeri PRIVATE ${KG_ORT_INCLUDE_DIR})
    target_link_libraries(kin-galeri PRIVATE ${KG_ORT_LIB})
    target_compile_definitions(kin-galeri PRIVATE KG_ENABLE_ML=1)
endif()

# Offline reverse-geocoding index for Places (GeoNames cities15000, CC BY 4.0),
# embedded so the feature works without a network call.
qt_add_resources(kin-galeri "kg_data"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/resources
    FILES resources/data/cities.tsv
)

# `pragma Singleton` in the file is not enough on its own: the build system has to
# be told *before* the file is added to the module, otherwise the generated qmldir
# registers Theme as a plain type and every `Theme.<colour>` lookup is undefined.
set_source_files_properties(src/ui/qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(KG_QML_FILES
    src/ui/qml/Main.qml
    src/ui/qml/Theme.qml
    src/ui/qml/AppIcon.qml
    src/ui/qml/Sidebar.qml
    src/ui/qml/MediaGrid.qml
    src/ui/qml/AlbumsView.qml
    src/ui/qml/AlbumCard.qml
    src/ui/qml/CollectionsView.qml
    src/ui/qml/SettingsView.qml
    src/ui/qml/MediaRow.qml
    src/ui/qml/MediaCell.qml
    src/ui/qml/TimelineHeader.qml
    src/ui/qml/PerfOverlay.qml
    src/ui/qml/ViewerOverlay.qml
)

# MpvVideoSurface.qml imports KinGaleri.Mpv, a module registered at runtime by
# qmlRegisterType with no generated qmldir. It is only compiled into the module
# when the mpv backend is built in; without mpvqt the Loader in ViewerOverlay
# stays inactive and the file is never loaded.
if(KG_ENABLE_MPV)
    list(APPEND KG_QML_FILES src/ui/qml/MpvVideoSurface.qml)
endif()

qt_add_qml_module(kin-galeri
    URI KinGaleri
    VERSION 1.0
    QML_FILES
        ${KG_QML_FILES}
)

# ---------------------------------------------------------------------------
# Translations (PRD 4.6). English is the source language; only Indonesian needs
# a catalog. The .qm is embedded at :/i18n/ and loaded by AppSettings; missing
# entries fall back to the English source automatically.
#
# Optional: a Qt install without LinguistTools still builds, just untranslated.
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    # NOTE: lupdate is a *manual* tool (`cmake --build build --target update_translations`).
    # It must be built with QML/JavaScript support, otherwise it silently ignores
    # every qsTr() in QML and - with the old `-no-obsolete` flag - deleted all 256
    # of them. `-no-obsolete` is deliberately gone: keeping obsolete entries is
    # recoverable, deleting them is not. The probe below warns about the broken
    # toolchain before anyone runs it.
    if(TARGET Qt6::lupdate)
        get_target_property(_kg_lupdate Qt6::lupdate IMPORTED_LOCATION)
    endif()
    if(_kg_lupdate AND EXISTS "${_kg_lupdate}")
        set(_kg_lupdate_probe_qml "${CMAKE_CURRENT_BINARY_DIR}/.kg_lupdate_probe.qml")
        file(WRITE "${_kg_lupdate_probe_qml}"
             "import QtQuick\nItem { property string s: qsTr(\"probe\") }\n")
        execute_process(
            COMMAND "${_kg_lupdate}" -verbose "${_kg_lupdate_probe_qml}"
                    -ts "${CMAKE_CURRENT_BINARY_DIR}/.kg_lupdate_probe.ts"
            OUTPUT_VARIABLE _kg_lupdate_probe
            ERROR_VARIABLE _kg_lupdate_probe
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(REMOVE "${_kg_lupdate_probe_qml}"
             "${CMAKE_CURRENT_BINARY_DIR}/.kg_lupdate_probe.ts")
        if(_kg_lupdate_probe MATCHES "missing qml/javascript support")
            message(WARNING
                "kin-galeri: this lupdate has no QML support. Do NOT run the "
                "update_translations target - it would strip every qsTr() string. "
                "Install a Qt LinguistTools built with QML support instead.")
        endif()
    endif()
    qt_add_translations(kin-galeri
        TS_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/kin-galeri_id.ts
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -locations relative
    )
else()
    message(STATUS "kin-galeri: Qt LinguistTools not found - Indonesian translation disabled")
endif()

target_include_directories(kin-galeri PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(kin-galeri PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::Concurrent
    ${KG_SQLITE_TARGET}
)

if(KG_ENABLE_MULTIMEDIA)
    target_link_libraries(kin-galeri PRIVATE Qt6::Multimedia)
    target_compile_definitions(kin-galeri PRIVATE KG_ENABLE_MULTIMEDIA=1)
endif()

if(KG_ENABLE_MPV)
    target_link_libraries(kin-galeri PRIVATE MpvQt::MpvQt)
    target_compile_definitions(kin-galeri PRIVATE KG_HAS_MPV=1)
endif()

if(KG_ENABLE_VIDEO_THUMBS)
    target_link_libraries(kin-galeri PRIVATE PkgConfig::KG_FFMPEG)
    target_compile_definitions(kin-galeri PRIVATE KG_HAS_VIDEO_THUMBS=1)
endif()

target_compile_definitions(kin-galeri PRIVATE
    KG_APP_ID="io.github.mrrizkin.KinGaleri"
    KG_APP_NAME="Kin Galeri"
    KG_APP_VERSION="${PROJECT_VERSION}"
    KG_VIDEO_BACKEND="${KG_VIDEO_BACKEND}"
)

# The FFmpeg media backend is a hard requirement for video playback (PRD 4.4).
if(KG_ENABLE_MULTIMEDIA)
    set(KG_MEDIA_BACKEND_DEFAULT "ffmpeg")
endif()

if(MSVC)
    target_compile_options(kin-galeri PRIVATE /W4)
else()
    target_compile_options(kin-galeri PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Install & packaging (M4, PRD 4.4 / section 9)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS kin-galeri RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.mrrizkin.KinGaleri.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.mrrizkin.KinGaleri.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/io.github.mrrizkin.KinGaleri.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

# Note: deliberately no qt_generate_deploy_qml_app_script() here. Bundling the Qt
# runtime is the packaging tool's job - linuxdeploy's qt plugin for the AppImage,
# and the runtime for Flatpak - which also avoids pinning the install to a single
# Qt/CMake deploy-tool combination. `cmake --install` therefore produces a normal
# distro-style prefix install.

# .deb via CPack. Building one needs dpkg-deb on the machine and is run with
# `cpack -G DEB`; the plain build never invokes CPack.
set(CPACK_PACKAGE_NAME "kin-galeri")
set(CPACK_PACKAGE_VENDOR "Kin Galeri")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast local photo & video library for Linux")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "io.github.mrrizkin")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_GENERATOR "DEB")
include(CPack)

# ---------------------------------------------------------------------------
# Tests & benchmarks
# ---------------------------------------------------------------------------
option(KG_BUILD_TESTS "Build unit tests" OFF)
if(KG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "kin-galeri ${PROJECT_VERSION}")
message(STATUS "  build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt             : ${Qt6_VERSION}")
message(STATUS "  video backend  : ${KG_VIDEO_BACKEND} (multimedia=${KG_ENABLE_MULTIMEDIA}, mpv=${KG_ENABLE_MPV})")
message(STATUS "  video thumbs   : ${KG_ENABLE_VIDEO_THUMBS} (ffmpeg)")
message(STATUS "  sqlite3        : ${SQLite3_VERSION}")
message(STATUS "")
