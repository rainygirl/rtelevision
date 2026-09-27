#include "Aes128.h"

#include <cstdint>
#include <cstring>

namespace tv {
namespace {

const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

const uint8_t kRcon[11] = { 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

// Derived rather than written out a second time: a typo in a 256-entry table is
// invisible until some stream fails to decrypt.
struct InverseSbox {
    uint8_t value[256];
    InverseSbox() {
        for (int i = 0; i < 256; ++i) value[kSbox[i]] = static_cast<uint8_t>(i);
    }
};

const uint8_t* inverseSbox() {
    static const InverseSbox table;
    return table.value;
}

uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ ((x >> 7) * 0x1b)); }

uint8_t mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b != 0) {
        if (b & 1) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

// 11 round keys, 16 bytes each.
void expandKey(const uint8_t* key, uint8_t* roundKeys) {
    std::memcpy(roundKeys, key, 16);
    for (int word = 4; word < 44; ++word) {
        uint8_t t[4];
        std::memcpy(t, roundKeys + (word - 1) * 4, 4);
        if (word % 4 == 0) {
            const uint8_t first = t[0];
            t[0] = static_cast<uint8_t>(kSbox[t[1]] ^ kRcon[word / 4]);
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[first];
        }
        for (int i = 0; i < 4; ++i)
            roundKeys[word * 4 + i] = static_cast<uint8_t>(roundKeys[(word - 4) * 4 + i] ^ t[i]);
    }
}

// The state is column-major: byte(column, row) lives at column * 4 + row.
void addRoundKey(uint8_t* state, const uint8_t* roundKey) {
    for (int i = 0; i < 16; ++i) state[i] ^= roundKey[i];
}

void invSubBytes(uint8_t* state) {
    const uint8_t* rsbox = inverseSbox();
    for (int i = 0; i < 16; ++i) state[i] = rsbox[state[i]];
}

void invShiftRows(uint8_t* s) {
    uint8_t t = s[13];  // row 1 moves one column right
    s[13] = s[9];
    s[9] = s[5];
    s[5] = s[1];
    s[1] = t;

    t = s[2];           // row 2 moves two columns
    s[2] = s[10];
    s[10] = t;
    t = s[6];
    s[6] = s[14];
    s[14] = t;

    t = s[3];           // row 3 moves three columns right, i.e. one left
    s[3] = s[7];
    s[7] = s[11];
    s[11] = s[15];
    s[15] = t;
}

void invMixColumns(uint8_t* s) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* column = s + c * 4;
        const uint8_t a = column[0], b = column[1], d = column[2], e = column[3];
        column[0] = static_cast<uint8_t>(mul(a, 0x0e) ^ mul(b, 0x0b) ^ mul(d, 0x0d) ^ mul(e, 0x09));
        column[1] = static_cast<uint8_t>(mul(a, 0x09) ^ mul(b, 0x0e) ^ mul(d, 0x0b) ^ mul(e, 0x0d));
        column[2] = static_cast<uint8_t>(mul(a, 0x0d) ^ mul(b, 0x09) ^ mul(d, 0x0e) ^ mul(e, 0x0b));
        column[3] = static_cast<uint8_t>(mul(a, 0x0b) ^ mul(b, 0x0d) ^ mul(d, 0x09) ^ mul(e, 0x0e));
    }
}

void decryptBlock(uint8_t* state, const uint8_t* roundKeys) {
    addRoundKey(state, roundKeys + 10 * 16);
    for (int round = 9;; --round) {
        invShiftRows(state);
        invSubBytes(state);
        addRoundKey(state, roundKeys + round * 16);
        if (round == 0) break;
        invMixColumns(state);
    }
}

}  // namespace

bool aes128CbcDecrypt(const std::string& key, const std::string& iv, std::string& data) {
    if (key.size() != 16 || iv.size() != 16) return false;
    if (data.empty() || data.size() % 16 != 0) return false;

    uint8_t roundKeys[176];
    expandKey(reinterpret_cast<const uint8_t*>(key.data()), roundKeys);

    uint8_t previous[16];
    std::memcpy(previous, iv.data(), 16);
    for (size_t offset = 0; offset < data.size(); offset += 16) {
        uint8_t cipher[16];
        uint8_t block[16];
        std::memcpy(cipher, data.data() + offset, 16);
        std::memcpy(block, cipher, 16);
        decryptBlock(block, roundKeys);
        for (int i = 0; i < 16; ++i) block[i] ^= previous[i];
        std::memcpy(&data[offset], block, 16);
        std::memcpy(previous, cipher, 16);
    }

    const size_t padding = static_cast<uint8_t>(data[data.size() - 1]);
    if (padding == 0 || padding > 16 || padding > data.size()) return false;
    data.resize(data.size() - padding);
    return true;
}

}  // namespace tv
