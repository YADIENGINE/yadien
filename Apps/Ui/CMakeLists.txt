# <engine>.exe: the windowed launcher (LAUNCHER_BUILD_UI). Thin over LauncherCore; the look is the engine's tool UI
# (EngineToolUi). Layout: Model/ (the one app state), Screens/ (one draw function per region), Dialogs/ (Windows).
add_library(LauncherUiPresentation STATIC
    Model/LauncherModel.cpp
    Dialogs/FolderDialog.cpp
    Screens/ScreenLayout.cpp
    Screens/LauncherFrame.cpp
    Screens/ProjectsTab.cpp
    Screens/EnginesTab.cpp
    Screens/NewProjectPanel.cpp
    Screens/SettingsPanel.cpp
    Screens/SystemCheckPanel.cpp
    Screens/EngineUpdatePanel.cpp
    Screens/JobPanel.cpp
    Screens/Confirmation.cpp)
target_include_directories(LauncherUiPresentation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# MSVC searches a library only where it first appears: raylib must follow Engine (its PlaySound, not winmm's) and come
# before user32 / winmm (raylib's CloseWindow, ShowCursor). The engine editor links the same way.
target_link_libraries(LauncherUiPresentation PUBLIC LauncherCore EngineToolUi Engine raylib ole32 shell32)
engine_warnings(LauncherUiPresentation)
# Velopack (installer, design D15): its prebuilt C library from ToolCache (Scripts/fetch_toolchain.py, Toolchain.lock.json
# "libraries"). The DLL keeps its own runtime, so it does not clash with the static MSVC runtime; only <engine>.exe needs
# it (Velopack runs the main executable for its install / uninstall hooks), and it is copied beside it.
set(LAUNCHER_VELOPACK_DIR "${PROJECT_SOURCE_DIR}/ToolCache/velopack" CACHE PATH "Velopack C library (velopack_libc)")
if(NOT EXISTS "${LAUNCHER_VELOPACK_DIR}/include/Velopack.h")
    message(FATAL_ERROR "No Velopack library at ${LAUNCHER_VELOPACK_DIR}; run: python Scripts/fetch_toolchain.py")
endif()
add_library(Velopack SHARED IMPORTED)
set_target_properties(Velopack PROPERTIES
    IMPORTED_LOCATION "${LAUNCHER_VELOPACK_DIR}/lib/velopack_libc_win_x64_msvc.dll"
    IMPORTED_IMPLIB "${LAUNCHER_VELOPACK_DIR}/lib/velopack_libc_win_x64_msvc.dll.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LAUNCHER_VELOPACK_DIR}/include")

add_executable(LauncherUi WIN32 Main.cpp LauncherApplication.cpp InstallerHooks.cpp)
target_link_libraries(LauncherUi PRIVATE LauncherUiPresentation Velopack)
set_target_properties(LauncherUi PROPERTIES OUTPUT_NAME "${LAUNCHER_EXECUTABLE_NAME}")
add_custom_command(TARGET LauncherUi POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Velopack>" "$<TARGET_FILE_DIR:LauncherUi>")
engine_warnings(LauncherUi)
