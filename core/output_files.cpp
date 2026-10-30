#include "output_files.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace amk {

namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

std::filesystem::path Utf8Path(std::string_view text) {
    std::u8string utf8;
    for (char c : text) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    return std::filesystem::path(utf8);
}

}  // namespace

bool IsValidOutputName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    if (!std::isalnum(static_cast<unsigned char>(name.front()))) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') return false;
    }
    if (name.find("..") != std::string_view::npos) return false;
    size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return false;
    std::string extension(name.substr(dot + 1));
    std::string stem(name.substr(0, name.find('.')));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return std::tolower(c); });
    if (extension != "txt" && extension != "json" && extension != "csv" && extension != "html" && extension != "xml") {
        return false;
    }
    // Windows gives these names to devices, whatever the extension.
    static const char* const kReserved[] = {"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                            "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                            "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* reserved : kReserved) {
        if (stem == reserved) return false;
    }
    return true;
}

std::filesystem::path OutputFolder(const std::filesystem::path& outputRoot, std::string_view elementId) {
    return outputRoot / Utf8Path(elementId);
}

bool WriteOutputFile(const std::filesystem::path& outputRoot, std::string_view elementId, std::string_view name,
                     std::string_view text, std::string* error) {
    if (!IsValidOutputName(name)) {
        return Fail(error, "nom de fichier refusé : « " + std::string(name) +
                               " » (lettres, chiffres, . _ -, et .txt, .json, .csv, .html ou .xml)");
    }
    if (text.size() > kMaxOutputFileBytes) return Fail(error, "texte trop long (256 Ko au plus)");

    std::error_code ec;
    std::filesystem::path folder = OutputFolder(outputRoot, elementId);
    std::filesystem::create_directories(folder, ec);
    if (ec) return Fail(error, "dossier de sortie impossible à créer");
    std::filesystem::path target = folder / Utf8Path(name);

    if (!std::filesystem::exists(target, ec)) {
        size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() != ".tmp") count++;
        }
        if (count >= kMaxOutputFilesPerElement) return Fail(error, "trop de fichiers de sortie (16 au plus)");
    }

    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return Fail(error, "écriture impossible dans le dossier de sortie");
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) return Fail(error, "écriture impossible dans le dossier de sortie");
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        // A reader can hold the file open on Windows. Removing first then renaming is the fallback.
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return Fail(error, "fichier de sortie occupé par un autre programme");
        }
    }
    return true;
}

}  // namespace amk
