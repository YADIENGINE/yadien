cmake_minimum_required(VERSION 3.24)
project(Launcher LANGUAGES C CXX)

# Launcher: headless core (LauncherCore) + thin entry points. Layout and rules: Docs/Architecture.md.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Single-file executables: the MSVC runtime is linked statically in every configuration.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LAUNCHER_BUILD_TESTS "Build the core's tests" ON)
option(LAUNCHER_BUILD_UI "Build the windowed launcher (needs the engine's raylib runtime and ToolUi)" ON)

# The launcher builds against an engine checkout (active development), never against the stable Engines/ copies:
# its own Engine/ submodule, or the enclosing project's Engine/ when the launcher sits inside a project
# (Scripts/dev.py passes it; Docs/Development.md).
set(LAUNCHER_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine" CACHE PATH "Engine checkout the launcher builds against")
if(NOT EXISTS "${LAUNCHER_ENGINE_DIR}/EngineManifest.json")
    message(FATAL_ERROR "No engine at ${LAUNCHER_ENGINE_DIR}; run: git submodule update --init Engine")
endif()

set(ENGINE_BUILD_RUNTIME ${LAUNCHER_BUILD_UI} CACHE BOOL "" FORCE)
set(ENGINE_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
set(ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENGINE_BUILD_EDITOR_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LAUNCHER_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/Engine" EXCLUDE_FROM_ALL)

# The product's name comes from the engine manifest; executables are named after it in lower case.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LAUNCHER_ENGINE_DIR}/EngineManifest.json")
file(READ "${LAUNCHER_ENGINE_DIR}/EngineManifest.json" launcherManifest)
string(JSON LAUNCHER_PRODUCT_NAME GET "${launcherManifest}" name)
string(TOLOWER "${LAUNCHER_PRODUCT_NAME}" LAUNCHER_EXECUTABLE_NAME)
set(LAUNCHER_VERSION "0.1.0")

# build/<Config>/: <engine>.com, the UI's <engine>.exe and the tests side by side
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

add_subdirectory(Core)
add_subdirectory(Apps)
if(LAUNCHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
