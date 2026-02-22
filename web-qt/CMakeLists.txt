cmake_minimum_required(VERSION 3.21)
project(firebird-web-qt LANGUAGES C CXX ASM)

set(FIREBIRD_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

set(FIREBIRD_WEB_QT ON CACHE BOOL "Build Firebird via the Qt WebAssembly toolchain" FORCE)
set(FIREBIRD_ENABLE_KDDOCKWIDGETS ON CACHE BOOL "Enable KDDockWidgets docking backend" FORCE)
if(NOT DEFINED FIREBIRD_KDDOCKWIDGETS_PREFIX)
    set(
        FIREBIRD_KDDOCKWIDGETS_PREFIX
        "${FIREBIRD_SOURCE_DIR}/.build/deps/kddockwidgets-2.4-wasm"
        CACHE PATH
        "KDDockWidgets wasm prefix used by web-qt build"
        FORCE
    )
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_BINARY_DIR}/firebird")
