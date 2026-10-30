#pragma once

// Plain WinRT at the ABI level, for the player (win/amp_player.*, win/smtc_session.*): no C++/WinRT projection, so
// that MinGW builds it as well as MSVC. The combase functions are looked up at run time, so the kernel still loads
// where one is missing. Every interface method is called by its vtable slot, with the slot written next to the call
// and its evidence (winmd method order, or the Windows SDK) in a comment. Nothing here throws.

#include <windows.h>
#include <unknwn.h>
#include <inspectable.h>
#include <winstring.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amk::win::rt {

// The combase.dll functions the player needs. Null when combase.dll or one of them is missing.
struct Combase {
    HRESULT(WINAPI* RoInitialize)(int type) = nullptr;
    HRESULT(WINAPI* RoGetActivationFactory)(HSTRING classId, REFIID iid, void** factory) = nullptr;
    HRESULT(WINAPI* WindowsCreateString)(LPCWSTR text, UINT32 length, HSTRING* out) = nullptr;
    HRESULT(WINAPI* WindowsDeleteString)(HSTRING text) = nullptr;
    LPCWSTR(WINAPI* WindowsGetStringRawBuffer)(HSTRING text, UINT32* length) = nullptr;
};
const Combase* Api();

// Joins the multithreaded apartment on this thread, once. False when the thread already lives in a single-threaded
// apartment (the calls still work, the journal says it).
bool EnsureMultithreaded();

// An owned reference to a COM object, released at the end of its life.
class Ref {
public:
    Ref() = default;
    explicit Ref(void* owned) : m_p(static_cast<IUnknown*>(owned)) {}
    Ref(const Ref& other) : m_p(other.m_p) {
        if (m_p) m_p->AddRef();
    }
    Ref(Ref&& other) noexcept : m_p(other.m_p) { other.m_p = nullptr; }
    Ref& operator=(Ref other) noexcept {
        std::swap(m_p, other.m_p);
        return *this;
    }
    ~Ref() { Reset(); }

    void Reset() {
        if (m_p) m_p->Release();
        m_p = nullptr;
    }
    // For an out parameter: releases the current object, then hands out the slot.
    void** Put() {
        Reset();
        return reinterpret_cast<void**>(&m_p);
    }
    void* Get() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
    // COM identity: two references to the same object compare equal through IUnknown.
    bool SameObject(const Ref& other) const;

private:
    IUnknown* m_p = nullptr;
};

// QueryInterface. False (and `out` empty) when the object does not have the interface.
bool Query(const Ref& object, const GUID& iid, Ref& out);

// Calls the method in `slot` of the vtable of `object`. The caller writes the slot and its evidence next to the call.
template <class... Args>
HRESULT CallSlot(void* object, int slot, Args... args) {
    if (!object) return E_POINTER;
    using Method = HRESULT(STDMETHODCALLTYPE*)(void*, Args...);
    void** vtable = *static_cast<void***>(object);
    Method method;
    std::memcpy(&method, &vtable[slot], sizeof method);  // an object pointer to a function pointer, without UB casts
    return method(object, args...);
}
template <class... Args>
HRESULT CallSlot(const Ref& object, int slot, Args... args) {
    return CallSlot(object.Get(), slot, args...);
}

// An HSTRING made from text, deleted at the end of its life.
class HString {
public:
    explicit HString(std::wstring_view text);
    ~HString();
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HSTRING Get() const { return m_h; }

private:
    HSTRING m_h = nullptr;
};

// The text of an HSTRING returned by a method, as UTF-8; deletes it.
std::string TakeString(HSTRING text);

// Reads a string property (a getter whose only parameter is HSTRING*) and returns it as UTF-8. Empty on failure.
std::string ReadString(const Ref& object, int slot);

// "34D100F9-0F0A-5980-8F04-7AF57FDB29B0", with or without braces. False when malformed.
bool ParseGuid(std::string_view text, GUID& out);
GUID GuidOf(const char* text);  // for constants known to be well formed; zero when not
std::string GuidText(const GUID& guid);

// IInspectable::GetIids (slot 3) and GetRuntimeClassName (slot 4).
std::vector<GUID> Iids(const Ref& inspectable);
std::string ClassName(const Ref& inspectable);

// ---- Activation ---------------------------------------------------------------------------------------------------

// Tests replace the activation of runtime classes with fakes. With a hook set, only the hook is asked.
using ActivationHook = HRESULT (*)(std::wstring_view className, const GUID& iid, void** factory);
void SetActivationHookForTests(ActivationHook hook);

struct Activation {
    HRESULT result = E_FAIL;
    std::string how;  // "RoGetActivationFactory", "DllGetActivationFactory de AMP.Services.dll", "test"
};

// The activation factory of `className`, as the interface `iid`. First RoGetActivationFactory; then, as C++/WinRT
// does for classes the package does not register, DllGetActivationFactory of a DLL named after the class's namespace
// ("AMP.Services.dll" for "AMP.Services.Player") — only when that DLL is already loaded in this process, so the kit
// never loads Apple's code into a process that does not have it.
Activation GetActivationFactory(std::wstring_view className, const GUID& iid, Ref& factory);

// ---- Asynchronous operations --------------------------------------------------------------------------------------

enum class AsyncState { Running, Completed, Failed };
// IAsyncInfo::get_Status of an IAsyncAction or IAsyncOperation<T>.
AsyncState StateOf(const Ref& operation);
// Polls until the operation ends or `timeoutMs` passes. True when it completed.
bool Wait(const Ref& operation, DWORD timeoutMs);
// IAsyncOperation<T>::GetResults is slot 8 for every T (put_Completed 6, get_Completed 7, GetResults 8), and so is
// IAsyncAction::GetResults.
inline constexpr int kAsyncGetResultsSlot = 8;

// ---- Property change notifications --------------------------------------------------------------------------------

// A delegate that counts its calls: PropertyChangedEventHandler, or any delegate whose Invoke (slot 3) takes two
// pointers. Agile, so that the object that raises the event may call it from any thread.
class ChangeCounter final : public IUnknown {
public:
    ChangeCounter(const GUID& delegateIid, std::shared_ptr<std::atomic<unsigned>> count);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    virtual HRESULT STDMETHODCALLTYPE Invoke(void* sender, void* args);

private:
    ~ChangeCounter() = default;
    std::atomic<ULONG> m_refs{1};
    GUID m_iid;
    std::shared_ptr<std::atomic<unsigned>> m_count;
};

}  // namespace amk::win::rt
