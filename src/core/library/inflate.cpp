// A canonical-Huffman DEFLATE decoder in the manner of zlib's puff.c: slow next to zlib, but small and easy to check,
// which suits a file of a few MB read once per change.

#include "inflate.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>

namespace ampify {

namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLiteralCodes = 286;
constexpr int kMaxDistanceCodes = 30;
constexpr int kFixedLiteralCodes = 288;

constexpr std::array<uint16_t, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistanceBase = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                                    33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistanceExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<uint8_t, 19> kCodeLengthOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Thrown inside the decoder only, and caught by InflateZlib: the stream is refused as a whole.
struct Refusal {
    const char* reason;
};

struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{};  // number of codes of each length
    std::array<uint16_t, kFixedLiteralCodes> symbol{};  // symbols ordered by code
};

// The canonical code for `lengths`. Returns 0 for a complete code, more for an incomplete one, less for an
// over-subscribed one.
int Build(Huffman& code, const uint8_t* lengths, int symbols) {
    code.count.fill(0);
    for (int s = 0; s < symbols; s++) code.count[lengths[s]]++;
    if (code.count[0] == symbols) return 0;
    int left = 1;
    for (int length = 1; length <= kMaxBits; length++) {
        left = (left << 1) - code.count[length];
        if (left < 0) return left;
    }
    std::array<uint16_t, kMaxBits + 1> offsets{};
    for (int length = 1; length < kMaxBits; length++) offsets[length + 1] = offsets[length] + code.count[length];
    for (int s = 0; s < symbols; s++) {
        if (lengths[s] != 0) code.symbol[offsets[lengths[s]]++] = static_cast<uint16_t>(s);
    }
    return left;
}

class Inflater {
public:
    Inflater(std::string_view input, size_t maxOutput, std::string& output)
        : m_in(input), m_max(maxOutput), m_out(output) {}

    void Run() {
        Header();
        bool last = false;
        while (!last) {
            last = Bits(1) == 1;
            switch (Bits(2)) {
                case 0: Stored(); break;
                case 1: Fixed(); break;
                case 2: Dynamic(); break;
                default: throw Refusal{"invalid block type"};
            }
        }
        Checksum();
    }

private:
    uint32_t Bits(int need) {
        uint64_t value = m_bitBuffer;
        while (m_bitCount < need) {
            if (m_pos >= m_in.size()) throw Refusal{"the stream is truncated"};
            value |= static_cast<uint64_t>(static_cast<uint8_t>(m_in[m_pos++])) << m_bitCount;
            m_bitCount += 8;
        }
        m_bitBuffer = value >> need;
        m_bitCount -= need;
        return static_cast<uint32_t>(value & ((uint64_t{1} << need) - 1));
    }

    void DropToByte() {
        m_bitBuffer = 0;
        m_bitCount = 0;
    }

    void Room(size_t bytes) {
        if (bytes > m_max - m_out.size()) throw Refusal{"the decoded data is larger than allowed"};
    }

    void Header() {
        if (m_in.size() < 6) throw Refusal{"the stream is truncated"};
        unsigned method = static_cast<uint8_t>(m_in[0]);
        unsigned flags = static_cast<uint8_t>(m_in[1]);
        if ((method & 0x0f) != 8 || (method >> 4) > 7) throw Refusal{"not a zlib stream"};
        if (((method << 8) | flags) % 31 != 0) throw Refusal{"bad zlib header check"};
        if (flags & 0x20) throw Refusal{"a preset dictionary is not supported"};
        m_pos = 2;
    }

    void Stored() {
        DropToByte();
        if (m_in.size() - m_pos < 4) throw Refusal{"the stream is truncated"};
        auto byte = [&](size_t at) { return static_cast<unsigned>(static_cast<uint8_t>(m_in[at])); };
        unsigned length = byte(m_pos) | (byte(m_pos + 1) << 8);
        unsigned check = byte(m_pos + 2) | (byte(m_pos + 3) << 8);
        if (length != (~check & 0xffffu)) throw Refusal{"damaged stored block"};
        m_pos += 4;
        if (m_in.size() - m_pos < length) throw Refusal{"the stream is truncated"};
        Room(length);
        m_out.append(m_in.substr(m_pos, length));
        m_pos += length;
    }

    int Decode(const Huffman& code) {
        int value = 0, first = 0, index = 0;
        for (int length = 1; length <= kMaxBits; length++) {
            value |= static_cast<int>(Bits(1));
            int count = code.count[length];
            if (value - count < first) return code.symbol[index + (value - first)];
            index += count;
            first = (first + count) << 1;
            value <<= 1;
        }
        throw Refusal{"invalid Huffman code"};
    }

