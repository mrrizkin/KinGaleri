# Unit tests (PRD: none in M0-M2; introduced here to pin the M2 file/album/undo
# behaviour). Built only when -DKG_BUILD_TESTS=ON.
#
# The production target compiles its sources straight into the executable, so the
# testable subset is recompiled into a static library. That keeps the app target
# untouched and avoids having to pick apart the QML module registration.

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_library(kin-galeri-testcore STATIC
    ${CMAKE_SOURCE_DIR}/src/app/Paths.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Paths.h
    ${CMAKE_SOURCE_DIR}/src/app/PathUtils.h
    ${CMAKE_SOURCE_DIR}/src/app/CliOptions.cpp
    ${CMAKE_SOURCE_DIR}/src/app/CliOptions.h
    ${CMAKE_SOURCE_DIR}/src/app/AppSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AppSettings.h
    ${CMAKE_SOURCE_DIR}/src/app/ShortcutRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ShortcutRegistry.h

    ${CMAKE_SOURCE_DIR}/src/core/db/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/Database.h
    ${CMAKE_SOURCE_DIR}/src/core/db/Migrations.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/Migrations.h
    ${CMAKE_SOURCE_DIR}/src/core/db/LibraryMaintenance.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/LibraryMaintenance.h

    ${CMAKE_SOURCE_DIR}/src/core/model/SourceAlbum.cpp
    ${CMAKE_SOURCE_DIR}/src/core/model/SourceAlbum.h
    ${CMAKE_SOURCE_DIR}/src/core/grouping/DateGrouper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/grouping/DateGrouper.h
    ${CMAKE_SOURCE_DIR}/src/core/grouping/GeoClusterer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/grouping/GeoClusterer.h
    ${CMAKE_SOURCE_DIR}/src/core/grouping/EventClusterer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/grouping/EventClusterer.h
    ${CMAKE_SOURCE_DIR}/src/core/grouping/GroupingService.cpp
    ${CMAKE_SOURCE_DIR}/src/core/grouping/GroupingService.h
    ${CMAKE_SOURCE_DIR}/src/core/metadata/ExifReader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/metadata/ExifReader.h

    ${CMAKE_SOURCE_DIR}/src/core/thumbs/ImageLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/thumbs/ImageLoader.h

    ${CMAKE_SOURCE_DIR}/src/core/indexer/FolderIndexer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/indexer/FolderIndexer.h
    ${CMAKE_SOURCE_DIR}/src/core/indexer/LibraryWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/core/indexer/LibraryWatcher.h

    ${CMAKE_SOURCE_DIR}/src/models/MediaGridModel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/MediaGridModel.h
)

target_include_directories(kin-galeri-testcore PUBLIC ${CMAKE_SOURCE_DIR}/src)

# The tests exercise the same embedded city index as the app.
qt_add_resources(kin-galeri-testcore "kg_data_test"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/resources
    FILES ${CMAKE_SOURCE_DIR}/resources/data/cities.tsv
)

target_link_libraries(kin-galeri-testcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Concurrent
    ${KG_SQLITE_TARGET}
)

# Qt Test needs moc; the offscreen platform keeps everything headless.
function(kg_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kin-galeri-testcore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg"
    )
endfunction()

kg_add_test(tst_clioptions)
kg_add_test(tst_appsettings)
kg_add_test(tst_shortcuts)
kg_add_test(tst_pathutils)
kg_add_test(tst_librarywatcher)
kg_add_test(tst_mediagridmodel)
kg_add_test(tst_librarymaintenance)
kg_add_test(tst_folderindexer)
kg_add_test(tst_grouping)
kg_add_test(tst_imageloader)
