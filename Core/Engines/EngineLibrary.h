#pragma once

#include "Engines/EngineCopyRecord.h"
#include "Engines/EngineManifest.h"
#include "Platform/Result.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A stable engine on this computer: Engine<version>/ in the launcher's Engines/ or in its download cache.
struct AvailableEngine {
    std::filesystem::path folder;
    EngineManifest manifest;
    EngineCopyRecord record;
    bool downloaded = false; // In the download cache, so it can be removed; the bundled one stays
};

// A version published online (Engines/EngineCatalog.h).
struct OnlineEngine {
    EngineVersion version;
    std::string tag;            // "v0.2.0"
    std::string recordUrl;      // The release's EngineCopy.json
    std::string archiveUrl;     // The tag's source archive
    std::string prebuiltUrl;    // The release's compiled engine; empty when it has none
    std::string prebuiltSha256; // GitHub's digest of that file, lower-case hex
    std::string notesUrl;       // The release's ReleaseNotes.json; empty before 0.11.1
};

// "Engine0.2.0".
[[nodiscard]] std::string EngineFolderName(const EngineVersion& version);

// Deletes a downloaded engine from the cache; a project with its own copy keeps working. Refuses the bundled one.
[[nodiscard]] Status RemoveDownloadedEngine(const AvailableEngine& engine);

// The engines the launcher can give a project: the one it ships (works offline), the ones it downloaded and, once the
// catalog has been read, the ones it can download. Folders without a valid manifest and copy record are listed in
// `Problems` (a broken install), not offered.
class EngineLibrary {
public:
    // `cacheFolder` may be empty or not exist yet. A version in both is the bundled one.
    [[nodiscard]] static EngineLibrary Scan(const std::filesystem::path& bundledFolder,
                                            const std::filesystem::path& cacheFolder = {});

    // On this computer, newest version first.
    [[nodiscard]] const std::vector<AvailableEngine>& Engines() const { return _engines; }
    [[nodiscard]] const std::vector<std::string>& Problems() const { return _problems; }
    [[nodiscard]] const AvailableEngine* Newest() const { return _engines.empty() ? nullptr : &_engines.front(); }
    [[nodiscard]] const AvailableEngine* Find(const EngineVersion& version) const;

    // The catalog's answer; versions already on this computer are dropped from it.
    void SetOnline(std::vector<OnlineEngine> online);
    // The newest release's notes (FetchEngineNotes): what versions not on this computer bring.
    void SetOnlineNotes(std::vector<EngineNote> notes);
    [[nodiscard]] const std::vector<OnlineEngine>& Online() const { return _online; }
    [[nodiscard]] const std::vector<EngineNote>& OnlineNotes() const { return _onlineNotes; }
    [[nodiscard]] const OnlineEngine* FindOnline(const EngineVersion& version) const;
    // Every version the list has named, on this computer or not: a local one can still lack its compiled engine.
    [[nodiscard]] const OnlineEngine* FindPublished(const EngineVersion& version) const;

    // Every version a project can be given, newest first.
    [[nodiscard]] std::vector<EngineVersion> KnownVersions() const;
    // The version's own note: from an engine on this computer, else from the online notes; null when none says.
    [[nodiscard]] const EngineNote* NoteFor(const EngineVersion& version) const;
    // "0.11.1 Cats" (EngineNoteTitle), or the bare version while no note names it.
    [[nodiscard]] std::string Label(const EngineVersion& version) const;
    // The release notes to show for a version, newest first: its own folder's, else the online ones up to it.
    [[nodiscard]] std::vector<EngineNote> NotesOf(const EngineVersion& version) const;
    // The newest version online that is newer than every engine on this computer and has a summary to announce.
    [[nodiscard]] const EngineNote* NewVersionNote() const;

    // The newest known version strictly newer than `version` (the update a copy project is offered).
    [[nodiscard]] std::optional<EngineVersion> UpdateFor(const EngineVersion& version) const;

private:
    std::vector<AvailableEngine> _engines;
    std::vector<OnlineEngine> _online;
    std::vector<OnlineEngine> _published;
    std::vector<EngineNote> _onlineNotes; // Newest first
    std::vector<std::string> _problems;
};
