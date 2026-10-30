#include "ui_thread.hpp"

#include <unknwn.h>

#include <atomic>
#include <mutex>
#include <new>
#include <utility>

namespace amk::win {

namespace {

// ---- Windows.System.DispatcherQueue, declared by hand from its metadata (MinGW ships no WinRT projection) --------
// Vtable order of each interface as in Windows.System.idl.

using Hstring = void*;  // HSTRING

struct InspectableAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(Hstring* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* level) = 0;
};

struct DispatcherQueueHandlerAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke() = 0;
};

struct DispatcherQueueAbi : InspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE CreateTimer(IUnknown** timer) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryEnqueue(DispatcherQueueHandlerAbi* callback, unsigned char* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryEnqueueWithPriority(int priority, DispatcherQueueHandlerAbi* callback,
                                                             unsigned char* result) = 0;
    // add_ShutdownStarting, remove_ShutdownStarting, add_ShutdownCompleted, remove_ShutdownCompleted: not used.
};

struct DispatcherQueueStaticsAbi : InspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentThread(DispatcherQueueAbi** queue) = 0;
};

// {603E88E4-A338-4FFE-A457-A5CFB9CEB899}
constexpr IID kIidDispatcherQueue = {0x603e88e4, 0xa338, 0x4ffe, {0xa4, 0x57, 0xa5, 0xcf, 0xb9, 0xce, 0xb8, 0x99}};
// {DFA2DC9C-1A2D-4917-98F2-939AF1D6E0C8}
constexpr IID kIidDispatcherQueueHandler = {0xdfa2dc9c, 0x1a2d, 0x4917, {0x98, 0xf2, 0x93, 0x9a, 0xf1, 0xd6, 0xe0, 0xc8}};
// {A96D83D7-9371-4517-9245-D0824AC12C74}
constexpr IID kIidDispatcherQueueStatics = {0xa96d83d7, 0x9371, 0x4517, {0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74}};
// {94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90}
constexpr IID kIidAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

// {00000000-0000-0000-C000-000000000046}
constexpr IID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

constexpr int kPriorityLow = -10;  // DispatcherQueuePriority::Low

bool SameIid(const IID& a, const IID& b) {
    return InlineIsEqualGUID(a, b) != 0;
}

// The callback handed to TryEnqueue: runs the work once, on the queue's thread. Agile, so any thread may hand it over.
class Handler final : public DispatcherQueueHandlerAbi {
public:
    explicit Handler(std::function<void()> work) : m_work(std::move(work)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (SameIid(iid, kIidUnknown) || SameIid(iid, kIidDispatcherQueueHandler) || SameIid(iid, kIidAgileObject)) {
            *object = static_cast<DispatcherQueueHandlerAbi*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = --m_refs;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        try {
            if (m_work) m_work();
        } catch (...) {
            // A failing task must not take the interface down with it. The caller logs what it needs itself.
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refs{1};
    std::function<void()> m_work;
};

struct QueueState {
    std::mutex mutex;
    DispatcherQueueAbi* queue = nullptr;  // adopted by the interface thread, one reference held
    DWORD queueThread = 0;                // the thread that adopted it
    bool attached = false;
};

QueueState& State() {
    static QueueState state;
    return state;
}

std::atomic<const UiThreadBackend*> g_backend{nullptr};

const UiThreadBackend* Backend() {
    return g_backend.load(std::memory_order_acquire);
}

// DispatcherQueue.GetForCurrentThread through RoGetActivationFactory, loaded at run time from combase.dll.
DispatcherQueueAbi* QueueForCurrentThread() {
    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (!combase) return nullptr;
    using CreateReference = HRESULT(WINAPI*)(const wchar_t*, UINT32, void*, Hstring*);
    using GetFactory = HRESULT(WINAPI*)(Hstring, REFIID, void**);
    auto createReference = reinterpret_cast<CreateReference>(reinterpret_cast<void*>(GetProcAddress(combase, "WindowsCreateStringReference")));
    auto getFactory = reinterpret_cast<GetFactory>(reinterpret_cast<void*>(GetProcAddress(combase, "RoGetActivationFactory")));
    if (!createReference || !getFactory) return nullptr;

    static const wchar_t kClass[] = L"Windows.System.DispatcherQueue";
    alignas(void*) unsigned char header[24] = {};  // HSTRING_HEADER: 5 pointer-sized words at most on x64
    Hstring name = nullptr;
    if (FAILED(createReference(kClass, static_cast<UINT32>(sizeof(kClass) / sizeof(wchar_t) - 1), header, &name))) {
        return nullptr;
    }
    DispatcherQueueStaticsAbi* statics = nullptr;
    if (FAILED(getFactory(name, kIidDispatcherQueueStatics, reinterpret_cast<void**>(&statics))) || !statics) return nullptr;
    DispatcherQueueAbi* queue = nullptr;
    HRESULT result = statics->GetForCurrentThread(&queue);
    statics->Release();
    return SUCCEEDED(result) ? queue : nullptr;
}

struct WindowSearch {
    DWORD processId;
    DWORD threadId;
};

BOOL CALLBACK FindMainWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);

    DWORD owner = 0;
    DWORD thread = GetWindowThreadProcessId(window, &owner);
    if (owner != search->processId || !IsWindowVisible(window)) return TRUE;
    if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;  // a dialog or a tool window, not the main one

    search->threadId = thread;
    return FALSE;
}

}  // namespace

