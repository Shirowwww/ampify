#include "platform.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace amk::platform {

namespace {

// Each tool is looked up on PATH by its plain name. Both are part of Windows 10 1803 and later.
#ifdef _WIN32
constexpr const char* kZipLister[] = {"tar.exe", "-tf"};
constexpr const char* kZipExtractor[] = {"tar.exe", "-xf"};
constexpr const char* kCurl = "curl.exe";
#else
constexpr const char* kZipLister[] = {"unzip", "-Z1"};
constexpr const char* kZipExtractor[] = {"unzip", "-q", "-n"};
constexpr const char* kCurl = "curl";
#endif

// Reads a file of at most a few megabytes into memory, for the entry list of an archive.
std::vector<std::string> ReadLines(const std::filesystem::path& file) {
    std::vector<std::string> lines;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

}  // namespace

std::filesystem::path TemporaryFile(const std::string& stem) {
    static std::atomic<unsigned> counter{0};
    std::random_device device;
    unsigned long long token = (static_cast<unsigned long long>(device()) << 32) ^ device();
    std::string name = stem + "-" + std::to_string(token) + "-" + std::to_string(counter++) + ".tmp";
    return std::filesystem::temp_directory_path() / name;
}

bool ListZip(const std::filesystem::path& archive, std::vector<std::string>& entries, std::string& error) {
    std::filesystem::path listing = TemporaryFile("amk-list");
    std::vector<std::string> command = {kZipLister[0], kZipLister[1], archive.string()};
    int code = Run(command, listing);
    if (code != 0) {
        error = "lecture de l'archive impossible (outil " + std::string(kZipLister[0]) +
                ", code " + std::to_string(code) + ")";
        std::error_code ec;
        std::filesystem::remove(listing, ec);
        return false;
    }
    entries = ReadLines(listing);
    std::error_code ec;
    std::filesystem::remove(listing, ec);
    return true;
}

bool ExtractZip(const std::filesystem::path& archive, const std::filesystem::path& destination, std::string& error) {
    std::vector<std::string> command;
    for (const char* part : kZipExtractor) command.emplace_back(part);
#ifdef _WIN32
    command.push_back(archive.string());
    command.push_back("-C");
    command.push_back(destination.string());
#else
    command.push_back(archive.string());
    command.push_back("-d");
    command.push_back(destination.string());
#endif
    int code = Run(command);
    if (code != 0) {
        error = "extraction impossible (code " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool DownloadHttps(const std::string& url, const std::filesystem::path& destination, std::string& error) {
    std::vector<std::string> command = {kCurl,
                                        "--proto",
                                        "=https",
                                        "--proto-redir",
                                        "=https",
                                        "--fail",
                                        "--silent",
                                        "--show-error",
                                        "--location",
                                        "--output",
                                        destination.string(),
                                        url};
    int code = Run(command);
    if (code != 0) {
        error = "téléchargement impossible (code " + std::to_string(code) + ") : " + url;
        return false;
    }
    return true;
}

bool ToolWorks(const std::vector<std::string>& command) {
    std::filesystem::path scratch = TemporaryFile("tool");
    int code = Run(command, scratch);
    std::error_code ec;
    std::filesystem::remove(scratch, ec);
    return code == 0;
}

}  // namespace amk::platform
