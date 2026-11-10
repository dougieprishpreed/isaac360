// libpng's zlib inflate on the host (translate.py HOST). isaac-ng.exe links zlib 1.1.x for libpng, and its inflate
// (0x9af810: the stream in ecx, the flush folded in, plain ret) was a fifth of a door's frame on the PC, translated
// (BOI360_PROFILE=slow: F_9c32e0, F_9c2a50, F_9c23f0, F_9af290). Here miniz's tinfl decodes each stream into a 32 KB
// window and copies out as miniz's mz_inflate does; deflate's output is fixed by its input, so the bytes are the
// same. A stream starts again where its totals are both 0 (inflateInit and inflateReset zero them); inflateEnd
// (0x9af540) lets the host's state go before the game's own end runs. BOI360_INFLATE_CHECK=1 runs the game's inflate
// and feeds the host's the same input beside it, logging output that differs.
#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <map>
#include <mutex>
#include <vector>
#include "../third_party/miniz/miniz_tinfl.h"

namespace {
// z_stream (zlib 1.1): next_in, avail_in, total_in, next_out, avail_out, total_out, msg, state, ... adler at 0x30
enum { NEXT_IN = 0, AVAIL_IN = 4, TOTAL_IN = 8, NEXT_OUT = 0xc, AVAIL_OUT = 0x10, TOTAL_OUT = 0x14, MSG = 0x18,
       ADLER = 0x30 };
enum { Z_OK = 0, Z_STREAM_END = 1, Z_DATA_ERROR = -3, Z_BUF_ERROR = -5 };

struct Inflater {
    tinfl_decompressor d;
    uint8_t dict[TINFL_LZ_DICT_SIZE];
    uint32_t dictOfs, dictAvail;
    int last;                                           // tinfl's last status
    uint32_t held;                                      // input bytes tinfl took that the stream still shows (0, 1)
    std::vector<uint8_t> pending;                       // the check's host output not yet matched
    void start() {
        tinfl_init(&d);
        dictOfs = dictAvail = 0;
        last = TINFL_STATUS_NEEDS_MORE_INPUT;
        held = 0;
        pending.clear();
    }
    // whether more output can come without more input
    bool more_output() const { return dictAvail || (last != TINFL_STATUS_NEEDS_MORE_INPUT && last != TINFL_STATUS_DONE); }
};

std::mutex g_lock;                                      // the streams' map (libpng decodes on the main thread)
std::map<uint32_t, Inflater*> g_streams;
int g_check = -1;
bool g_inCheck = false;
uint64_t g_checkedBytes = 0, g_differ = 0;

Inflater* stream_of(uint32_t strm) {
    std::lock_guard<std::mutex> l(g_lock);
    Inflater*& s = g_streams[strm];
    if (!s) {
        s = new Inflater;
        s->start();
    }
    return s;
}

// the window's decoded bytes not yet taken, into out (as much as fits); returns how many
uint32_t take(Inflater& s, uint8_t* out, uint32_t room) {
    uint32_t n = s.dictAvail < room ? s.dictAvail : room;
    memcpy(out, s.dict + s.dictOfs, n);
    s.dictAvail -= n;
    s.dictOfs = (s.dictOfs + n) & (TINFL_LZ_DICT_SIZE - 1);
    return n;
}

// one inflate call (libpng never finishes): the window's waiting bytes out first, then decode while the output has
// room and tinfl can go on; Z_OK where anything moved, Z_BUF_ERROR where nothing could, as zlib
int inflate_host(Inflater& s, const uint8_t*& in, uint32_t& availIn, uint8_t*& out, uint32_t& availOut) {
    if (s.last < 0) return Z_DATA_ERROR;
    bool moved = false;
    for (;;) {
        if (s.dictAvail) {
            uint32_t n = take(s, out, availOut);
            out += n;
            availOut -= n;
            moved = moved || n;
        }
        if (s.dictAvail || !availOut || s.last == TINFL_STATUS_DONE) break;
        size_t inBytes = availIn, outBytes = TINFL_LZ_DICT_SIZE - s.dictOfs;
        s.last = tinfl_decompress(&s.d, in, &inBytes, s.dict, s.dict + s.dictOfs, &outBytes,
                                  TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32 | TINFL_FLAG_HAS_MORE_INPUT);
        in += inBytes;
        availIn -= (uint32_t)inBytes;
        s.dictAvail = (uint32_t)outBytes;
        moved = moved || inBytes || outBytes;
        if (s.last < 0) return Z_DATA_ERROR;
        if (!inBytes && !outBytes) break;
    }
    if (s.last == TINFL_STATUS_DONE && !s.dictAvail) return Z_STREAM_END;
    return moved ? Z_OK : Z_BUF_ERROR;
}

// BOI360_INFLATE_CHECK: the game's inflate runs; the host's decodes the input it took, and their output is compared
void check(X86* c, uint32_t strm) {
    uint32_t nextIn = RD32(strm + NEXT_IN), totalIn = RD32(strm + TOTAL_IN);
    uint32_t nextOut = RD32(strm + NEXT_OUT), totalOut = RD32(strm + TOTAL_OUT);
    Inflater& s = *stream_of(strm);
    if (!totalIn && !totalOut) s.start();
    uint32_t regs[8];
    memcpy(regs, c->r, sizeof(regs));
    g_inCheck = true;
    uint32_t ret = guest_call(c, 0x9af810, 0, 0, false);
    g_inCheck = false;
    memcpy(c->r, regs, sizeof(regs));
    c->r[0] = ret;
    uint32_t took = RD32(strm + TOTAL_IN) - totalIn, made = RD32(strm + TOTAL_OUT) - totalOut;
    const uint8_t* in = g_mem + nextIn;
    uint32_t availIn = took;
    for (int guard = 0; guard < 100000; ++guard) {
        uint8_t buf[4096];
        uint8_t* out = buf;
        uint32_t room = sizeof(buf);
        int r = inflate_host(s, in, availIn, out, room);
        s.pending.insert(s.pending.end(), buf, out);
        if (r != Z_OK || (!availIn && out == buf)) break;
    }
    uint32_t n = made < s.pending.size() ? made : (uint32_t)s.pending.size();
    if (n < made || memcmp(&s.pending[0], g_mem + nextOut, n) != 0) {
        if (++g_differ <= 8)
            recomp_log("inflate check: stream %08x differs (the game made %u bytes, the host %u; %u in)", strm, made,
                       (unsigned)s.pending.size(), took);
    }
    s.pending.erase(s.pending.begin(), s.pending.begin() + n);
    g_checkedBytes += made;
    static uint64_t logged = 0;
    if (g_checkedBytes - logged >= (64u << 20)) {
        logged = g_checkedBytes;
        recomp_log("inflate check: %llu MB compared, %llu calls differ", (unsigned long long)(g_checkedBytes >> 20),
                   (unsigned long long)g_differ);
    }
}
}  // namespace

