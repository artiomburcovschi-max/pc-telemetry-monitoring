cmake_minimum_required(VERSION 3.21)

project(ORION
    VERSION 0.1.0
    DESCRIPTION "O.R.I.O.N. native system monitoring"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORION_BUILD_GUI "Build the Qt Widgets application" ON)
option(ORION_BUILD_DIAGNOSTICS "Build the JSON diagnostic contract layer" ON)
option(ORION_BUILD_STORAGE "Build Qt settings, data paths and logging" ON)
option(ORION_BUILD_TESTS "Build native unit and smoke tests" ON)
option(ORION_BUILD_NETSCAN "Build the native network scanner" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(tools/probe)

if(ORION_BUILD_GUI OR ORION_BUILD_DIAGNOSTICS OR ORION_BUILD_STORAGE)
    set(_orion_qt_components Core)
    if(ORION_BUILD_STORAGE)
        list(APPEND _orion_qt_components Sql)
    endif()
    if(ORION_BUILD_GUI)
        list(APPEND _orion_qt_components Gui Network Widgets)
    endif()
    find_package(Qt6 6.5 QUIET COMPONENTS ${_orion_qt_components})
    if(Qt6_FOUND)
        if(ORION_BUILD_DIAGNOSTICS)
            add_subdirectory(diagnostics)
        endif()
        if(ORION_BUILD_STORAGE)
            add_subdirectory(storage)
        endif()
        if(ORION_BUILD_GUI)
            add_subdirectory(ui)
        endif()
    else()
        message(WARNING
            "Qt 6.5+ development files were not found. "
            "The native core, telemetry probe and tests will still be built; "
            "the Qt GUI, storage and JSON diagnostic targets are skipped."
        )
    endif()
endif()

if(ORION_BUILD_NETSCAN)
    add_subdirectory(tools/netscan)
endif()

if(ORION_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