    void Codes(const Huffman& literals, const Huffman& distances) {
        for (;;) {
            int symbol = Decode(literals);
            if (symbol < 256) {
                Room(1);
                m_out.push_back(static_cast<char>(symbol));
                continue;
            }
            if (symbol == 256) return;
            symbol -= 257;
            if (symbol >= static_cast<int>(kLengthBase.size())) throw Refusal{"invalid length code"};
            size_t length = kLengthBase[symbol] + Bits(kLengthExtra[symbol]);
            int d = Decode(distances);
            if (d >= kMaxDistanceCodes) throw Refusal{"invalid distance code"};
            size_t distance = kDistanceBase[d] + Bits(kDistanceExtra[d]);
            if (distance > m_out.size()) throw Refusal{"a distance points before the start"};
            Room(length);
            size_t from = m_out.size() - distance;
            for (size_t i = 0; i < length; i++) m_out.push_back(m_out[from + i]);
        }
    }

    void Fixed() {
        struct Tables {
            Huffman literals, distances;
        };
        static const Tables tables = [] {
            Tables built;
            std::array<uint8_t, kFixedLiteralCodes> lengths{};
            for (int s = 0; s < kFixedLiteralCodes; s++) lengths[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
            Build(built.literals, lengths.data(), kFixedLiteralCodes);
            lengths.fill(5);
            Build(built.distances, lengths.data(), kMaxDistanceCodes);
            return built;
        }();
        Codes(tables.literals, tables.distances);
    }

    // The code lengths of a dynamic block, themselves Huffman-coded (RFC 1951, 3.2.7).
    void ReadLengths(const Huffman& lengthCode, uint8_t* lengths, int total) {
        int index = 0;
        while (index < total) {
            int symbol = Decode(lengthCode);
            if (symbol < 16) {
                lengths[index++] = static_cast<uint8_t>(symbol);
                continue;
            }
            uint8_t repeated = 0;
            uint32_t times = 0;
            if (symbol == 16) {
                if (index == 0) throw Refusal{"a repeat with no previous length"};
                repeated = lengths[index - 1];
                times = 3 + Bits(2);
            } else {
                times = symbol == 17 ? 3 + Bits(3) : 11 + Bits(7);
            }
            if (index + static_cast<int>(times) > total) throw Refusal{"too many code lengths"};
            while (times-- > 0) lengths[index++] = repeated;
        }
    }

    void Dynamic() {
        int literalCount = static_cast<int>(Bits(5)) + 257;
        int distanceCount = static_cast<int>(Bits(5)) + 1;
        int lengthCount = static_cast<int>(Bits(4)) + 4;
        if (literalCount > kMaxLiteralCodes || distanceCount > kMaxDistanceCodes) throw Refusal{"bad code counts"};
        std::array<uint8_t, kMaxLiteralCodes + kMaxDistanceCodes> lengths{};
        for (int i = 0; i < lengthCount; i++) lengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(Bits(3));
        Huffman lengthCode;
        if (Build(lengthCode, lengths.data(), 19) != 0) throw Refusal{"bad code length code"};
        lengths.fill(0);
        ReadLengths(lengthCode, lengths.data(), literalCount + distanceCount);
        if (lengths[256] == 0) throw Refusal{"no end-of-block code"};
        // An incomplete code is only allowed when it has a single code (RFC 1951, 3.2.7).
        Huffman literals, distances;
        int left = Build(literals, lengths.data(), literalCount);
        if (left < 0 || (left > 0 && literalCount - literals.count[0] != 1)) throw Refusal{"bad literal code"};
        left = Build(distances, lengths.data() + literalCount, distanceCount);
        if (left < 0 || (left > 0 && distanceCount - distances.count[0] != 1)) throw Refusal{"bad distance code"};
        Codes(literals, distances);
    }

    void Checksum() {
        DropToByte();
        if (m_in.size() - m_pos < 4) throw Refusal{"the stream is truncated"};
        uint32_t expected = 0;
        for (int i = 0; i < 4; i++) expected = (expected << 8) | static_cast<uint8_t>(m_in[m_pos + i]);
        constexpr uint32_t kModulo = 65521;
        constexpr size_t kChunk = 5552;  // the most bytes before `b` can overflow 32 bits
        uint32_t a = 1, b = 0;
        for (size_t at = 0; at < m_out.size(); at += kChunk) {
            size_t end = std::min(m_out.size(), at + kChunk);
            for (size_t i = at; i < end; i++) {
                a += static_cast<uint8_t>(m_out[i]);
                b += a;
            }
            a %= kModulo;
            b %= kModulo;
        }
        if (((b << 16) | a) != expected) throw Refusal{"wrong checksum"};
    }

    std::string_view m_in;
    size_t m_max;
    std::string& m_out;
    size_t m_pos = 0;
    uint64_t m_bitBuffer = 0;
    int m_bitCount = 0;
};

}  // namespace

bool InflateZlib(std::string_view input, size_t maxOutput, std::string* output, std::string* error) {
    output->clear();
    try {
        Inflater(input, maxOutput, *output).Run();
        return true;
    } catch (const Refusal& refusal) {
        output->clear();
        if (error) *error = refusal.reason;
        return false;
    } catch (const std::bad_alloc&) {
        output->clear();
        if (error) *error = "not enough memory to decode the stream";
        return false;
    }
}

}  // namespace ampify
