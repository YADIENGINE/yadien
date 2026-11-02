#include "Screens/ProjectPreview.h"
#include <imgui.h>
#include <raylib.h>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace {
struct LoadedPreview {
    std::filesystem::path file;
    std::filesystem::file_time_type written;
    Texture2D texture{};
};
// Lives until the process ends: the window closes first, and the texture goes with it.
std::optional<LoadedPreview> Loaded;

void Load(const std::filesystem::path& file, std::filesystem::file_time_type written) {
    if (Loaded && Loaded->texture.id != 0) UnloadTexture(Loaded->texture);
    Loaded = LoadedPreview{file, written, {}};
    // By bytes: raylib opens files with the ANSI code page, which a project's path may not fit.
    std::ifstream stream(file, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
    if (bytes.empty()) return;
    const Image image = LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size()));
    if (image.data == nullptr) return;
    Loaded->texture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(Loaded->texture, TEXTURE_FILTER_BILINEAR);
}
} // namespace

void DrawProjectPreview(const std::filesystem::path& file, float width) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(file, error);
    if (error) return;
    if (!Loaded || Loaded->file != file || Loaded->written != written) Load(file, written);
    const Texture2D& texture = Loaded->texture;
    if (texture.id == 0 || texture.width <= 0) return;
    const float height = width * static_cast<float>(texture.height) / static_cast<float>(texture.width);
    ImGui::Image(ImTextureID(texture.id), {width, height});
}
