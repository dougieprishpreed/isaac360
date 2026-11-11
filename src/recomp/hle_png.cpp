// libpng's row unfiltering on the host (translate.py HOST). isaac-ng.exe decodes its textures with libpng, and
// png_read_filter_row (0x9b2b60) undoes each row's filter a byte at a time, Paeth with its branches and Sub and Up
// through SSE: translated, over the 5-14 MB of pixels a room loads, a large part of the console's pause at a door.
// The filters are the PNG specification's, so the bytes are the same; BOI360_PNG_CHECK=1 runs the game's as well
// and logs any row that differs.
#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {
int g_check = -1;
bool g_inCheck = false;                                 // the check's own call to the game's function (main thread)
uint64_t g_rows = 0, g_differ = 0;

void unfilter(uint8_t* r, const uint8_t* p, uint32_t n, uint32_t bpp, uint32_t filter) {
    switch (filter) {
    case 1:                                             // Sub
        for (uint32_t i = bpp; i < n; ++i) r[i] = (uint8_t)(r[i] + r[i - bpp]);
        break;
    case 2:                                             // Up
        for (uint32_t i = 0; i < n; ++i) r[i] = (uint8_t)(r[i] + p[i]);
        break;
    case 3:                                             // Average
        for (uint32_t i = 0; i < bpp; ++i) r[i] = (uint8_t)(r[i] + (p[i] >> 1));
        for (uint32_t i = bpp; i < n; ++i) r[i] = (uint8_t)(r[i] + ((r[i - bpp] + p[i]) >> 1));
        break;
    case 4:                                             // Paeth
        // without branches: the console's in-order CPU paid a mispredicted branch on most bytes. Masks from sign
        // bits (an arithmetic shift of a negative int on both compilers) pick the predictor as the branches did.
        for (uint32_t i = 0; i < bpp; ++i) r[i] = (uint8_t)(r[i] + p[i]);
        for (uint32_t i = bpp; i < n; ++i) {
            int a = r[i - bpp], b = p[i], cc = p[i - bpp];
            int pa = b - cc, pb = a - cc, pc = pa + pb;
            int sa = pa >> 31, sb = pb >> 31, sc = pc >> 31;
            pa = (pa ^ sa) - sa;
            pb = (pb ^ sb) - sb;
            pc = (pc ^ sc) - sc;
            int useA = ~((pb - pa) >> 31) & ~((pc - pa) >> 31);   // pa <= pb && pa <= pc
            int useB = ~useA & ~((pc - pb) >> 31);                 // else pb <= pc
            int pred = (a & useA) | (b & useB) | (cc & ~(useA | useB));
            r[i] = (uint8_t)(r[i] + pred);
        }
        break;
    }
}
}  // namespace

// png_read_filter_row(png_ptr in ecx, row_info in edx, row, prev_row, filter): the caller pops the arguments.
// row_info: width (u32), rowbytes (u32), color_type, bit_depth, channels, pixel_depth (bytes).
extern "C" int hle_png_filter_row(X86* c) {
    if (g_inCheck) return 0;
    uint32_t esp = c->r[4], info = c->r[2];
    uint32_t row = RD32(esp + 4), prevRow = RD32(esp + 8), filter = RD32(esp + 12);
    if (filter > 4) return 0;                           // the game's own error
    uint32_t n = RD32(info + 4), bpp = ((uint32_t)RD8(info + 11) + 7) >> 3;
    if (!bpp || row >= g_mem_size || prevRow >= g_mem_size || n > g_mem_size - row || n > g_mem_size - prevRow)
        return 0;
    if (bpp > n) bpp = n;
    if (g_check < 0) g_check = getenv("BOI360_PNG_CHECK") ? 1 : 0;
    if (g_check) {
        // the host's on a copy, the game's on the row, compared
        std::vector<uint8_t> mine(g_mem + row, g_mem + row + n);
        unfilter(&mine[0], g_mem + prevRow, n, bpp, filter);
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        uint32_t args[3] = {row, prevRow, filter};
        g_inCheck = true;
        guest_call(c, 0x9b2b60, 3, args, false);
        g_inCheck = false;
        memcpy(c->r, regs, sizeof(regs));
        ++g_rows;
        if (memcmp(&mine[0], g_mem + row, n) != 0 && ++g_differ <= 8)
            recomp_log("png check: a row differs (filter %u, %u bytes, %u a pixel)", filter, n, bpp);
        if (g_rows % 20000 == 0)
            recomp_log("png check: %llu rows, %llu differ", (unsigned long long)g_rows, (unsigned long long)g_differ);
    } else {
        unfilter(g_mem + row, g_mem + prevRow, n, bpp, filter);
    }
    c->r[4] = esp + 4;                                  // the return address; the caller pops the rest
    return 1;
}
