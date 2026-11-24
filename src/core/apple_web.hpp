#pragma once

// Apple's public web API (amp-api.music.apple.com), as music.apple.com uses it: the token its web player hands every
// visitor, and the audio analysis of catalog songs (tempo, key, beat and bar grid, loudness, vocal activity, energy,
// valence...). Behind Ampify.music.analysis (src/core/natives/music.cpp). No account, no user token.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "host.hpp"
#include "natives/http.hpp"

namespace ampify::appleweb {

inline constexpr std::size_t kMaxIdsPerRequest = 50;  // Apple answers 50 songs at once
inline constexpr std::size_t kMaxIdsPerCall = 250;     // half the cache: the answers and their "us" fallback fit

// The address of the web player's main script in the page of music.apple.com ("/assets/index~abc.js"), or nothing.
std::optional<std::string> IndexScriptPath(std::string_view page);
// The token written in that script: the first JSON web token in quotes ("eyJ...") of three parts, or nothing.
std::optional<std::string> TokenInScript(std::string_view script);

// Catalog ids kept: digits only, at most 20 of them each, duplicates and others dropped, in order, the first
// kMaxIdsPerCall only.
std::vector<std::string> CatalogIds(const std::vector<std::string>& ids);
// A storefront as Apple writes it ("fr"), or "us" for anything that is not two letters.
std::string Storefront(std::string_view storefront);
std::string AnalysisAddress(const std::string& storefront, const std::vector<std::string>& ids);

// {"items":{"<id>":<Apple's attributes>,...}} from one of Apple's answers ({"data":[{"id","attributes"}]}), merged
// into `items` (a JSON object's members, written as text). False when the answer is not that shape.
bool CollectItems(const std::string& answer, std::vector<std::pair<std::string, std::string>>& items);

// The requests the analysis needs, so that tests can stand in for the network.
struct Fetch {
    std::function<HttpResponse(const std::string& url)> get;  // a plain GET
    std::function<HttpResponse(const std::string& url, const HttpHeaders& headers)> getWithHeaders;
};

// The analysis of `ids` (already CatalogIds) in `storefront`, the songs missing there looked for again in "us":
// status 200 and {"items":{...}} (songs Apple has not analysed are left out), or status 0 and the reason. The token is
// kept for the process and fetched again once when Apple refuses it. Any thread; one request at a time.
HttpResponse Analysis(const Fetch& fetch, const std::string& storefront, const std::vector<std::string>& ids);

// Forgets the kept token and analyses (tests).
void ResetCache();

}  // namespace ampify::appleweb
