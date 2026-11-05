#include "address_order.hpp"

#include <algorithm>

namespace ampify {

std::vector<size_t> Ipv4FirstOrder(const std::vector<int>& families) {
    auto firstV4 = std::find(families.begin(), families.end(), kFamilyIpv4);
    if (firstV4 == families.end()) return {};
    // Nothing moves when no IPv6 address comes before an IPv4 one.
    if (std::find(families.begin(), firstV4, kFamilyIpv6) == firstV4) return {};
    std::vector<size_t> order(families.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_partition(order.begin(), order.end(), [&](size_t i) { return families[i] == kFamilyIpv4; });
    return order;
}

}  // namespace ampify
