// sha256.hpp — SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), header-only. Signs the remote commands of the 4G
// uplink: the broker is a public one, so a command is only obeyed when it carries a signature made with a key that
// exists on the board and in the operator's browser, nowhere on the wire.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace des {

inline void sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int r) { return (x >> r) | (x << (32 - r)); };
    auto block = [&](const uint8_t *p) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };
    size_t off = 0;
    for (; off + 64 <= n; off += 64) block(data + off);
    uint8_t tail[128] = {0};
    const size_t rest = n - off;
    if (rest) memcpy(tail, data + off, rest);
    tail[rest] = 0x80;
    const size_t tn = rest < 56 ? 64 : 128;
    const uint64_t bits = uint64_t(n) * 8;
    for (int i = 0; i < 8; i++) tail[tn - 1 - size_t(i)] = uint8_t(bits >> (8 * i));
    block(tail);
    if (tn == 128) block(tail + 64);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = uint8_t(h[i] >> 24); out[4 * i + 1] = uint8_t(h[i] >> 16);
        out[4 * i + 2] = uint8_t(h[i] >> 8); out[4 * i + 3] = uint8_t(h[i]);
    }
}

inline std::string hex_of(const uint8_t *p, size_t n)
{
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}
inline std::string sha256_hex(const std::string &s)
{
    uint8_t o[32];
    sha256(reinterpret_cast<const uint8_t *>(s.data()), s.size(), o);
    return hex_of(o, 32);
}
inline std::string hmac_sha256_hex(const std::string &key, const std::string &msg)
{
    uint8_t k[64] = {0}, o[32];
    if (key.size() > 64) sha256(reinterpret_cast<const uint8_t *>(key.data()), key.size(), k);
    else memcpy(k, key.data(), key.size());
    std::string inner(64, '\0'), outer(64, '\0');
    for (int i = 0; i < 64; i++) { inner[size_t(i)] = char(k[i] ^ 0x36); outer[size_t(i)] = char(k[i] ^ 0x5c); }
    inner += msg;
    sha256(reinterpret_cast<const uint8_t *>(inner.data()), inner.size(), o);
    outer.append(reinterpret_cast<const char *>(o), 32);
    sha256(reinterpret_cast<const uint8_t *>(outer.data()), outer.size(), o);
    return hex_of(o, 32);
}
// compare without stopping at the first difference
inline bool same_text(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace des
