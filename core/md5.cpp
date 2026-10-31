#include "md5.hpp"

#include <cstdint>
#include <cstring>

namespace ampify {

namespace {

constexpr uint32_t kShift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

// floor(abs(sin(i + 1)) * 2^32), from the RFC.
constexpr uint32_t kTable[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

uint32_t RotateLeft(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

void Block(uint32_t state[4], const unsigned char* block) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t f;
        uint32_t g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t next = d;
        d = c;
        c = b;
        b = b + RotateLeft(a + f + kTable[i] + m[g], kShift[i]);
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

}  // namespace

std::string Md5Hex(std::string_view data) {
    uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t whole = data.size() / 64;
    for (size_t i = 0; i < whole; i++) Block(state, reinterpret_cast<const unsigned char*>(data.data()) + i * 64);

    // Padding: 0x80, zeros, then the length in bits on 64 bits, little-endian.
    unsigned char tail[128] = {};
    size_t rest = data.size() - whole * 64;
    std::memcpy(tail, data.data() + whole * 64, rest);
    tail[rest] = 0x80;
    size_t tailSize = rest < 56 ? 64 : 128;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; i++) tail[tailSize - 8 + static_cast<size_t>(i)] = static_cast<unsigned char>(bits >> (8 * i));
    Block(state, tail);
    if (tailSize == 128) Block(state, tail + 64);

    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (uint32_t word : state) {
        for (int i = 0; i < 4; i++) {
            unsigned char byte = static_cast<unsigned char>(word >> (8 * i));
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        }
    }
    return out;
}

}  // namespace ampify
