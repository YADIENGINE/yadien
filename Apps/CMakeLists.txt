# Thin entry points over LauncherCore (Docs/Architecture.md). Both carry the product's name in lower case:
# <engine>.com (console, CLI) and <engine>.exe (window, Apps/Ui, LAUNCHER_BUILD_UI).

# Icon and installer splash, drawn from code at configure time (Scripts/brand.py; never committed). The resource names
# the icon GLFW_ICON: Windows shows it for the executables, and GLFW (raylib) uses it for the window and taskbar.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(LAUNCHER_BRAND_DIR "${CMAKE_BINARY_DIR}/Brand")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Scripts/brand.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/Scripts/brand.py" "${LAUNCHER_BRAND_DIR}"
            --name "${LAUNCHER_PRODUCT_NAME}"
    RESULT_VARIABLE brandResult)
if(NOT brandResult EQUAL 0)
    message(FATAL_ERROR "Scripts/brand.py failed (${brandResult})")
endif()
file(WRITE "${LAUNCHER_BRAND_DIR}/Launcher.rc" "GLFW_ICON ICON \"${LAUNCHER_BRAND_DIR}/Icon.ico\"\n")
set(LAUNCHER_RESOURCES "${LAUNCHER_BRAND_DIR}/Launcher.rc")

add_executable(LauncherCli Cli/Main.cpp "${LAUNCHER_RESOURCES}")
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
    add_subdirectory(Ui)
endif()
