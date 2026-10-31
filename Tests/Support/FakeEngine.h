#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// A small engine copy as Scripts/import_engine.py would write it: EngineManifest.json (with a note per version in
// `history`, newest first, the copy's own version always included), the given files and EngineCopy.json hashing all
// of them. `breakingIn` versions get one breaking change in their note.
struct FakeEngine {
    std::string version = "0.1.0";
    std::map<std::string, std::string> files = {{"Tools/cli.py", "print('engine')\n"}};
    std::vector<std::string> history;
    std::vector<std::string> breakingIn;
    int toolsContract = 1;
};

void WriteFakeEngine(const std::filesystem::path& folder, const FakeEngine& engine);
// Project.json + Engine/ copy, as a user-mode project looks.
void WriteFakeProject(const std::filesystem::path& root, const std::string& name, const FakeEngine& engine);
