// LZ4 block format (lz.h): token (literal length << 4 | match length - 4), extra length bytes of 255, literals,
// 2-byte little-endian offset; the last sequence is literals only. Greedy matching through a 4096-entry hash of
// 4-byte groups, as LZ4's fast mode.
#include "lz.h"
#include <string.h>

namespace {

enum { HASH_BITS = 12, MIN_MATCH = 4, MAX_OFFSET = 65535, LAST_LITERALS = 5 };

uint32_t read32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

uint32_t hash4(uint32_t v) { return (v * 2654435761u) >> (32 - HASH_BITS); }

void put_length(std::vector<uint8_t>& out, size_t n) {
    while (n >= 255) {
        out.push_back(255);
        n -= 255;
    }
    out.push_back((uint8_t)n);
}

void put_sequence(std::vector<uint8_t>& out, const uint8_t* lit, size_t litLen, size_t offset, size_t matchLen) {
    size_t m = matchLen ? matchLen - MIN_MATCH : 0;
    out.push_back((uint8_t)((litLen >= 15 ? 15 : litLen) << 4 | (m >= 15 ? 15 : m)));
    if (litLen >= 15) put_length(out, litLen - 15);
    out.insert(out.end(), lit, lit + litLen);
    if (!matchLen) return;
    out.push_back((uint8_t)offset);
    out.push_back((uint8_t)(offset >> 8));
    if (m >= 15) put_length(out, m - 15);
}

}  // namespace

void lz_compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(n / 4 + 16);
    std::vector<uint32_t> table((size_t)1 << HASH_BITS, 0xffffffffu);
    size_t anchor = 0, i = 0;
    while (n >= MIN_MATCH + LAST_LITERALS && i + MIN_MATCH + LAST_LITERALS <= n) {
        uint32_t v = read32(src + i);
        uint32_t h = hash4(v);
        uint32_t cand = table[h];
        table[h] = (uint32_t)i;
        if (cand != 0xffffffffu && i - cand <= MAX_OFFSET && read32(src + cand) == v) {
            size_t len = MIN_MATCH;
            while (i + len + LAST_LITERALS < n && src[cand + len] == src[i + len]) ++len;
            put_sequence(out, src + anchor, i - anchor, i - cand, len);
            i += len;
            anchor = i;
            continue;
        }
        ++i;
    }
    put_sequence(out, src + anchor, n - anchor, 0, 0);
}

bool lz_decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t n) {
    const uint8_t* s = src;
    const uint8_t* end = src + srcLen;
    size_t o = 0;
    while (s < end) {
        uint8_t token = *s++;
        size_t lit = token >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (s >= end) return false;
                b = *s++;
                lit += b;
            } while (b == 255);
        }
        if ((size_t)(end - s) < lit || n - o < lit) return false;
        memcpy(dst + o, s, lit);
        s += lit;
        o += lit;
        if (s >= end) break;                                 // the last sequence: literals only
        if (end - s < 2) return false;
        size_t offset = (size_t)s[0] | (size_t)s[1] << 8;
        s += 2;
        size_t len = (token & 15) + MIN_MATCH;
        if ((token & 15) == 15) {
            uint8_t b;
            do {
                if (s >= end) return false;
                b = *s++;
                len += b;
            } while (b == 255);
        }
        if (!offset || offset > o || n - o < len) return false;
        const uint8_t* m = dst + o - offset;
        for (size_t k = 0; k < len; ++k) dst[o + k] = m[k];   // overlapping copies repeat the pattern
        o += len;
    }
    return o == n;
}
