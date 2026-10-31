#include "Support/FakeEngine.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include "Platform/Crypto.h"
#include "Platform/Text.h"

namespace {
void WriteFile(const std::filesystem::path& file, const std::string& content) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << content;
}

std::string Hash(const std::string& content) {
    return "sha256:" + Sha256Hex({reinterpret_cast<const std::uint8_t*>(content.data()), content.size()});
}
} // namespace

void WriteFakeEngine(const std::filesystem::path& folder, const FakeEngine& engine) {
    std::vector<std::string> versions = engine.history;
    if (std::ranges::find(versions, engine.version) == versions.end())
        versions.insert(versions.begin(), engine.version);
    nlohmann::json notes = nlohmann::json::array();
    for (const std::string& version : versions) {
        nlohmann::json breaking = nlohmann::json::array();
        if (std::ranges::find(engine.breakingIn, version) != engine.breakingIn.end())
            breaking.push_back({{"change", "Renamed X in " + version}, {"migrate", "Use Y"}});
        notes.push_back({{"version", version},
                         {"date", "2026-01-01"},
                         {"changes", {"Change in " + version}},
                         {"breaking", breaking}});
    }
    const nlohmann::json manifest = {{"name", "TestEngine"},
                                     {"version", engine.version},
                                     {"toolsContract", engine.toolsContract},
                                     {"repository", "https://example.invalid/engine.git"},
                                     {"notes", notes}};
    std::map<std::string, std::string> files = engine.files;
    files["EngineManifest.json"] = manifest.dump(2) + "\n";

    nlohmann::json hashes = nlohmann::json::object();
    for (const auto& [path, content] : files) {
        WriteFile(folder / Utf8ToPath(path), content);
        hashes[path] = Hash(content);
    }
    const nlohmann::json record = {
        {"format", 1}, {"source", {{"tag", "v" + engine.version}, {"commit", "0123456789abcdef"}}}, {"files", hashes}};
    WriteFile(folder / "EngineCopy.json", record.dump(2) + "\n");
}

void WriteFakeProject(const std::filesystem::path& root, const std::string& name, const FakeEngine& engine) {
    WriteFile(root / "Project.json", nlohmann::json{{"version", 1}, {"name", name}}.dump(2) + "\n");
    WriteFakeEngine(root / "Engine", engine);
}
