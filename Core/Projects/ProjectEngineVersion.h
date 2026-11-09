#pragma once

#include "Engines/EngineVersion.h"
#include "Platform/Result.h"
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <optional>

// Project.json "engine": {"version": "0.11.1"} (engine Docs/ProjectSetup.md): the engine the project uses. Engine/
// stays out of git, so this is what a fresh clone has to go on; the launcher installs that version instead of
// treating the missing engine as a move from an unknown one.

// The recorded version; nothing when the field is absent or not a version.
[[nodiscard]] std::optional<EngineVersion> RecordedEngineVersion(const nlohmann::json& project);

// Writes the version into the project's Project.json and changes nothing else in the text: the file is the user's,
// kept in git, and hand-formatted. Nothing is written when it already names this version.
[[nodiscard]] Status RecordProjectEngine(const std::filesystem::path& projectRoot, const EngineVersion& version);
