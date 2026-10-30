#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace amk {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t RotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
public:
    void Update(const unsigned char* data, size_t size) {
        m_length += size;
        while (size > 0) {
            size_t take = std::min(size, m_block.size() - m_filled);
            std::memcpy(m_block.data() + m_filled, data, take);
            m_filled += take;
            data += take;
            size -= take;
            if (m_filled == m_block.size()) {
                Compress();
                m_filled = 0;
            }
        }
    }

    std::string Finish() {
        std::uint64_t bitLength = m_length * 8;
        const unsigned char one = 0x80;
        const unsigned char zero = 0x00;
        Update(&one, 1);
        while (m_filled != 56) Update(&zero, 1);
        unsigned char lengthBytes[8];
        for (int i = 0; i < 8; i++) lengthBytes[i] = static_cast<unsigned char>(bitLength >> (56 - 8 * i));
        Update(lengthBytes, 8);

        static constexpr char kHex[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (std::uint32_t word : m_state) {
            for (int shift = 28; shift >= 0; shift -= 4) hex += kHex[(word >> shift) & 0xF];
        }
        return hex;
    }

private:
    void Compress() {
        std::array<std::uint32_t, 64> schedule{};
        for (int i = 0; i < 16; i++) {
            schedule[i] = (static_cast<std::uint32_t>(m_block[4 * i]) << 24) |
                          (static_cast<std::uint32_t>(m_block[4 * i + 1]) << 16) |
                          (static_cast<std::uint32_t>(m_block[4 * i + 2]) << 8) |
                          static_cast<std::uint32_t>(m_block[4 * i + 3]);
        }
        for (int i = 16; i < 64; i++) {
            std::uint32_t s0 = RotateRight(schedule[i - 15], 7) ^ RotateRight(schedule[i - 15], 18) ^
                               (schedule[i - 15] >> 3);
            std::uint32_t s1 = RotateRight(schedule[i - 2], 17) ^ RotateRight(schedule[i - 2], 19) ^
                               (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }

        std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
        std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
        for (int i = 0; i < 64; i++) {
            std::uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            std::uint32_t choice = (e & f) ^ (~e & g);
            std::uint32_t temp1 = h + sum1 + choice + kRoundConstants[i] + schedule[i];
            std::uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<std::uint32_t, 8> m_state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<unsigned char, 64> m_block{};
    size_t m_filled = 0;
    std::uint64_t m_length = 0;
};

}  // namespace

std::string Sha256Hex(std::string_view data) {
    Sha256 hash;
    hash.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return hash.Finish();
}

std::optional<std::string> Sha256File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;

    Sha256 hash;
    std::vector<unsigned char> chunk(64 * 1024);
    while (in) {
        in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        std::streamsize got = in.gcount();
        if (got > 0) hash.Update(chunk.data(), static_cast<size_t>(got));
    }
    if (in.bad()) return std::nullopt;
    return hash.Finish();
}

bool IsSha256Hex(std::string_view text) {
    if (text.size() != 64) return false;
    for (char c : text) {
        bool digit = c >= '0' && c <= '9';
        bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower) return false;
    }
    return true;
}

}  // namespace amk