void SetUiThreadBackend(const UiThreadBackend* backend) {
    g_backend.store(backend, std::memory_order_release);
}

bool AdoptCurrentThreadQueue(void* dispatcherQueue) {
    DispatcherQueueAbi* queue = nullptr;
    if (dispatcherQueue) {
        auto* unknown = static_cast<IUnknown*>(dispatcherQueue);
        if (FAILED(unknown->QueryInterface(kIidDispatcherQueue, reinterpret_cast<void**>(&queue)))) queue = nullptr;
    } else {
        queue = QueueForCurrentThread();
    }
    if (!queue) return false;

    QueueState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.queue) state.queue->Release();
    state.queue = queue;
    state.queueThread = GetCurrentThreadId();
    return true;
}

bool AttachUiThread(DWORD processId, HINSTANCE module) {
    if (const UiThreadBackend* backend = Backend()) return backend->attach(processId, module);

    WindowSearch search{processId, 0};
    EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
    if (search.threadId == 0) return false;

    QueueState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.queue || state.queueThread != search.threadId) return false;  // that thread has not adopted its queue
    state.attached = true;
    return true;
}

void DetachUiThread() {
    if (const UiThreadBackend* backend = Backend()) {
        backend->detach();
        return;
    }
    QueueState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.queue) state.queue->Release();
    state.queue = nullptr;
    state.queueThread = 0;
    state.attached = false;
}

bool PostToUiThread(std::function<void()> work) {
    if (const UiThreadBackend* backend = Backend()) return backend->post(std::move(work));

    QueueState& state = State();
    DispatcherQueueAbi* queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.attached || !state.queue) return false;
        queue = state.queue;
        queue->AddRef();
    }
    auto* handler = new (std::nothrow) Handler(std::move(work));
    unsigned char accepted = 0;
    HRESULT result = handler ? queue->TryEnqueueWithPriority(kPriorityLow, handler, &accepted) : E_OUTOFMEMORY;
    if (handler) handler->Release();  // the queue holds its own reference while the work waits
    queue->Release();
    return SUCCEEDED(result) && accepted != 0;
}

DWORD UiThreadId() {
    if (const UiThreadBackend* backend = Backend()) return backend->threadId();
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().attached ? State().queueThread : 0;
}

bool UiThreadAttached() {
    if (const UiThreadBackend* backend = Backend()) return backend->attached();
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().attached;
}

}  // namespace amk::win
