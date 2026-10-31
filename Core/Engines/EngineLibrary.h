#pragma once

#include "Engines/EngineCopyRecord.h"
#include "Engines/EngineManifest.h"
#include <filesystem>
#include <string>
#include <vector>

// A stable engine the launcher carries: Engines/Engine<MAJOR.MINOR>/.
struct AvailableEngine {
    std::filesystem::path folder;
    EngineManifest manifest;
    EngineCopyRecord record;
};

// The launcher's Engines/ folder. Folders without a valid manifest and copy record are listed in `Problems`
// (a broken install), not offered.
class EngineLibrary {
public:
    [[nodiscard]] static EngineLibrary Scan(const std::filesystem::path& enginesFolder);

    // Newest version first.
    [[nodiscard]] const std::vector<AvailableEngine>& Engines() const { return _engines; }
    [[nodiscard]] const std::vector<std::string>& Problems() const { return _problems; }
    [[nodiscard]] const AvailableEngine* Newest() const { return _engines.empty() ? nullptr : &_engines.front(); }
    // The folder of a series ("0.2"), or nullptr.
    [[nodiscard]] const AvailableEngine* FindSeries(const std::string& series) const;
    // The newest engine strictly newer than `version` (the update a copy project is offered), or nullptr.
    [[nodiscard]] const AvailableEngine* UpdateFor(const EngineVersion& version) const;

private:
    std::vector<AvailableEngine> _engines;
    std::vector<std::string> _problems;
};
