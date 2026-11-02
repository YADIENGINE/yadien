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

// The API's answer, newest version first; releases that are not a version tag or have no record are left out.
[[nodiscard]] Result<std::vector<OnlineEngine>> ParseEngineCatalog(const std::string& json, bool includePrereleases);

// `apiUrl`: https, or file for tests. `scratchFolder` is removed afterwards.
[[nodiscard]] Result<std::vector<OnlineEngine>> FetchEngineCatalog(const std::string& apiUrl, bool includePrereleases,
                                                                   const std::filesystem::path& scratchFolder,
                                                                   const std::atomic<bool>* cancel);

// Downloads the version, checks every file against its record and adds <cacheFolder>/Engine<version>/. Nothing is left
// behind on failure; a version already there is returned as it is.
[[nodiscard]] Result<AvailableEngine>
DownloadEngine(const OnlineEngine& engine, const std::filesystem::path& cacheFolder, const std::atomic<bool>* cancel);
