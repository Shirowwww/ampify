#include "xaml_watcher.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <ocidl.h>
#include <xamlOM.h>

#include <atomic>
#include <string>
#include <thread>

// XAML Diagnostics is the interface Visual Studio's Live Visual Tree uses: a "tap" object, created from a DLL by its
// CLSID, receives the tree's service and is told of each change. Here the DLL is the kernel itself, which exports
// DllGetClassObject for that one class. Design after Microsoft's WinUISnoop sample (MIT). The calls and their order
// matter: Advise from another thread (it waits for the interface thread, which calls SetSite), and each handle the
// service gives is released, or it keeps every element alive.

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")

namespace ampify::win {

namespace {

// {6C4B1E2A-7D35-4F7B-9A0E-3B1C5D8E2F41}, Ampify's tap.
constexpr CLSID kTapClsid = {0x6c4b1e2a, 0x7d35, 0x4f7b, {0x9a, 0x0e, 0x3b, 0x1c, 0x5d, 0x8e, 0x2f, 0x41}};

// The hooks that release a handle of the service: not in the SDK headers. Only the IID and the slot of its first
// method are known, as other diagnostics tools use them.
struct __declspec(uuid("735941a2-3ee3-495a-8da9-972627003075")) IXamlDiagnosticsTestHooks : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE UnregisterInstance(InstanceHandle handle) = 0;
};

using InitializeFn = HRESULT(WINAPI*)(LPCWSTR endPoint, DWORD pid, LPCWSTR diagnosticsDll, LPCWSTR tapDll,
                                      CLSID tapClsid, LPCWSTR initializationData);

constexpr int kLoggedAdds = 40;          // the first elements, one line each
constexpr long kSummaryEvery = 2000;     // then one line of totals every so many changes
constexpr int kEndpointAttempts = 32;    // a name already taken (another tool) gives ERROR_NOT_FOUND

Journal* g_journal = nullptr;
std::atomic<bool> g_started{false};

std::string Narrow(BSTR text) {
    if (!text) return {};
    int length = static_cast<int>(SysStringLen(text));
    int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
    return out;
}

class Tap final : public IObjectWithSite, public IVisualTreeServiceCallback2 {
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IObjectWithSite)) {
            *out = static_cast<IObjectWithSite*>(this);
        } else if (riid == __uuidof(IVisualTreeServiceCallback) || riid == __uuidof(IVisualTreeServiceCallback2)) {
            *out = static_cast<IVisualTreeServiceCallback2*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = --m_refs;
        if (left == 0) delete this;
        return left;
    }

    // IObjectWithSite: called on the interface thread once the service is ready.
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        try {
            return Connect(site);
        } catch (...) {
            return E_FAIL;  // no exception may cross into WinUI
        }
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID, void** site) override {
        if (site) *site = nullptr;
        return E_NOTIMPL;
    }

    HRESULT Connect(IUnknown* site) {
        if (!site) return S_OK;
        if (FAILED(site->QueryInterface(__uuidof(IVisualTreeService), reinterpret_cast<void**>(&m_tree)))) {
            g_journal->Write("XAML watcher: the site has no visual tree service, nothing more is tried");
            return S_OK;
        }
        site->QueryInterface(__uuidof(IXamlDiagnosticsTestHooks), reinterpret_cast<void**>(&m_hooks));
        g_journal->Write(std::string("XAML watcher: connected") +
                         (m_hooks ? "" : ", without the hooks that release handles (elements may be kept alive)"));
        AddRef();  // held by the advise below for the life of the process
        IVisualTreeService* tree = m_tree;
        std::thread([tree, self = this] {
            HRESULT result = tree->AdviseVisualTreeChange(self);
            g_journal->Write("XAML watcher: advise " + std::string(SUCCEEDED(result) ? "done" : "refused, hr ") +
                             (SUCCEEDED(result) ? "" : std::to_string(static_cast<long>(result))));
        }).detach();  // returns at once; joining it from SetSite would wait for this very thread
        return S_OK;
    }

    // IVisualTreeServiceCallback: always S_OK, an error here stops the notifications for good.
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
                                                 VisualMutationType mutation) override {
        try {
            long count = mutation == Add ? ++m_adds : ++m_removes;
            if (mutation == Add && count <= kLoggedAdds) {
                g_journal->Write("XAML watcher: + " + Narrow(element.Type) +
                                 (element.Name && SysStringLen(element.Name) ? " #" + Narrow(element.Name) : ""));
            }
            if ((m_adds + m_removes) % kSummaryEvery == 0) {
                g_journal->Write("XAML watcher: " + std::to_string(m_adds) + " added, " + std::to_string(m_removes) +
                                 " removed so far");
            }
        } catch (...) {
        }
        if (m_hooks) m_hooks->UnregisterInstance(element.Handle);
        return S_OK;  // element's strings belong to the caller
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override {
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refs{1};
    IVisualTreeService* m_tree = nullptr;
    IXamlDiagnosticsTestHooks* m_hooks = nullptr;
    long m_adds = 0;  // the callbacks come on the interface thread only
    long m_removes = 0;
};

class TapFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid != __uuidof(IUnknown) && riid != __uuidof(IClassFactory)) return E_NOINTERFACE;
        *out = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }  // a static object
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        Tap* tap = new (std::nothrow) Tap();
        if (!tap) return E_OUTOFMEMORY;
        HRESULT result = tap->QueryInterface(riid, out);
        tap->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

