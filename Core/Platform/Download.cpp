#include "Platform/Download.h"
#include <windows.h>
#include <urlmon.h>
#include "Platform/Text.h"

namespace {
// OnProgress reports and, by returning E_ABORT, cancels a URL moniker download; GetBindInfo turns the cache off.
class TransferCallback final : public IBindStatusCallback {
public:
    TransferCallback(const std::atomic<bool>* cancel, const DownloadProgress& progress)
        : _cancel(cancel)
        , _progress(progress) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (id == IID_IUnknown || id == IID_IBindStatusCallback) {
            *object = static_cast<IBindStatusCallback*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    // Lives on the caller's stack for the whole (synchronous) download: no reference counting needed.
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG done, ULONG total, ULONG status, LPCWSTR) override {
        if (_progress && (status == BINDSTATUS_DOWNLOADINGDATA || status == BINDSTATUS_ENDDOWNLOADDATA))
            _progress(done, total);
        return _cancel != nullptr && _cancel->load() ? E_ABORT : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* flags, BINDINFO*) override {
        *flags |= BINDF_GETNEWESTVERSION | BINDF_NOWRITECACHE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }

private:
    const std::atomic<bool>* _cancel;
    const DownloadProgress& _progress;
};
} // namespace

Status DownloadFile(const std::string& url, const std::filesystem::path& file, const std::atomic<bool>* cancel,
                    const DownloadProgress& progress) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path partial = file;
    partial += L".part";
    std::filesystem::remove(partial, error);
    TransferCallback callback(cancel, progress);
    const HRESULT result = URLDownloadToFileW(nullptr, Utf8ToWide(url).c_str(), partial.c_str(), 0, &callback);
    if (cancel != nullptr && cancel->load()) {
        std::filesystem::remove(partial, error);
        return Status::Failure("Download of " + url + " was canceled");
    }
    if (FAILED(result)) {
        std::filesystem::remove(partial, error);
        return Status::Failure("Cannot download " + url + " (check the internet connection)");
    }
    std::filesystem::remove(file, error);
    std::filesystem::rename(partial, file, error);
    if (error) return Status::Failure("Cannot write " + PathToUtf8(file) + ": " + error.message());
    return Status::Success();
}
