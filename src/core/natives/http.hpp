#pragma once

// The optional host service behind Ampify.http.post, and behind Ampify.http.get when it carries headers. A plain get
// uses Host::HttpGet, which every host has.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../host.hpp"

namespace ampify {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// The host of an https URL, lowercase, or empty when the URL has another form (src/core/natives/http.cpp).
std::string HttpsHost(const std::string& url);

struct HttpPostHost {
    static constexpr std::string_view kCapabilityName = "http.post";
    virtual ~HttpPostHost() = default;

    // Called from a worker thread of the element, possibly with other requests at the same time: an implementation
    // must not touch unguarded state. Only https URLs whose host the manifest declares reach this call, with at most a few checked headers
    // (content-type, authorization, accept, origin; lowercase names, no line breaks). A failure comes back as status 0
    // with the reason in text, never as an exception.
    virtual HttpResponse HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) = 0;

    // A GET with the same checked headers, under the same rules. A host without it answers status 0.
    virtual HttpResponse HttpGetWithHeaders(const std::string&, const HttpHeaders&) {
        return HttpResponse{0, "headers on a GET are not supported by this host"};
    }
};

// The optional host service behind Ampify.http.open: shows a page in the user's browser, for a sign-in page such as
// Last.fm's. Only https URLs whose host the manifest declares reach it, one every few seconds per element.
struct BrowserHost {
    static constexpr std::string_view kCapabilityName = "http.open";
    virtual ~BrowserHost() = default;

    // Called on the JavaScript thread. True when the browser was asked to show the page.
    virtual bool OpenInBrowser(const std::string& url) = 0;
};

}  // namespace ampify