// inflate(strm in ecx): the stream's fields in, the decode, the fields out; eax the zlib result
extern "C" int hle_inflate(X86* c) {
    if (g_inCheck) return 0;
    uint32_t strm = c->r[1], esp = c->r[4];
    if (!strm || !RD32(strm + 0x1c) || !RD32(strm + NEXT_IN)) return 0;     // the game's own Z_STREAM_ERROR
    if (g_check < 0) g_check = getenv("BOI360_INFLATE_CHECK") ? 1 : 0;
    if (g_check) {
        check(c, strm);
        c->r[4] = esp + 4;
        return 1;
    }
    uint32_t nextIn = RD32(strm + NEXT_IN), availIn = RD32(strm + AVAIL_IN), nextOut = RD32(strm + NEXT_OUT);
    uint32_t availOut = RD32(strm + AVAIL_OUT), totalIn = RD32(strm + TOTAL_IN), totalOut = RD32(strm + TOTAL_OUT);
    if (nextIn >= g_mem_size || availIn > g_mem_size - nextIn || nextOut >= g_mem_size || availOut > g_mem_size - nextOut)
        return 0;
    Inflater& s = *stream_of(strm);
    if (!totalIn && !totalOut) s.start();
    // tinfl takes input ahead of the output it gives (up to its 32 KB window), where zlib takes it as the output
    // needs it: libpng reads more data when the input runs out, and at an image's end there is none ("Not enough
    // image data"). While output can still come without input, the stream shows the last byte taken as not taken.
    const uint8_t* in = g_mem + nextIn;
    uint32_t skip = s.held < availIn ? s.held : availIn;
    in += skip;
    availIn -= skip;
    s.held = 0;
    uint8_t* out = g_mem + nextOut;
    int r = inflate_host(s, in, availIn, out, availOut);
    uint32_t took = (uint32_t)(in - (g_mem + nextIn)), made = (uint32_t)(out - (g_mem + nextOut));
    if (r == Z_OK && !availIn && took && s.more_output()) {
        s.held = 1;
        --took;
        availIn = 1;
    }
    WR32(strm + NEXT_IN, nextIn + took);
    WR32(strm + AVAIL_IN, availIn);
    WR32(strm + TOTAL_IN, totalIn + took);
    WR32(strm + NEXT_OUT, nextOut + made);
    WR32(strm + AVAIL_OUT, availOut);
    WR32(strm + TOTAL_OUT, totalOut + made);
    WR32(strm + ADLER, tinfl_get_adler32(&s.d));
    if (r == Z_DATA_ERROR) WR32(strm + MSG, 0);
    c->r[0] = (uint32_t)r;
    c->r[4] = esp + 4;
    return 1;
}

// inflateEnd(strm in ecx): the host's state goes; the game's own end frees its state as before
extern "C" int hle_inflate_end(X86* c) {
    uint32_t strm = c->r[1];
    std::lock_guard<std::mutex> l(g_lock);
    std::map<uint32_t, Inflater*>::iterator it = g_streams.find(strm);
    if (it != g_streams.end()) {
        delete it->second;
        g_streams.erase(it);
    }
    return 0;
}
