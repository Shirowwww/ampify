#include "network_fix.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../core/address_order.hpp"
#include "hook.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

using GetAddrInfoNarrow = int(WSAAPI*)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
using GetAddrInfoWide = int(WSAAPI*)(PCWSTR, PCWSTR, const ADDRINFOW*, PADDRINFOW*);
using GetAddrInfoExWide = INT(WSAAPI*)(PCWSTR, PCWSTR, DWORD, LPGUID, const ADDRINFOEXW*, PADDRINFOEXW*, timeval*,
                                       LPOVERLAPPED, LPLOOKUPSERVICE_COMPLETION_ROUTINE, LPHANDLE);
using OverlappedResult = INT(WSAAPI*)(LPOVERLAPPED);

constexpr int kExtendedResults = static_cast<int>(0x80000000);  // AI_EXTENDED: ADDRINFOEX2 and later, left alone

GetAddrInfoNarrow g_narrow = nullptr;
GetAddrInfoWide g_wide = nullptr;
GetAddrInfoExWide g_ex = nullptr;
OverlappedResult g_overlappedResult = nullptr;
Journal* g_journal = nullptr;
std::mutex g_seenLock;
std::set<std::string> g_seen;  // hosts already logged
constexpr size_t kLoggedHosts = 24;

// An asynchronous GetAddrInfoExW in flight: where its result lands, its host, and the caller's completion routine.
struct Pending {
    PADDRINFOEXW* result;
    std::string host;
    LPLOOKUPSERVICE_COMPLETION_ROUTINE routine;
};
std::mutex g_pendingLock;
std::map<LPOVERLAPPED, Pending> g_pending;

// What a node says about its address, moved with it. ADDRINFOEXW also carries a blob per address.
template <typename Info>
struct Address {
    int family, socktype, protocol;
    size_t length;
    sockaddr* address;
    void* blob = nullptr;
    size_t blobLength = 0;
};

template <typename Info>
Address<Info> Take(const Info* node) {
    return {node->ai_family, node->ai_socktype, node->ai_protocol, node->ai_addrlen, node->ai_addr};
}
template <>
Address<ADDRINFOEXW> Take(const ADDRINFOEXW* node) {
    return {node->ai_family, node->ai_socktype, node->ai_protocol, node->ai_addrlen, node->ai_addr,
            node->ai_blob,   node->ai_bloblen};
}

template <typename Info>
void Put(Info* node, const Address<Info>& moved) {
    node->ai_family = moved.family;
    node->ai_socktype = moved.socktype;
    node->ai_protocol = moved.protocol;
    node->ai_addrlen = moved.length;
    node->ai_addr = moved.address;
}
template <>
void Put(ADDRINFOEXW* node, const Address<ADDRINFOEXW>& moved) {
    node->ai_family = moved.family;
    node->ai_socktype = moved.socktype;
    node->ai_protocol = moved.protocol;
    node->ai_addrlen = moved.length;
    node->ai_addr = moved.address;
    node->ai_blob = moved.blob;
    node->ai_bloblen = moved.blobLength;
}

// The addresses move, not the nodes: freeaddrinfo frees each node as the resolver allocated it.
template <typename Info>
bool PutIpv4First(Info* list) {
    std::vector<Info*> nodes;
    std::vector<int> families;
    for (Info* node = list; node; node = node->ai_next) {
        nodes.push_back(node);
        families.push_back(node->ai_family);
    }
    std::vector<size_t> order = Ipv4FirstOrder(families);
    if (order.empty()) return false;
    std::vector<Address<Info>> addresses;
    for (Info* node : nodes) addresses.push_back(Take(node));
    for (size_t i = 0; i < nodes.size(); ++i) Put(nodes[i], addresses[order[i]]);
    return true;
}

void Note(const std::string& host) {
    std::lock_guard<std::mutex> guard(g_seenLock);
    if (!g_journal || host.empty() || g_seen.size() >= kLoggedHosts || !g_seen.insert(host).second) return;
    g_journal->Write("Network: " + host + " resolved IPv6 first; its IPv4 addresses are tried first now");
}

template <typename Info>
void Reorder(Info* list, const std::string& host) noexcept {
    try {
        if (list && PutIpv4First(list)) Note(host);
    } catch (...) {  // the order stays as Windows gave it
    }
}

std::string HostOf(PCWSTR name) {
    try {
        return name ? ToUtf8(name) : std::string();
    } catch (...) {
        return {};
    }
}

// The original, also when a module was hooked before Ampify read it (never null afterwards).
template <typename Function>
Function Original(Function saved, const char* name) {
    if (saved) return saved;
    return reinterpret_cast<Function>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), name));
}

int WSAAPI Ipv4FirstNarrow(PCSTR node, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result) {
    auto original = Original(g_narrow, "getaddrinfo");
    if (!original) return WSANO_RECOVERY;
    int status = original(node, service, hints, result);
    if (status == 0 && result) Reorder(*result, node ? std::string(node) : std::string());
    return status;
}

