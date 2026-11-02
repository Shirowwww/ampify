#include "form_data.hpp"

#include <array>
#include <cstdint>

namespace ampify {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The value of each Base64 character, -1 for the others.
constexpr std::array<int8_t, 256> MakeDecodeTable() {
    std::array<int8_t, 256> table{};
    for (int8_t& entry : table) entry = -1;
    for (int i = 0; i < 64; i++) table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int8_t>(i);
    return table;
}
constexpr std::array<int8_t, 256> kDecode = MakeDecodeTable();

}  // namespace

std::string Base64Encode(std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t chunk = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
        if (i + 1 < bytes.size()) chunk |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
        if (i + 2 < bytes.size()) chunk |= static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 63]);
        out.push_back(kAlphabet[(chunk >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kAlphabet[(chunk >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? kAlphabet[chunk & 63] : '=');
    }
    return out;
}

std::optional<std::string> Base64Decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    size_t padding = 0;
    while (padding < 2 && padding < text.size() && text[text.size() - 1 - padding] == '=') padding++;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    uint32_t chunk = 0;
    for (size_t i = 0; i < text.size() - padding; i++) {
        int8_t value = kDecode[static_cast<unsigned char>(text[i])];
        if (value < 0) return std::nullopt;
        chunk = (chunk << 6) | static_cast<uint32_t>(value);
        if (i % 4 == 3) {
            out.push_back(static_cast<char>((chunk >> 16) & 0xFF));
            out.push_back(static_cast<char>((chunk >> 8) & 0xFF));
            out.push_back(static_cast<char>(chunk & 0xFF));
            chunk = 0;
        }
    }
    // The last group: 2 characters give one byte, 3 give two.
    size_t rest = (text.size() - padding) % 4;
    if (rest == 1) return std::nullopt;
    if (rest == 2) out.push_back(static_cast<char>((chunk >> 4) & 0xFF));
    if (rest == 3) {
        out.push_back(static_cast<char>((chunk >> 10) & 0xFF));
        out.push_back(static_cast<char>((chunk >> 2) & 0xFF));
    }
    return out;
}

std::string MultipartBody(const std::vector<FormField>& fields, std::string_view boundary) {
    std::string body;
    for (const FormField& field : fields) {
        body += "--";
        body += boundary;
        body += "\r\nContent-Disposition: form-data; name=\"" + field.name + "\"";
        if (!field.filename.empty()) {
            body += "; filename=\"" + field.filename + "\"\r\nContent-Type: ";
            body += field.contentType.empty() ? "application/octet-stream" : field.contentType;
        }
        body += "\r\n\r\n";
        body += field.value;
        body += "\r\n";
    }
    body += "--";
    body += boundary;
    body += "--\r\n";
    return body;
}

}  // namespace ampify
