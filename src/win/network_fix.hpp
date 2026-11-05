#pragma once

// apple.network = ipv4-first (the default): every name Apple Music resolves (getaddrinfo, GetAddrInfoW, GetAddrInfoExW,
// the last one through Windows' HTTP stack, webio.dll and WININET.dll, which fetch the songs) lists the server's IPv4
// addresses before its IPv6 ones, IPv6 kept after. Where a server's IPv6 does not answer, that stack never falls back
// and songs stay at 0:00 (05/11/2026: streamingaudio, aod and mvod.itunes.apple.com). Installed once per process, in
// AppleMusic.exe and AMPLibraryAgent.exe; nothing in a protected (DRM) module is hooked.

#include "../core/journal.hpp"

namespace ampify::win {

// Hooks the resolver imports of every module, now and loaded later. Logs how many were hooked, and each host whose
// order changed (the first few).
void InstallIpv4First(Journal& journal);

}  // namespace ampify::win