int WSAAPI Ipv4FirstWide(PCWSTR node, PCWSTR service, const ADDRINFOW* hints, PADDRINFOW* result) {
    auto original = Original(g_wide, "GetAddrInfoW");
    if (!original) return WSANO_RECOVERY;
    int status = original(node, service, hints, result);
    if (status == 0 && result) Reorder(*result, HostOf(node));
    return status;
}

// The asynchronous result, when it comes through a completion routine.
VOID CALLBACK Ipv4FirstCompletion(DWORD error, DWORD bytes, LPWSAOVERLAPPED overlapped) {
    Pending pending{};
    {
        std::lock_guard<std::mutex> guard(g_pendingLock);
        auto found = g_pending.find(overlapped);
        if (found == g_pending.end()) return;  // cannot happen: registered before the call
        pending = found->second;
        g_pending.erase(found);
    }
    if (error == 0 && pending.result) Reorder(*pending.result, pending.host);
    if (pending.routine) pending.routine(error, bytes, overlapped);
}

INT WSAAPI Ipv4FirstEx(PCWSTR name, PCWSTR service, DWORD space, LPGUID provider, const ADDRINFOEXW* hints,
                       PADDRINFOEXW* result, timeval* timeout, LPOVERLAPPED overlapped,
                       LPLOOKUPSERVICE_COMPLETION_ROUTINE routine, LPHANDLE handle) {
    auto original = Original(g_ex, "GetAddrInfoExW");
    if (!original) return WSANO_RECOVERY;
    bool extended = hints && (hints->ai_flags & kExtendedResults);
    if (extended || !result) return original(name, service, space, provider, hints, result, timeout, overlapped, routine, handle);
    if (!overlapped) {
        INT status = original(name, service, space, provider, hints, result, timeout, nullptr, nullptr, handle);
        if (status == 0) Reorder(*result, HostOf(name));
        return status;
    }
    // Asynchronous: the result is reordered when it lands, through Ampify's completion routine (which then calls the
    // caller's) or through GetAddrInfoExOverlappedResult for a caller that waits on the event.
    try {
        std::lock_guard<std::mutex> guard(g_pendingLock);
        g_pending[overlapped] = Pending{result, HostOf(name), routine};
    } catch (...) {
        return original(name, service, space, provider, hints, result, timeout, overlapped, routine, handle);
    }
    INT status = original(name, service, space, provider, hints, result, timeout, overlapped,
                          routine ? &Ipv4FirstCompletion : nullptr, handle);
    if (status != WSA_IO_PENDING) {
        // Done at once, or failed. A routine's entry stays after a success: if Windows calls the routine anyway, it
        // must still find the caller's (reordering twice changes nothing).
        if (status != 0 || !routine) {
            std::lock_guard<std::mutex> guard(g_pendingLock);
            g_pending.erase(overlapped);
        }
        if (status == 0) Reorder(*result, HostOf(name));
    }
    return status;
}

INT WSAAPI Ipv4FirstOverlappedResult(LPOVERLAPPED overlapped) {
    auto original = Original(g_overlappedResult, "GetAddrInfoExOverlappedResult");
    if (!original) return WSANO_RECOVERY;
    INT status = original(overlapped);
    if (status == WSAEINPROGRESS) return status;  // still resolving: the entry stays
    Pending pending{};
    {
        std::lock_guard<std::mutex> guard(g_pendingLock);
        auto found = g_pending.find(overlapped);
        if (found == g_pending.end() || found->second.routine) return status;  // not ours, or a routine will run
        pending = found->second;
        g_pending.erase(found);
    }
    if (status == 0 && pending.result) Reorder(*pending.result, pending.host);
    return status;
}

}  // namespace

void InstallIpv4First(Journal& journal) {
    g_journal = &journal;
    size_t hooked = HookImportFromNowOn("WS2_32.dll", "getaddrinfo", reinterpret_cast<void*>(&Ipv4FirstNarrow),
                                        reinterpret_cast<void**>(&g_narrow));
    hooked += HookImportFromNowOn("WS2_32.dll", "GetAddrInfoW", reinterpret_cast<void*>(&Ipv4FirstWide),
                                  reinterpret_cast<void**>(&g_wide));
    // Windows' HTTP stack (webio.dll under WinHTTP, which Windows.Web.Http uses): how Apple's player fetches the songs.
    hooked += HookImportFromNowOn("WS2_32.dll", "GetAddrInfoExW", reinterpret_cast<void*>(&Ipv4FirstEx),
                                  reinterpret_cast<void**>(&g_ex));
    hooked += HookImportFromNowOn("WS2_32.dll", "GetAddrInfoExOverlappedResult",
                                  reinterpret_cast<void*>(&Ipv4FirstOverlappedResult),
                                  reinterpret_cast<void**>(&g_overlappedResult));
    journal.Write("Network: IPv4 addresses tried first (apple.network = ipv4-first), " + std::to_string(hooked) +
                  " resolver import(s) hooked, and those of modules loaded later");
}

}  // namespace ampify::win
