#include "winrt_abi.hpp"

#include <objbase.h>

#include <cstdio>

#include "win_common.hpp"

namespace ampify::win::rt {

namespace {

// Interfaces of the Windows SDK, fixed for ever (combaseapi.h, inspectable.h, asyncinfo.h, objidl.h).
const GUID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidAgileObject = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};

// IAsyncInfo: get_Id 6, get_Status 7, get_ErrorCode 8, Cancel 9, Close 10. AsyncStatus: Started 0, Completed 1,
// Canceled 2, Error 3.
constexpr int kAsyncInfoStatusSlot = 7;
// IInspectable: GetIids 3, GetRuntimeClassName 4.
constexpr int kGetIidsSlot = 3;
constexpr int kGetRuntimeClassNameSlot = 4;

std::atomic<ActivationHook> g_hook{nullptr};

template <class Fn>
void Load(HMODULE module, const char* name, Fn& out) {
    FARPROC address = GetProcAddress(module, name);
    std::memcpy(&out, &address, sizeof out);
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

const Combase* Api() {
    static const Combase api = [] {
        Combase loaded;
        // combase.dll is part of every Windows 8+ process that uses WinRT; LoadLibrary only takes a reference.
        HMODULE module = LoadLibraryW(L"combase.dll");
        if (!module) return loaded;
        Load(module, "RoInitialize", loaded.RoInitialize);
        Load(module, "RoGetActivationFactory", loaded.RoGetActivationFactory);
        Load(module, "WindowsCreateString", loaded.WindowsCreateString);
        Load(module, "WindowsDeleteString", loaded.WindowsDeleteString);
        Load(module, "WindowsGetStringRawBuffer", loaded.WindowsGetStringRawBuffer);
        return loaded;
    }();
    if (!api.WindowsCreateString || !api.WindowsDeleteString || !api.WindowsGetStringRawBuffer) return nullptr;
    return &api;
}

bool EnsureMultithreaded() {
    thread_local int state = 0;  // 0 not tried, 1 multithreaded, 2 single-threaded
    if (state == 0) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        state = (hr == S_OK || hr == S_FALSE) ? 1 : 2;  // RPC_E_CHANGED_MODE: the thread is already single-threaded
    }
    return state == 1;
}

bool Ref::SameObject(const Ref& other) const {
    if (!m_p || !other.m_p) return m_p == other.m_p;
    Ref a, b;
    if (!Query(*this, kIidUnknown, a) || !Query(other, kIidUnknown, b)) return false;
    return a.Get() == b.Get();
}

bool Query(const Ref& object, const GUID& iid, Ref& out) {
    out.Reset();
    if (!object) return false;
    IUnknown* unknown = static_cast<IUnknown*>(object.Get());
    return SUCCEEDED(unknown->QueryInterface(iid, out.Put())) && out;
}

HString::HString(std::wstring_view text) {
    const Combase* api = Api();
    if (api) api->WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &m_h);
}

HString::~HString() {
    const Combase* api = Api();
    if (api && m_h) api->WindowsDeleteString(m_h);
}

std::string TakeString(HSTRING text) {
    const Combase* api = Api();
    if (!api || !text) return {};
    UINT32 length = 0;
    LPCWSTR buffer = api->WindowsGetStringRawBuffer(text, &length);
    std::string utf8 = buffer ? ToUtf8(std::wstring_view(buffer, length)) : std::string();
    api->WindowsDeleteString(text);
    return utf8;
}

std::string ReadString(const Ref& object, int slot) {
    HSTRING text = nullptr;
    if (FAILED(CallSlot(object, slot, &text))) return {};
    return TakeString(text);
}

