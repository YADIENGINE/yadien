#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include "Platform/Text.h"
#include "Settings/LauncherSettings.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"
#include "UiSmoke.h"

namespace {
bool CheckWorkspaceModel(const LauncherLayout& layout, const std::filesystem::path& project) {
    LauncherSettings settings;
    settings.projectsFolder = project.parent_path();
    if (!SaveLauncherSettings(layout.SettingsFile(), settings).Ok()) return false;
    LauncherModel model(layout);
    for (const std::string directory :
         {std::string{}, std::string{"relative-folder"}, PathToUtf8(project / "Missing")}) {
        model.SetProjectsFolder(Utf8ToPath(directory));
        model.Tick();
        if (model.CurrentJob() || !model.CurrentNotice() || model.CurrentNotice()->level != NoticeLevel::Error ||
            model.ProjectsFolder() != settings.projectsFolder)
            return false;
    }
    model.SetProjectsFolder(project);
    model.Tick();
    if (model.ProjectsFolder() != project || model.NewProject().location != PathToUtf8(project)) return false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (model.CurrentJob() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        model.Tick();
    }
    if (model.CurrentJob() || model.Rows().size() != 1 || model.Rows().front().entry.root != project) return false;
    model.Select(0);
    model.Tick();
    const auto saved = LoadLauncherSettings(layout.SettingsFile());
    return model.Selected() == 0 && saved.Ok() && saved.Value().projects.size() == 1 &&
           saved.Value().projectsFolder == project;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: LauncherUiSmoke <snapshot-folder>\n";
        return 2;
    }
    const auto output = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output);
    TempFolder temp;
    const LauncherLayout empty{temp.Path() / "Engines", temp.Path() / "Tools", temp.Path() / "EmptyData"};
    const LauncherLayout populated{empty.enginesFolder, empty.toolsFolder, temp.Path() / "ProjectData"};
    WriteFakeEngine(empty.enginesFolder / "Engine0.1", FakeEngine{});
    LauncherSettings settings;
    if (!SaveLauncherSettings(empty.SettingsFile(), settings).Ok()) return 1;
    const auto first = temp.Path() / "Workspace";
    const auto second = temp.Path() / "Experiments" / "A longer project folder with spaces and UTF-8 - Çalışma";
    WriteFakeProject(first, "Workspace", FakeEngine{});
    WriteFakeProject(second, "A longer project title · Çalışma", FakeEngine{});
    settings.projects = {{first, ""}, {second, ""}};
    if (!SaveLauncherSettings(populated.SettingsFile(), settings).Ok()) return 1;
    if (!CheckWorkspaceModel({empty.enginesFolder, empty.toolsFolder, temp.Path() / "ScanData"}, first)) {
        std::cerr << "Workspace actions failed: folder validation, automatic discovery or default folder persistence\n";
        return 1;
    }
    std::cout << "Workspace actions: passed\n";
    UiSmoke smoke(empty, populated, output);
    smoke.Run();
    std::cout << "UI checks: " << smoke.Failures() << " failures\n";
    return smoke.Failures() == 0 ? 0 : 1;
}
