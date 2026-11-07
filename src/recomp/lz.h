// A small LZ77 codec in LZ4's block format (sequences of literal run + match): fast to decode on the console,
// used for texture pixels that wait in host memory until a draw needs them (gl_vk.cpp).
#ifndef BOI360_RECOMP_LZ_H
#define BOI360_RECOMP_LZ_H
#include <stddef.h>
#include <stdint.h>
#include <vector>

void lz_compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out);
// false when the stream is malformed or does not fill exactly n bytes
bool lz_decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t n);

#endif
