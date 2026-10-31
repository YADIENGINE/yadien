#pragma once

#include "Engines/EngineVersion.h"
#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <vector>

// Engine/EngineManifest.json, written by hand in the engine repository (engine Docs/ToolsContract.md → Manifest).
struct BreakingChange {
    std::string change;
    std::string migrate; // How a project adapts
};

struct EngineNote {
    EngineVersion version;
    std::string date; // YYYY-MM-DD
    std::vector<std::string> changes;
    std::vector<BreakingChange> breaking;
};

struct EngineManifest {
    std::string name;
    EngineVersion version;
    int toolsContract = 0;
    std::string repository;        // Developer mode: the submodule's source
    std::vector<EngineNote> notes; // Newest first
};

inline constexpr const char* EngineManifestFile = "EngineManifest.json";

// Reads <engineFolder>/EngineManifest.json. Notes are optional for reading (older manifests had none).
[[nodiscard]] Result<EngineManifest> LoadEngineManifest(const std::filesystem::path& engineFolder);

// Notes of the versions after `from` up to and including the manifest's own version, newest first: what an update
// from `from` brings, including skipped versions (cumulative notes).
[[nodiscard]] std::vector<EngineNote> NotesSince(const EngineManifest& manifest, const EngineVersion& from);
