# Thin entry points over LauncherCore (Docs/Architecture.md). Both carry the product's name in lower case:
# <engine>.com (console, CLI) and <engine>.exe (window, added by the UI under LAUNCHER_BUILD_UI).

add_executable(LauncherCli Cli/Main.cpp)
target_link_libraries(LauncherCli PRIVATE LauncherCore)
# A console program named .com: in a terminal `<engine>` finds it before the windowed .exe (PATHEXT order)
set_target_properties(LauncherCli PROPERTIES OUTPUT_NAME "${LAUNCHER_EXECUTABLE_NAME}" SUFFIX ".com")
engine_warnings(LauncherCli)

# A development build reads the repository's engines and tools and keeps its settings in the build tree
# (Core/Settings/LauncherLayout.h); an installed launcher has no such file.
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:LauncherCli>/LauncherLayout.json" CONTENT
"{
  \"engines\": \"${PROJECT_SOURCE_DIR}/Engines\",
  \"tools\": \"${PROJECT_SOURCE_DIR}/ToolCache\",
  \"data\": \"$<TARGET_FILE_DIR:LauncherCli>/LauncherData\"
}
")

if(LAUNCHER_BUILD_UI)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Ui/CMakeLists.txt")
        message(FATAL_ERROR "LAUNCHER_BUILD_UI: Apps/Ui/ does not exist yet (Docs/CoreApi.md → UI)")
    endif()
    add_subdirectory(Ui)
endif()
