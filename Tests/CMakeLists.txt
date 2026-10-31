# LauncherCore tests (Catch2 from the engine's ThirdParty). Run: python Scripts/dev.py test
engine_use_catch2()

add_executable(LauncherCoreTests
    Support/TempFolder.cpp
    Support/FakeEngine.cpp
    CommandLineTests.cpp
    CopyAndUpdateTests.cpp
    CryptoTests.cpp
    DeveloperGitTests.cpp
    EnginesTests.cpp
    JobTests.cpp
    NewProjectTests.cpp
    ProcessTests.cpp
    ProjectListTests.cpp
    SettingsTests.cpp
)
target_include_directories(LauncherCoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(LauncherCoreTests PRIVATE LauncherCore Catch2::Catch2WithMain)
engine_warnings(LauncherCoreTests)
catch_discover_tests(LauncherCoreTests DISCOVERY_MODE PRE_TEST)