TapFactory g_factory;

}  // namespace

void StartXamlWatcher(Journal& journal, HINSTANCE kernel) {
    if (g_started.exchange(true)) return;
    g_journal = &journal;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(kernel, path, MAX_PATH)) return;

    // The Windows App SDK exports the initializer from its framework DLL; the older name is tried after it.
    InitializeFn initialize = nullptr;
    std::string from;
    for (const wchar_t* module : {L"Microsoft.Internal.FrameworkUdk.dll", L"Microsoft.UI.Xaml.dll"}) {
        if (HMODULE loaded = GetModuleHandleW(module)) {
            initialize = reinterpret_cast<InitializeFn>(GetProcAddress(loaded, "InitializeXamlDiagnosticsEx"));
            if (initialize) {
                from = module == std::wstring(L"Microsoft.UI.Xaml.dll") ? "Microsoft.UI.Xaml" : "FrameworkUdk";
                break;
            }
        }
    }
    if (!initialize) {
        journal.Write("XAML watcher: no InitializeXamlDiagnosticsEx in Apple Music, the themes keep looking by themselves");
        return;
    }
    // Which connection name and diagnostics DLL WinUI 3 wants is not documented: each pair is tried, and logged.
    HRESULT result = E_FAIL;
    for (const wchar_t* prefix : {L"WinUIVisualDiagConnection", L"VisualDiagConnection"}) {
        for (const wchar_t* diagnostics : {L"Microsoft.UI.Xaml.dll", L""}) {
            for (int attempt = 1; attempt <= kEndpointAttempts; attempt++) {
                std::wstring endpoint = prefix + std::to_wstring(attempt);
                result = initialize(endpoint.c_str(), GetCurrentProcessId(), diagnostics, path, kTapClsid, nullptr);
                if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
            }
            journal.Write("XAML watcher: " + from + ", " + (prefix[0] == L'W' ? "WinUI" : "plain") + " name, " +
                          (diagnostics[0] ? "named" : "empty") + " DLL: hr " + std::to_string(static_cast<long>(result)));
            if (SUCCEEDED(result)) break;
        }
        if (SUCCEEDED(result)) break;
    }
    journal.Write(SUCCEEDED(result) ? "XAML watcher: asked WinUI for the tree's changes"
                                    : "XAML watcher: WinUI refused (hr " + std::to_string(static_cast<long>(result)) +
                                          "), maybe another tool already watches it; the themes keep looking by themselves");
}

}  // namespace ampify::win

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != ampify::win::kTapClsid) return CLASS_E_CLASSNOTAVAILABLE;
    return ampify::win::g_factory.QueryInterface(riid, out);
}

// The kernel is never unloaded while Apple Music runs.
STDAPI DllCanUnloadNow() { return S_FALSE; }

#endif  // AMPIFY_WITH_XAML
