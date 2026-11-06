#pragma once

// The order in which Apple Music tries the addresses of a server. Windows puts IPv6 first, and the HTTP stack that
// fetches the songs (Windows' webio and WinINet) never falls back to the other family: where a server's IPv6 does not
// answer (seen for streamingaudio.itunes.apple.com on a home connection, 05/11/2026), songs stay at 0:00 while its
// IPv4 works.
// With `apple.network = ipv4-first` (the default) the kernel lists IPv4 first and keeps IPv6 after
// (src/win/network_fix.cpp). Portable, so the order is tested under Linux.

#include <cstddef>
#include <optional>
#include <vector>

namespace ampify {

// WinSock's values, so the order runs under Linux; network_fix.cpp checks them against AF_INET and AF_INET6.
inline constexpr int kFamilyIpv4 = 2;
inline constexpr int kFamilyIpv6 = 23;

// The positions of `families` (one address family per resolved address, in the order the resolver gave) with every
// IPv4 address first, each group in its own order. Nothing when nothing would move: one family only, or IPv4 already
// first.
std::optional<std::vector<size_t>> Ipv4FirstOrder(const std::vector<int>& families);

}  // namespace ampify
