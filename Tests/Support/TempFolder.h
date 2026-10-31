#pragma once

#include <filesystem>
#include <string>

// A fresh folder under %TEMP%, deleted with everything in it when the test ends.
class TempFolder {
public:
    TempFolder();
    ~TempFolder();

    [[nodiscard]] const std::filesystem::path& Path() const { return _path; }
    // Writes a text file (folders created), LF kept as is.
    void Write(const std::string& relative, const std::string& content) const;
    [[nodiscard]] std::string Read(const std::string& relative) const;

private:
    std::filesystem::path _path;

public:
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};
