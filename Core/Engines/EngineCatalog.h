#pragma once

#include "Engines/EngineLibrary.h"
#include "Platform/Result.h"
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The engine versions published on GitHub (design D16): every release of the engine repository that holds an
// EngineCopy.json. A version is a tag's source archive; the release's record says which files it must contain, so a
// tag that moved or an archive that changed is refused.

// "https://github.com/owner/engine.git" → the URL listing its releases; empty when it is not a github.com repository.
[[nodiscard]] std::string EngineCatalogUrl(std::string_view repository);

// EngineCatalogUrl, or the LAUNCHER_ENGINE_API environment variable when it is set (Scripts/engine_test.py's local
// list).
[[nodiscard]] std::string EngineCatalogSource(std::string_view repository);

// Engine releases before 1.0 are GitHub pre-releases: a 0.x engine lists them, from 1.0 only stable ones.
[[nodiscard]] bool IncludesPrereleases(const EngineVersion& newest);

// The API's answer, newest version first; releases that are not a version tag or have no record are left out.
[[nodiscard]] Result<std::vector<OnlineEngine>> ParseEngineCatalog(const std::string& json, bool includePrereleases);

// `apiUrl`: https, or file for tests. `scratchFolder` is removed afterwards.
[[nodiscard]] Result<std::vector<OnlineEngine>> FetchEngineCatalog(const std::string& apiUrl, bool includePrereleases,
                                                                   const std::filesystem::path& scratchFolder,
                                                                   const std::atomic<bool>* cancel);

// What the catalog job reads: the versions and the newest release's notes.
struct OnlineCatalog {
    std::vector<OnlineEngine> engines;
    std::vector<EngineNote> notes;
};

// The notes of every version, read from the newest release that carries ReleaseNotes.json (engine
// Docs/ToolsContract.md → Manifest); empty when none does. `scratchFolder` is removed afterwards.
[[nodiscard]] Result<std::vector<EngineNote>> FetchEngineNotes(const std::vector<OnlineEngine>& engines,
                                                               const std::filesystem::path& scratchFolder,
                                                               const std::atomic<bool>* cancel);

// Downloads the version, checks every file against its record and adds <cacheFolder>/Engine<version>/. Nothing is left
// behind on failure; a version already there is returned as it is.
[[nodiscard]] Result<AvailableEngine>
DownloadEngine(const OnlineEngine& engine, const std::filesystem::path& cacheFolder, const std::atomic<bool>* cancel);

// Downloads the version's compiled engine into <prebuiltFolder>/<version>/ (engine Docs/Prebuilt.md) and checks it
// against the release's digest. Succeeds doing nothing when the release has none or it is already there. A project
// without one builds the engine from source, so callers treat a failure as a warning.
[[nodiscard]] Status DownloadPrebuiltEngine(const OnlineEngine& engine, const std::filesystem::path& prebuiltFolder,
                                            const std::atomic<bool>* cancel);
