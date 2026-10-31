#pragma once

// The optional host service behind Ampify.http.post. Ampify.http.get uses Host::HttpGet, which every host has.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../host.hpp"

namespace ampify {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpPostHost {
    static constexpr std::string_view kCapabilityName = "http.post";
    virtual ~HttpPostHost() = default;

    // Only https URLs whose host the manifest declares reach this call, with at most a few checked headers
    // (content-type, authorization, accept; lowercase names, no line breaks). A failure comes back as status 0
    // with the reason in text, never as an exception.
    virtual HttpResponse HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) = 0;
};

}  // namespace ampify
