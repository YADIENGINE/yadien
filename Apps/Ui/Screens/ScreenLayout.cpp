#include "Screens/ScreenLayout.h"
#include <extras/IconsFontAwesome6.h>
#include <algorithm>
#include <cmath>
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float ActivityRadius = 6.0f;
constexpr float ActivityThickness = 2.0f;
constexpr float ActivitySpeed = 4.5f;
constexpr float ActivityArc = 4.6f;
constexpr float FullCircle = 6.2831853f;
constexpr int ActivitySegments = 20;
constexpr float PathSuffixGap = 4.0f;
constexpr float MinContentWidth = 1.0f;

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

void LauncherPathText(const std::filesystem::path& path, float maxWidth) {
    const auto utf8 = path.u8string();
    const std::string full(utf8.begin(), utf8.end());
    std::string display = full;
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), ToolMetrics::CaptionSize);
    const float available = ImGui::GetContentRegionAvail().x;
    const float width =
        std::max(MinContentWidth * ToolUiScale(), maxWidth > 0 ? std::min(maxWidth, available) : available);
    if (ImGui::CalcTextSize(display.c_str()).x > width) {
        const float suffixWidth = ImGui::CalcTextSize("...").x + PathSuffixGap * ToolUiScale();
        while (!display.empty() && ImGui::CalcTextSize(display.c_str()).x + suffixWidth > width) {
            auto end = display.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(display[end]) & 0xc0) == 0x80)
                --end;
            display.resize(end);
        }
        display += "...";
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    const ImVec4 clip{start.x, start.y, start.x + width, start.y + height};
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), start,
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), display.c_str(), nullptr, 0, &clip);
    ImGui::Dummy({width, height});
    ToolTooltip(full.c_str());
    ImGui::PopFont();
}

void DrawLauncherActivity(const char* label) {
    const float radius = ActivityRadius * ToolUiScale();
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 center{start.x + radius, start.y + height * 0.5f};
    const float angle = static_cast<float>(std::fmod(ImGui::GetTime() * ActivitySpeed, FullCircle));
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), ActivitySegments,
                    ActivityThickness * ToolUiScale());
    draw->PathArcTo(center, radius, angle, angle + ActivityArc, ActivitySegments);
    draw->PathStroke(ImGui::GetColorU32(ToolToneColor(ToolTone::Accent)), 0, ActivityThickness * ToolUiScale());
    ImGui::Dummy({radius * 2, height});
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(label);
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
        ImGui::SeparatorText(note.version.ToString().c_str());
        if (!note.date.empty()) LauncherMutedText(note.date.c_str());
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
        ImGui::PushTextWrapPos(0);
        for (const auto& change : note.changes)
            ImGui::BulletText("%s", change.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
}
