#include "Dialogs/FolderDialog.h"
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <memory>
#include "Platform/Text.h"

namespace {
// Releases a COM interface pointer.
struct ComRelease {
    void operator()(IUnknown* object) const {
        if (object) object->Release();
    }
};
template <typename T> using ComPointer = std::unique_ptr<T, ComRelease>;

// COM for this thread while the dialog is open; nested initializations are balanced.
class ComScope {
public:
    ComScope()
        : _initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {}
    ~ComScope() {
        if (_initialized) CoUninitialize();
    }

private:
    bool _initialized;

public:
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};
} // namespace

std::optional<std::filesystem::path> PickFolder(const std::string& title, const std::filesystem::path& initial) {
    const ComScope com;
    IFileOpenDialog* created = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created))))
        return std::nullopt;
    const ComPointer<IFileOpenDialog> dialog(created);
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(Utf8ToWide(title).c_str());
    std::error_code error;
    if (!initial.empty() && std::filesystem::is_directory(initial, error)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            const ComPointer<IShellItem> owned(folder);
            dialog->SetFolder(owned.get());
        }
    }
    if (FAILED(dialog->Show(GetActiveWindow()))) return std::nullopt; // Includes the user's cancel
    IShellItem* picked = nullptr;
    if (FAILED(dialog->GetResult(&picked))) return std::nullopt;
    const ComPointer<IShellItem> result(picked);
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    std::filesystem::path folder(path);
    CoTaskMemFree(path);
    return folder;
}
