// `ampify publish`: sends an element to the public catalog as a pull request, with the GitHub CLI and no clone.

#include <cstdio>
#include <map>
#include <set>

#include "../core/base64.hpp"
#include "internal.hpp"
#include "messages.hpp"
#include "platform.hpp"
#include "release_facts.hpp"

namespace ampify::cli {

namespace {

const std::string kCatalogPage = "https://github.com/" + std::string(kCatalogRepository);
constexpr std::string_view kBaseBranch = "main";

// What one run sends. `target` is the folder of the element in the catalog repository.
struct Publication {
    Manifest manifest;
    std::vector<ZipSource> files;
    std::string branch;
    std::string target;
};

// What the run learns from GitHub, step by step.
struct Remote {
    std::string login;
    std::string fork;
    std::map<std::string, std::string> existing;  // path -> blob sha, for the files already under the target
};

std::string PermissionsText(const Manifest& manifest) {
    std::string text;
    for (const std::string& permission : manifest.permissions) text += (text.empty() ? "" : ", ") + permission;
    return text.empty() ? "none" : text;
}

int Stopped(Context& ctx, const std::string& what) {
    return Failure(ctx, what + " Run ampify publish again (what was already sent is reused), or follow the steps at " +
                            kCatalogPage);
}

// Runs gh and returns what it printed. gh writes its own error messages to the console.
std::optional<std::string> Gh(const std::vector<std::string>& arguments) {
    std::vector<std::string> command{"gh"};
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::filesystem::path output = platform::TemporaryFile("ampify-gh");
    int code = platform::Run(command, output);
    std::string text;
    ReadWholeFile(output, text);
    std::error_code ec;
    std::filesystem::remove(output, ec);
    if (code != 0) return std::nullopt;
    return std::string(TrimSpace(text));
}

// Percent-encodes a repository path for the contents API, keeping the slashes.
std::string EncodedPath(const std::string& path) {
    static constexpr std::string_view kKept = "-._~/";
    std::string out;
    for (unsigned char c : path) {
        if (std::isalnum(c) || kKept.find(static_cast<char>(c)) != std::string_view::npos) {
            out += static_cast<char>(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

// Sends a JSON body to the GitHub API. The body goes through a file: it can hold a whole picture.
bool GhSend(const std::string& method, const std::string& endpoint, const std::string& body) {
    std::filesystem::path file = platform::TemporaryFile("ampify-body");
    std::string error;
    bool sent = WriteTextFile(file, body, error) && Gh({"api", "-X", method, endpoint, "--input", Utf8(file)});
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return sent;
}

std::string PullRequestBody(const Publication& publication, bool update) {
    const Manifest& manifest = publication.manifest;
    std::ostringstream out;
    out << manifest.description << "\n\n"
        << "| | |\n|---|---|\n"
        << "| Name | " << manifest.name << " |\n"
        << "| Id | `" << manifest.id << "` |\n"
        << "| Kind | " << ToString(manifest.kind) << " |\n"
        << "| Version | " << manifest.version << (update ? " (update)" : " (new)") << " |\n"
        << "| Author | " << manifest.author << " |\n"
        << "| Permissions | " << PermissionsText(manifest) << " |\n"
        << "| Files | " << publication.files.size() << " in `" << publication.target << "/` |\n\n"
        << "Sent with `ampify publish`, which ran `ampify validate`"
        << (manifest.main ? " and `ampify test`" : "") << " on this folder first.\n";
    return out.str();
}

void PrintManualSteps(Context& ctx, const Publication& publication, const std::string& reason) {
    ctx.out << reason << "\nSo the pull request is yours to open. It takes a few minutes:\n"
            << "  1. Sign in to GitHub and open " << kCatalogPage << "\n"
            << "  2. Click Fork to get your own copy.\n"
            << "  3. In your copy, click Add file > Upload files, and drop the files of your folder into "
            << publication.target << "/ (replace the ones already there).\n"
            << "  4. Choose \"Create a new branch\" named " << publication.branch << ", then commit.\n"
            << "  5. Click Contribute > Open pull request, and send it.\n"
            << "Or install gh from https://cli.github.com, run gh auth login, then run ampify publish again.\n";
    if (ctx.options.dryRun) {
        ctx.out << "(--dry-run: the page is not opened.)\n";
        return;
    }
    // The file manager's command also opens an address in the browser: explorer.exe on Windows, xdg-open elsewhere.
    if (!platform::StartDetached(platform::OpenFolderCommand(std::filesystem::path(kCatalogPage)))) {
        ctx.out << "The page could not be opened; open it yourself.\n";
    }
}

void PrintPlan(Context& ctx, const Publication& publication) {
    const Manifest& manifest = publication.manifest;
    ctx.out << "--dry-run: nothing was sent.\n"
            << "  element      " << manifest.name << " " << manifest.version << " (" << ToString(manifest.kind) << ", "
            << manifest.id << ")\n"
            << "  permissions  " << PermissionsText(manifest) << "\n"
            << "  files        " << publication.files.size() << "\n"
            << "With the GitHub CLI signed in, ampify publish would:\n"
            << "  1. fork " << kCatalogRepository << " (or reuse your fork)\n"
            << "  2. create the branch " << publication.branch << " in your fork\n"
            << "  3. put the " << publication.files.size() << " file(s) in " << publication.target
            << "/, replacing the previous version's files\n"
            << "  4. open a pull request on " << kCatalogRepository << " and print its address\n";
}

// Your fork, kept level with the catalog. The author of the catalog works in the catalog itself.
bool PrepareFork(Context& ctx, Remote& remote) {
    ctx.out << "Step 1 of 4: your fork of the catalog\n";
    std::optional<std::string> login = Gh({"api", "user", "--jq", ".login"});
    if (!login || login->empty()) return false;
    remote.login = *login;
    if (ToLowerAscii(remote.login) == ToLowerAscii(kProjectOwner)) {
        remote.fork = std::string(kCatalogRepository);
        return true;
    }
    remote.fork = remote.login + "/" + std::string(kCatalogRepository.substr(kProjectOwner.size() + 1));
    if (!Gh({"repo", "fork", std::string(kCatalogRepository), "--clone=false", "--remote=false"})) return false;
    if (!Gh({"repo", "sync", remote.fork, "--branch", std::string(kBaseBranch)})) {
        ctx.out << "Your fork could not be brought up to date; going on with it as it is.\n";
    }
    return true;
}

bool HasLine(const std::string& text, const std::string& wanted) {
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (TrimSpace(line) == wanted) return true;
    }
    return false;
}

// Creates the branch from the fork's main, unless an earlier try left it there.
bool PrepareBranch(Context& ctx, const Publication& publication, const Remote& remote) {
    ctx.out << "Step 2 of 4: branch " << publication.branch << "\n";
    const std::string repo = "repos/" + remote.fork;
    const std::string ref = "refs/heads/" + publication.branch;
    std::optional<std::string> refs = Gh({"api", repo + "/git/matching-refs/heads/" + publication.branch, "--jq", ".[].ref"});
    if (!refs) return false;
    if (HasLine(*refs, ref)) {
        ctx.out << "The branch exists from an earlier try: it is reused.\n";
        return true;
    }
    std::optional<std::string> base = Gh({"api", repo + "/git/ref/heads/" + std::string(kBaseBranch), "--jq", ".object.sha"});
    if (!base || base->empty()) return false;
    return GhSend("POST", repo + "/git/refs", "{\"ref\":" + Json::Quote(ref) + ",\"sha\":" + Json::Quote(*base) + "}");
}

// The files already under the target, on the branch: each one is replaced or removed with its blob sha.
bool ListExisting(const Publication& publication, Remote& remote) {
    std::optional<std::string> tree = Gh({"api", "repos/" + remote.fork + "/git/trees/" + publication.branch + "?recursive=1",
                                          "--jq", ".tree[] | [.sha, .path, .type] | @tsv"});
    if (!tree) return false;
    std::istringstream lines(*tree);
    for (std::string line; std::getline(lines, line);) {
        size_t first = line.find('\t');
        size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (second == std::string::npos || TrimSpace(line.substr(second + 1)) != "blob") continue;
        std::string path = line.substr(first + 1, second - first - 1);
        if (path.rfind(publication.target + "/", 0) == 0) remote.existing[path] = line.substr(0, first);
    }
    return true;
}

// One commit per file with the contents API: no clone, no git.
bool SendFiles(Context& ctx, const Publication& publication, const Remote& remote) {
    ctx.out << "Step 3 of 4: sending " << publication.files.size() << " file(s) to " << publication.target << "/\n";
    std::set<std::string> kept;
    for (const ZipSource& file : publication.files) {
        std::string path = publication.target + "/" + file.name;
        std::string bytes;
        if (!ReadWholeFile(file.file, bytes)) return false;
        auto old = remote.existing.find(path);
        std::string body = "{\"message\":" + Json::Quote((old == remote.existing.end() ? "Add " : "Update ") + path) +
                           ",\"content\":" + Json::Quote(Base64Encode(bytes)) +
                           ",\"branch\":" + Json::Quote(publication.branch) +
                           (old == remote.existing.end() ? "" : ",\"sha\":" + Json::Quote(old->second)) + "}";
        if (!GhSend("PUT", "repos/" + remote.fork + "/contents/" + EncodedPath(path), body)) return false;
        kept.insert(path);
        ctx.out << "  sent " << path << "\n";
    }
    for (const auto& [path, sha] : remote.existing) {
        if (kept.count(path)) continue;
        std::string body = "{\"message\":" + Json::Quote("Remove " + path) + ",\"sha\":" + Json::Quote(sha) +
                           ",\"branch\":" + Json::Quote(publication.branch) + "}";
        if (!GhSend("DELETE", "repos/" + remote.fork + "/contents/" + EncodedPath(path), body)) return false;
        ctx.out << "  removed " << path << " (not in this version)\n";
    }
    return true;
}

int OpenPullRequest(Context& ctx, const Publication& publication, const Remote& remote) {
    ctx.out << "Step 4 of 4: the pull request\n";
    bool update = !remote.existing.empty();
    const Manifest& manifest = publication.manifest;
    std::filesystem::path bodyFile = platform::TemporaryFile("ampify-pr");
    std::string error;
    if (!WriteTextFile(bodyFile, PullRequestBody(publication, update), error)) return Failure(ctx, error);

    std::string title = (update ? "Update " : "Add ") + manifest.name + " " + manifest.version;
    std::optional<std::string> address =
        Gh({"pr", "create", "--repo", std::string(kCatalogRepository), "--base", std::string(kBaseBranch), "--head",
            remote.login + ":" + publication.branch, "--title", title, "--body-file", Utf8(bodyFile)});
    std::error_code ec;
    std::filesystem::remove(bodyFile, ec);
    if (!address) return Stopped(ctx, "The pull request could not be opened.");

    ctx.out << "Pull request: " << *address << "\n"
            << "A maintainer reads the code. Once it is merged, a bot packs " << manifest.id << " " << manifest.version
            << " into the catalog and the Marketplace lists it.\n";
    return 0;
}

}  // namespace

int CmdPublish(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "publish needs the element folder");

    std::filesystem::path folder = Utf8ToPath(args[1]);
    FolderValidation validation = ValidateElementFolder(folder);
    if (validation.manifest) {
        for (std::string& left : StarterLeftovers(*validation.manifest)) validation.errors.push_back(std::move(left));
    }
    if (!validation.ok() || !validation.errors.empty() || !validation.manifest) {
        ctx.err << Utf8(folder) << ": invalid, nothing was sent\n";
        for (const std::string& problem : validation.errors) ctx.err << "  - " << problem << "\n";
        ctx.err << "Fix the points above, then run ampify publish again.\n";
        return 1;
    }
    // `test` reads the same positional arguments (command, folder), so it runs as is.
    if (validation.manifest->main && CmdTest(ctx) != 0) {
        ctx.err << "The test failed, nothing was sent. Fix what ampify test reports, then run ampify publish again.\n";
        return 1;
    }

    const Manifest& manifest = *validation.manifest;
    Publication publication{manifest, FilesUnder(folder, ""), "add-" + manifest.id + "-" + manifest.version,
                            "sources/" + manifest.id};
    bool installed = platform::ToolWorks({"gh", "--version"});
    if (ctx.options.dryRun) {
        if (installed) PrintPlan(ctx, publication);
        else PrintManualSteps(ctx, publication, "--dry-run: nothing was sent.\nThe GitHub CLI (gh) is not installed.");
        return 0;
    }
    if (!installed) {
        PrintManualSteps(ctx, publication, "The GitHub CLI (gh) is not installed.");
        return 0;
    }

    Remote remote;
    if (!PrepareFork(ctx, remote)) {
        if (remote.login.empty()) {
            PrintManualSteps(ctx, publication, "gh is installed but not signed in (run gh auth login), or GitHub cannot be reached.");
            return 0;
        }
        return Stopped(ctx, "Your fork of the catalog could not be prepared.");
    }
    if (!PrepareBranch(ctx, publication, remote) || !ListExisting(publication, remote)) return Stopped(ctx, "The branch could not be prepared.");
    if (!SendFiles(ctx, publication, remote)) return Stopped(ctx, "A file could not be sent.");
    return OpenPullRequest(ctx, publication, remote);
}

}  // namespace ampify::cli
