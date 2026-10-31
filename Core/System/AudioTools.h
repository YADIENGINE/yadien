#pragma once

#include "Platform/Process.h"
#include "Platform/Result.h"
#include "System/Toolchain.h"
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// The audio tools (design D7): the NumPy / SciPy wheels the engine's offline sound generator
// (Engine/Tools/AudioGeneration) needs. Not in the package: downloaded only when the user asks, with the versions and
// SHA-256 the launcher was built with (Toolchain.json "audio"), and kept beside the settings, outside the install's
// current/, so a launcher update does not remove them:
//     <folder>/site/              the extracted wheels (a sys.path entry)
//     <folder>/AudioTools.json    the installed versions
// `<engine> launcher python SCRIPT` runs a script with the bundled Python and <folder>/site on sys.path.

// NotOffered: this launcher lists no audio packages. Outdated: installed, but other versions than this launcher's.
enum class AudioToolsState { NotOffered, NotInstalled, Outdated, Installed };

[[nodiscard]] AudioToolsState InspectAudioTools(const std::filesystem::path& folder,
                                                const std::vector<AudioPackage>& packages);

// Downloads every package, checks its SHA-256, extracts it, then replaces `folder` as a whole: a failed or canceled
// install leaves the previous one as it was. Needs the internet; extraction uses Windows' own tar.exe.
[[nodiscard]] Status InstallAudioTools(const std::filesystem::path& folder, const std::vector<AudioPackage>& packages,
                                       const OutputLine& onLine, const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Status RemoveAudioTools(const std::filesystem::path& folder);

// The bundled Python running `arguments` (SCRIPT, then its arguments) with the script's folder and <folder>/site first
// on sys.path, as `python SCRIPT ...` would; the bundled Python is isolated (its ._pth), so PYTHONPATH cannot do this.
[[nodiscard]] ProcessCommand PythonScriptCommand(const Toolchain& toolchain, const std::filesystem::path& folder,
                                                 const std::vector<std::string>& arguments);