bool ParseGuid(std::string_view text, GUID& out) {
    if (text.size() == 38 && text.front() == '{' && text.back() == '}') text = text.substr(1, 36);
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') return false;
    unsigned char bytes[16];
    size_t at = 0;
    for (int i = 0; i < 16; ++i) {
        if (text[at] == '-') ++at;
        int high = HexDigit(text[at]);
        int low = HexDigit(text[at + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<unsigned char>(high * 16 + low);
        at += 2;
    }
    out.Data1 = (static_cast<unsigned long>(bytes[0]) << 24) | (static_cast<unsigned long>(bytes[1]) << 16) |
                (static_cast<unsigned long>(bytes[2]) << 8) | bytes[3];
    out.Data2 = static_cast<unsigned short>((bytes[4] << 8) | bytes[5]);
    out.Data3 = static_cast<unsigned short>((bytes[6] << 8) | bytes[7]);
    std::memcpy(out.Data4, bytes + 8, 8);
    return true;
}

GUID GuidOf(const char* text) {
    GUID guid{};
    ParseGuid(text, guid);
    return guid;
}

std::string GuidText(const GUID& g) {
    char text[40];
    std::snprintf(text, sizeof text, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                  g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return text;
}

std::vector<GUID> Iids(const Ref& inspectable) {
    ULONG count = 0;
    GUID* iids = nullptr;
    std::vector<GUID> out;
    if (SUCCEEDED(CallSlot(inspectable, kGetIidsSlot, &count, &iids)) && iids) {
        out.assign(iids, iids + count);
        CoTaskMemFree(iids);
    }
    return out;
}

std::string ClassName(const Ref& inspectable) {
    HSTRING name = nullptr;
    if (FAILED(CallSlot(inspectable, kGetRuntimeClassNameSlot, &name))) return {};
    return TakeString(name);
}

// ---- Activation ---------------------------------------------------------------------------------------------------

void SetActivationHookForTests(ActivationHook hook) { g_hook.store(hook); }

Activation GetActivationFactory(std::wstring_view className, const GUID& iid, Ref& factory) {
    factory.Reset();
    if (ActivationHook hook = g_hook.load()) {
        HRESULT hr = hook(className, iid, factory.Put());
        return {SUCCEEDED(hr) && factory ? S_OK : (FAILED(hr) ? hr : E_POINTER), "test"};
    }
    const Combase* api = Api();
    if (!api) return {E_NOTIMPL, "combase.dll not available"};
    HString name(className);

    HRESULT registered = REGDB_E_CLASSNOTREG;
    if (api->RoGetActivationFactory) {
        registered = api->RoGetActivationFactory(name.Get(), iid, factory.Put());
        if (SUCCEEDED(registered) && factory) return {S_OK, "RoGetActivationFactory"};
    }

    // AMP.Services.Player -> AMP.Services.dll, then AMP.dll, as C++/WinRT's fallback, but never LoadLibrary.
    std::wstring module(className);
    while (true) {
        size_t dot = module.rfind(L'.');
        if (dot == std::wstring::npos) break;
        module.resize(dot);
        HMODULE loaded = GetModuleHandleW((module + L".dll").c_str());
        if (!loaded) continue;
        using GetFactory = HRESULT(WINAPI*)(HSTRING, void**);
        GetFactory get = nullptr;
        Load(loaded, "DllGetActivationFactory", get);
        if (!get) continue;
        Ref activation;
        HRESULT hr = get(name.Get(), activation.Put());
        if (FAILED(hr) || !activation) continue;
        if (Query(activation, iid, factory)) return {S_OK, "DllGetActivationFactory from " + ToUtf8(module) + ".dll"};
        return {E_NOINTERFACE, "DllGetActivationFactory from " + ToUtf8(module) + ".dll, interface missing"};
    }
    return {registered, "RoGetActivationFactory"};
}

// ---- Asynchronous operations --------------------------------------------------------------------------------------

AsyncState StateOf(const Ref& operation) {
    Ref info;
    if (!Query(operation, kIidAsyncInfo, info)) return AsyncState::Failed;
    int32_t status = 3;
    if (FAILED(CallSlot(info, kAsyncInfoStatusSlot, &status))) return AsyncState::Failed;
    if (status == 0) return AsyncState::Running;
    return status == 1 ? AsyncState::Completed : AsyncState::Failed;
}

bool Wait(const Ref& operation, DWORD timeoutMs) {
    ULONGLONG until = GetTickCount64() + timeoutMs;
    while (true) {
        AsyncState state = StateOf(operation);
        if (state != AsyncState::Running) return state == AsyncState::Completed;
        if (GetTickCount64() >= until) return false;
        Sleep(5);
    }
}

// ---- Property change notifications --------------------------------------------------------------------------------

ChangeCounter::ChangeCounter(const GUID& delegateIid, std::shared_ptr<std::atomic<unsigned>> count)
    : m_iid(delegateIid), m_count(std::move(count)) {}

HRESULT STDMETHODCALLTYPE ChangeCounter::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, kIidUnknown) || IsEqualGUID(iid, kIidAgileObject) || IsEqualGUID(iid, m_iid)) {
        AddRef();
        *out = this;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ChangeCounter::AddRef() { return ++m_refs; }

ULONG STDMETHODCALLTYPE ChangeCounter::Release() {
    ULONG left = --m_refs;
    if (left == 0) delete this;
    return left;
}

HRESULT STDMETHODCALLTYPE ChangeCounter::Invoke(void*, void*) {
    ++*m_count;
    return S_OK;
}

}  // namespace ampify::win::rt
