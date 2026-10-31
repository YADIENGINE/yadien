# <engine>.exe: the windowed launcher (LAUNCHER_BUILD_UI). Thin over LauncherCore; the look is the engine's tool UI
# (EngineToolUi). Layout: Model/ (the one app state), Screens/ (one draw function per region), Dialogs/ (Windows).
add_executable(LauncherUi WIN32
    Main.cpp
    LauncherApplication.cpp
    Model/LauncherModel.cpp
    Dialogs/FolderDialog.cpp
    Screens/LauncherFrame.cpp
    Screens/ProjectsTab.cpp
    Screens/EnginesTab.cpp
    Screens/NewProjectPanel.cpp
    Screens/SettingsPanel.cpp
    Screens/SystemCheckPanel.cpp
    Screens/EngineUpdatePanel.cpp
    Screens/JobPanel.cpp
    Screens/Confirmation.cpp)
target_include_directories(LauncherUi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# MSVC searches a library only where it first appears: raylib must follow Engine (its PlaySound, not winmm's) and come
# before user32 / winmm (raylib's CloseWindow, ShowCursor). The engine editor links the same way.
target_link_libraries(LauncherUi PRIVATE LauncherCore EngineToolUi Engine raylib ole32 shell32)
set_target_properties(LauncherUi PROPERTIES OUTPUT_NAME "${LAUNCHER_EXECUTABLE_NAME}")
engine_warnings(LauncherUi)
