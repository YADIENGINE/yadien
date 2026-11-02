#include "Screens/ScreenLayout.h"
#include <extras/IconsFontAwesome6.h>
#include <algorithm>
#include "Platform/Text.h"
#include "System/DisplayText.h"
#include "ToolUi/ToolPixel.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float MinContentWidth = 1.0f;
constexpr float FooterGap = 10.0f;
constexpr float BulletGap = 12.0f;

int ResizeInput(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto& value = *static_cast<std::string*>(data->UserData);
        value.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value.data();
    }
    return 0;
}
} // namespace

bool LauncherInputText(const char* label, std::string& value, ImGuiInputTextFlags flags, const char* hint) {
    if (hint)
        return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1,
                                        flags | ImGuiInputTextFlags_CallbackResize, ResizeInput, &value);
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                            ResizeInput, &value);
}

void LauncherMutedText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void LauncherToneText(const char* text, ToolTone tone) {
    ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(tone));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void LauncherFieldLabel(const char* text) {
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::CaptionSize);
    ImGui::TextColored(ToolPalette::Muted, "%s", text);
    ImGui::PopFont();
}

std::string LauncherShortPath(const std::filesystem::path& path, float width) {
    static const std::filesystem::path Home = UserHomeFolder();
    return ToolClipMiddle(ShortPathText(path, Home).c_str(), width);
}

void LauncherPathText(const std::filesystem::path& path, float maxWidth) {
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), ToolMetrics::CaptionSize);
    const float available = ImGui::GetContentRegionAvail().x;
    const float width =
        std::max(MinContentWidth * ToolUiScale(), maxWidth > 0 ? std::min(maxWidth, available) : available);
    const std::string display = LauncherShortPath(path, width);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), start,
                                        ImGui::GetColorU32(ToolPalette::Muted), display.c_str());
    ImGui::Dummy({width, height});
    ToolTooltip(PathToUtf8(path).c_str());
    ImGui::PopFont();
}

void DrawLauncherActivity(const char* label) {
    ToolActivity();
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
}

void DrawLauncherBullet(const char* text) {
    ImGui::Bullet(); // BulletText does not wrap
    ImGui::SameLine(0, BulletGap * ToolUiScale());
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void DrawLauncherNotes(const std::vector<EngineNote>& notes) {
    if (notes.empty()) {
        LauncherMutedText("No release notes provided.");
        return;
    }
    for (std::size_t index = 0; index < notes.size(); ++index) {
        const auto& note = notes[index];
        ImGui::PushID(static_cast<int>(index));
        if (index > 0) ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        ImGui::TextUnformatted(note.version.ToString().c_str());
        ImGui::PopFont();
        if (!note.date.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ToolPalette::Muted, "%s", note.date.c_str());
        }
        for (const auto& breaking : note.breaking) {
            ToolStatusBadge(ICON_FA_TRIANGLE_EXCLAMATION " Breaking change", ToolToneColor(ToolTone::Warning));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(breaking.change.c_str());
            ImGui::PopTextWrapPos();
            if (!breaking.migrate.empty()) {
                LauncherToneText("Migration", ToolTone::Information);
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(breaking.migrate.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        for (const auto& change : note.changes)
            DrawLauncherBullet(change.c_str());
        ImGui::PopID();
    }
}

float LauncherFooterHeight() { return ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale(); }

int DrawLauncherFooter(std::span<const LauncherFooterButton> buttons, bool afterItem) {
    const auto& style = ImGui::GetStyle();
    float width = 0;
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    for (const LauncherFooterButton& button : buttons)
        width += ImGui::CalcTextSize(button.label, nullptr, true).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
    ImGui::PopFont();
    width -= style.ItemSpacing.x;
    const float start = std::max(0.0f, ImGui::GetContentRegionMax().x - width);
    if (afterItem)
        ImGui::SameLine(start);
    else
        ImGui::SetCursorPosX(start);
    int pressed = -1;
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        const auto& button = buttons[index];
        if (index > 0) ImGui::SameLine();
        ImGui::BeginDisabled(!button.enabled);
        const bool toned = button.tone != ToolTone::Accent;
        if (toned) ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(button.tone));
        if (ToolButton(button.label, {}, button.primary)) pressed = static_cast<int>(index);
        if (toned) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (button.primary && button.enabled) ImGui::SetItemDefaultFocus();
    }
    return pressed;
}
