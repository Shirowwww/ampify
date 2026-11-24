#include "apple_web.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>

#include "json.hpp"

namespace ampify::appleweb {

namespace {

constexpr char kPlayerPage[] = "https://music.apple.com/us/browse";
constexpr char kPlayerHost[] = "https://music.apple.com";
constexpr char kApi[] = "https://amp-api.music.apple.com/v1/catalog/";
constexpr auto kTokenLife = std::chrono::hours(24 * 5);  // Apple's web tokens last months; fetched again sooner
constexpr std::size_t kKeptAnalyses = 500;
static_assert(kMaxIdsPerCall * 2 <= kKeptAnalyses, "a call's answers and their fallback must fit in the cache");
constexpr std::size_t kMaxIdDigits = 20;
constexpr int kHttpUnauthorized = 401;
constexpr int kHttpForbidden = 403;
constexpr int kHttpNotFound = 404;
constexpr int kHttpOk = 200;

struct Cache {
    std::mutex mutex;
    std::string token;
    std::chrono::steady_clock::time_point tokenAt{};
    std::map<std::string, std::string> analyses;  // "<storefront>/<id>" -> Apple's attributes as JSON text
    std::deque<std::string> order;                // oldest first, to keep kKeptAnalyses at most
};

Cache& State() {
    static Cache* cache = new Cache();  // never destroyed: worker threads may still use it at exit
    return *cache;
}

bool IsTokenCharacter(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

// The token from the player's page and script, or status 0 and the reason.
HttpResponse FetchToken(const Fetch& fetch) {
    HttpResponse page = fetch.get(kPlayerPage);
    if (page.status != kHttpOk) return {0, "music.apple.com did not answer (status " + std::to_string(page.status) + ")"};
    std::optional<std::string> path = IndexScriptPath(page.text);
    if (!path) return {0, "no player script in music.apple.com's page"};
    HttpResponse script = fetch.get(std::string(kPlayerHost) + *path);
    if (script.status != kHttpOk) return {0, "the player script did not load (status " + std::to_string(script.status) + ")"};
    std::optional<std::string> token = TokenInScript(script.text);
    if (!token) return {0, "no token in Apple's player script"};
    return {kHttpOk, *token};
}

// The kept token, or a new one. Under the cache's lock.
HttpResponse Token(const Fetch& fetch, Cache& cache, bool renew) {
    auto now = std::chrono::steady_clock::now();
    if (!renew && !cache.token.empty() && now - cache.tokenAt < kTokenLife) return {kHttpOk, cache.token};
    HttpResponse got = FetchToken(fetch);
    if (got.status == kHttpOk) {
        cache.token = got.text;
        cache.tokenAt = now;
    }
    return got;
}

void Keep(Cache& cache, const std::string& key, std::string attributes) {
    if (cache.analyses.emplace(key, std::move(attributes)).second) cache.order.push_back(key);
    while (cache.order.size() > kKeptAnalyses) {
        cache.analyses.erase(cache.order.front());
        cache.order.pop_front();
    }
}

// Asks Apple for `ids` in one storefront, the answers kept in the cache. Status 200, or 0 and the reason.
HttpResponse Ask(const Fetch& fetch, Cache& cache, const std::string& storefront, const std::vector<std::string>& ids) {
    for (std::size_t start = 0; start < ids.size(); start += kMaxIdsPerRequest) {
        std::vector<std::string> chunk(ids.begin() + static_cast<std::ptrdiff_t>(start),
                                       ids.begin() + static_cast<std::ptrdiff_t>(std::min(ids.size(), start + kMaxIdsPerRequest)));
        HttpResponse answer;
        for (bool renew : {false, true}) {
            HttpResponse token = Token(fetch, cache, renew);
            if (token.status != kHttpOk) return token;
            HttpHeaders headers{{"authorization", "Bearer " + token.text}, {"origin", kPlayerHost}};
            answer = fetch.getWithHeaders(AnalysisAddress(storefront, chunk), headers);
            if (answer.status != kHttpUnauthorized && answer.status != kHttpForbidden) break;
        }
        std::vector<std::pair<std::string, std::string>> items;
        if (answer.status != kHttpNotFound) {  // 404: none of these songs is analysed there
            if (answer.status != kHttpOk) return {0, "Apple refused the analysis (status " + std::to_string(answer.status) + ")"};
            if (!CollectItems(answer.text, items)) return {0, "Apple's analysis did not read as expected"};
        }
        for (auto& [id, attributes] : items) Keep(cache, storefront + "/" + id, std::move(attributes));
        // A song Apple has not analysed there is kept as such (empty), so that it is not asked again and again.
        for (const std::string& id : chunk) Keep(cache, storefront + "/" + id, "");
    }
    return {kHttpOk, ""};
}

}  // namespace

std::optional<std::string> IndexScriptPath(std::string_view page) {
    constexpr std::string_view kStart = "/assets/index";
    for (std::size_t at = page.find(kStart); at != std::string_view::npos; at = page.find(kStart, at + 1)) {
        std::size_t end = at + kStart.size();
        if (end >= page.size() || (page[end] != '~' && page[end] != '-')) continue;
        while (end < page.size() && (IsTokenCharacter(page[end]) || page[end] == '.' || page[end] == '~')) end++;
        std::string_view path = page.substr(at, end - at);
        if (path.size() > 3 && path.substr(path.size() - 3) == ".js") return std::string(path);
    }
    return std::nullopt;
}

std::optional<std::string> TokenInScript(std::string_view script) {
    constexpr std::string_view kStart = "\"eyJ";
    for (std::size_t at = script.find(kStart); at != std::string_view::npos; at = script.find(kStart, at + 1)) {
        std::size_t end = at + 1;
        int dots = 0;
        while (end < script.size() && (IsTokenCharacter(script[end]) || script[end] == '.')) {
            if (script[end] == '.') dots++;
            end++;
        }
        if (end < script.size() && script[end] == '"' && dots == 2) return std::string(script.substr(at + 1, end - at - 1));
    }
    return std::nullopt;
}

std::vector<std::string> CatalogIds(const std::vector<std::string>& ids) {
    std::vector<std::string> kept;
    for (const std::string& id : ids) {
        if (kept.size() == kMaxIdsPerCall) break;
        bool digits = !id.empty() && id.size() <= kMaxIdDigits &&
                      std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        if (digits && std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
    }
    return kept;
}

std::string Storefront(std::string_view storefront) {
    if (storefront.size() != 2 || !std::isalpha(static_cast<unsigned char>(storefront[0])) ||
        !std::isalpha(static_cast<unsigned char>(storefront[1]))) {
        return "us";
    }
    std::string lower(storefront);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

std::string AnalysisAddress(const std::string& storefront, const std::vector<std::string>& ids) {
    std::string url = std::string(kApi) + storefront + "/audio-analysis?ids=";
    for (std::size_t i = 0; i < ids.size(); i++) url += (i ? "," : "") + ids[i];
    return url;
}

bool CollectItems(const std::string& answer, std::vector<std::pair<std::string, std::string>>& items) {
    std::optional<Json> json = Json::Parse(answer);
    const Json* data = json ? json->Find("data") : nullptr;
    if (!data || !data->IsArray()) return false;
    for (const Json& entry : *data->AsArray()) {
        const Json* id = entry.Find("id");
        const Json* attributes = entry.Find("attributes");
        if (id && id->IsString() && attributes && attributes->IsObject()) items.emplace_back(*id->AsString(), attributes->Dump());
    }
    return true;
}

HttpResponse Analysis(const Fetch& fetch, const std::string& storefront, const std::vector<std::string>& ids) {
    Cache& cache = State();
    std::lock_guard<std::mutex> lock(cache.mutex);
    // Songs not asked in `place` yet; with `orAbsent`, also those Apple has no analysis of there.
    auto missing = [&](const std::string& place, bool orAbsent = false) {
        std::vector<std::string> left;
        for (const std::string& id : ids) {
            auto found = cache.analyses.find(place + "/" + id);
            if (found == cache.analyses.end() || (orAbsent && found->second.empty())) left.push_back(id);
        }
        return left;
    };
    std::vector<std::string> places{storefront};
    if (storefront != "us") places.push_back("us");
    for (const std::string& place : places) {
        std::vector<std::string> left = missing(place);
        if (place != storefront) {
            // Only what the user's storefront did not have.
            std::vector<std::string> notHome = missing(storefront, true);
            left.erase(std::remove_if(left.begin(), left.end(), [&](const std::string& id) {
                return std::find(notHome.begin(), notHome.end(), id) == notHome.end();
            }), left.end());
        }
        if (left.empty()) continue;
        HttpResponse asked = Ask(fetch, cache, place, left);
        if (asked.status != kHttpOk) return asked;
    }
    Json::Object items;
    for (const std::string& id : ids) {
        for (const std::string& place : places) {
            auto found = cache.analyses.find(place + "/" + id);
            if (found == cache.analyses.end() || found->second.empty()) continue;
            if (std::optional<Json> attributes = Json::Parse(found->second)) items.emplace_back(id, std::move(*attributes));
            break;
        }
    }
    Json::Object answer;
    answer.emplace_back("items", Json(std::move(items)));
    return {kHttpOk, Json(std::move(answer)).Dump()};
}

void ResetCache() {
    Cache& cache = State();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.token.clear();
    cache.analyses.clear();
    cache.order.clear();
}

}  // namespace ampify::appleweb
