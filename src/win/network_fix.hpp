#pragma once

// apple.network = ipv4-first (the default): every name Apple Music resolves (getaddrinfo, GetAddrInfoW, GetAddrInfoExW,
// the last one through Windows' HTTP stack, webio.dll and WININET.dll, which fetch the songs) lists the server's IPv4
// addresses before its IPv6 ones, IPv6 kept after. Where a server's IPv6 does not answer, that stack never falls back
// and songs stay at 0:00 (streamingaudio, aod and mvod.itunes.apple.com). Installed once per process, in
// AppleMusic.exe and AMPLibraryAgent.exe; nothing in a protected (DRM) module is hooked, and no code is patched.
//
// apple.proxy = no-autodetect (off by default): DetectAutoProxyUrl, which Apple's network library (CFNetwork.dll)
// calls when Windows' "Automatically detect settings" is on, answers "nothing found" at once instead of waiting for
// DHCP and DNS. A proxy set by hand or by a script address still applies.
//
// apple.watchdog = on also times the lookups and the proxy detection, and logs those of 300 ms or more.

#include "../core/journal.hpp"

namespace ampify::win {

struct NetworkFixes {
    bool ipv4First = false;
    bool skipProxyDetection = false;
    bool timeWaits = false;
};

// Hooks the resolver and proxy-detection imports of every module, now and loaded later, for the fixes asked. Logs once
// what is on, and each host whose order changed (the first few). Nothing is hooked when nothing is asked.
void InstallNetworkFixes(Journal& journal, const NetworkFixes& fixes);

}  // namespace ampify::win
