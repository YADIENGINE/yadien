#include "Support/TempFolder.h"
#include <atomic>
#include <fstream>
#include <sstream>
#include <windows.h>
#include "Platform/Text.h"

TempFolder::TempFolder() {
    static std::atomic<int> counter = 0;
    _path = std::filesystem::temp_directory_path() /
            ("LauncherTest-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(_path);
    std::filesystem::create_directories(_path);
}

TempFolder::~TempFolder() {
    std::error_code error;
    std::filesystem::remove_all(_path, error);
}

void TempFolder::Write(const std::string& relative, const std::string& content) const {
    const std::filesystem::path file = _path / Utf8ToPath(relative);
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << content;
}

std::string TempFolder::Read(const std::string& relative) const {
    std::ifstream stream(_path / Utf8ToPath(relative), std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}
