#pragma once
#include <filesystem>

// A recent frame of the project, as wide as `width`, kept to one texture and reloaded when the file changes. Draws
// nothing when the file cannot be loaded. The one screen piece that reads a file: a texture belongs to the window, so
// it cannot go through the model.
void DrawProjectPreview(const std::filesystem::path& file, float width);
