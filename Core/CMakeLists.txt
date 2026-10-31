# LauncherCore: every launcher job without a window (Docs/CoreApi.md). Entry points and tests link it.
add_library(LauncherCore STATIC
    Cli/CommandLine.cpp
    Cli/EngineCommand.cpp
    Developer/DeveloperGit.cpp
    Engines/EngineCopyRecord.cpp
    Engines/EngineLibrary.cpp
    Engines/EngineManifest.cpp
    Engines/EngineVersion.cpp
    Jobs/BackgroundJob.cpp
    Platform/Crypto.cpp
    Platform/FileSystem.cpp
    Platform/KnownFolders.cpp
    Platform/Process.cpp
    Platform/Shell.cpp
    Platform/Text.cpp
    Projects/CopyIntegrity.cpp
    Projects/EngineUpdate.cpp
    Projects/NewProject.cpp
    Projects/ProjectDetails.cpp
    Projects/ProjectInfo.cpp
    Projects/ProjectLaunch.cpp
    Projects/ProjectRow.cpp
    Projects/ProjectScan.cpp
    Settings/LauncherLayout.cpp
    Settings/LauncherSettings.cpp
    Settings/ProjectList.cpp
    System/SystemCheck.cpp
    System/Toolchain.cpp
    System/UserPath.cpp
)
target_include_directories(LauncherCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(LauncherCore PUBLIC nlohmann_json::nlohmann_json PRIVATE bcrypt shell32 ole32 user32 advapi32)
target_compile_definitions(LauncherCore PUBLIC
    LAUNCHER_PRODUCT_NAME="${LAUNCHER_PRODUCT_NAME}"
    LAUNCHER_EXECUTABLE_NAME="${LAUNCHER_EXECUTABLE_NAME}"
    LAUNCHER_VERSION="${LAUNCHER_VERSION}"
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
engine_warnings(LauncherCore)
