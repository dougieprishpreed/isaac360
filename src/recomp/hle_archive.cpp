// The game's archive reader on the host (translate.py HOST). The game's files come from resources/packed/*.a
// (tools/recomp/archives.py's formats): a version 2 entry is a deflate stream cut into blocks of at most 1 KB of
// output, and some blocks are XORed with an ISAAC keystream seeded from the entry's hash. Each entry the game opens
// seeds a generator (F_9aee80: 256 words of a 64-bit LCG through PCG32's output step, one translated 64-bit multiply
// call each, then ISAAC's initialisation and a round); each block is inflated by the game's miniz tinfl into the
// stream's 1 KB window (F_99b210) or XORed (F_99ce00), and read out by the stream's Read (F_988670), a few bytes a
// call. A door's frame on the PC: ~150 entries opened, ~240 blocks, ~19,000 Reads, ~35,000 multiply calls; the run's
// and the menu's loads many times that. The same work runs here natively on the game's own objects:
//   F_9aee80 seed (this ecx, seed; ret 4)     the generator at this: randcnt, randrsl[256], randmem[256], aa, bb, cc
//   F_9aef40 isaac (this ecx)                 one round
//   F_99ce00 xor (this ecx, buf, len; ret 8)  this holds the generator: each 4 bytes take the next word, LSB first
//   F_99b210 tinfl_decompress (r ecx, in edx; in size*, out start, out next, out size*, flags; plain ret)
//   F_988670 Read (this ecx, dst, size, count; ret 12): from the 1 KB window at +0x81C (position +0xC1C, length
//            +0xC20, the end flag +0xC28, the stream's position +0x18), the next block by the game's F_988190
// tinfl's state stays on the host, keyed by the game's decompressor: the game's tinfl_init (its m_state, the first
// word, set to 0) starts a stream again, the host marks the word after each call, and the stream's end (tinfl's DONE)
// frees the host's state. ISAAC360_ARCHIVE_CHECK=1 runs the game's code as well (its results kept) and logs any
// difference from the host's.
#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "../third_party/miniz/miniz_tinfl.h"

namespace {
enum { RANDSIZ = 256, CTX_RSL = 4, CTX_MEM = 0x404, CTX_A = 0x804, CTX_B = 0x808, CTX_C = 0x80c, CTX_BYTES = 0x810 };
enum { WINDOW = 0x81c, WIN_POS = 0xc1c, WIN_LEN = 0xc20, AT_END = 0xc28, STREAM_POS = 0x18 };
const uint32_t kHostMark = 0x48535431u;                 // the decompressor's m_state while the host holds its stream
const uint32_t kDoneMark = 0x48535444u;                 // and once it has ended (the host's state freed)

int g_check = -1;
thread_local bool t_inCheck = false;                   // read only while checking (a call into emutls on the console)
uint64_t g_checked = 0, g_differ = 0;

bool checking() {
    if (g_check < 0) g_check = getenv("ISAAC360_ARCHIVE_CHECK") && atoi(getenv("ISAAC360_ARCHIVE_CHECK")) ? 1 : 0;
    return g_check != 0;
}

void differ(const char* what, uint32_t at) {
    if (++g_differ <= 12) recomp_log("archive check: %s differs (%08x)", what, at);
}

void checked() {
    if (++g_checked % 20000 == 0)
        recomp_log("archive check: %llu calls compared, %llu differ", (unsigned long long)g_checked,
                   (unsigned long long)g_differ);
}

// ---- ISAAC (Bob Jenkins' rand.c, 32-bit) on a generator in host words
struct Isaac {
    uint32_t cnt, rsl[RANDSIZ], mem[RANDSIZ], a, b, c;
    void load(uint32_t ctx) {
        cnt = RD32(ctx);
        for (int i = 0; i < RANDSIZ; ++i) {
            rsl[i] = RD32(ctx + CTX_RSL + 4 * i);
            mem[i] = RD32(ctx + CTX_MEM + 4 * i);
        }
        a = RD32(ctx + CTX_A);
        b = RD32(ctx + CTX_B);
        c = RD32(ctx + CTX_C);
    }
    void store(uint32_t ctx) const {
        WR32(ctx, cnt);
        for (int i = 0; i < RANDSIZ; ++i) {
            WR32(ctx + CTX_RSL + 4 * i, rsl[i]);
            WR32(ctx + CTX_MEM + 4 * i, mem[i]);
        }
        WR32(ctx + CTX_A, a);
        WR32(ctx + CTX_B, b);
        WR32(ctx + CTX_C, c);
    }
    void round() {
        ++c;
        b += c;
        for (int i = 0; i < RANDSIZ; ++i) {
            uint32_t x = mem[i];
            switch (i & 3) {
            case 0: a ^= a << 13; break;
            case 1: a ^= a >> 6; break;
            case 2: a ^= a << 2; break;
            default: a ^= a >> 16; break;
            }
            a = mem[(i + 128) & 255] + a;
            uint32_t y = mem[(x >> 2) & 255] + a + b;
            mem[i] = y;
            b = mem[(y >> 10) & 255] + x;
            rsl[i] = b;
        }
    }
    void init_from_rsl() {
        uint32_t h[8];
        h[0] = h[1] = h[2] = h[3] = h[4] = h[5] = h[6] = h[7] = 0x9e3779b9u;
        a = b = c = 0;
        for (int k = 0; k < 4; ++k) mix(h);
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < RANDSIZ; i += 8) {
                const uint32_t* from = pass ? mem : rsl;
                for (int j = 0; j < 8; ++j) h[j] += from[i + j];
                mix(h);
                for (int j = 0; j < 8; ++j) mem[i + j] = h[j];
            }
        round();
        cnt = 0;
    }
    static void mix(uint32_t* h) {
        h[0] ^= h[1] << 11; h[3] += h[0]; h[1] += h[2];
        h[1] ^= h[2] >> 2;  h[4] += h[1]; h[2] += h[3];
        h[2] ^= h[3] << 8;  h[5] += h[2]; h[3] += h[4];
        h[3] ^= h[4] >> 16; h[6] += h[3]; h[4] += h[5];
        h[4] ^= h[5] << 10; h[7] += h[4]; h[5] += h[6];
        h[5] ^= h[6] >> 4;  h[0] += h[5]; h[6] += h[7];
        h[6] ^= h[7] << 8;  h[1] += h[6]; h[7] += h[0];
        h[7] ^= h[0] >> 9;  h[2] += h[7]; h[0] += h[1];
    }
    // F_9aee80's seeding: PCG32 (XSH-RR output, multiplier 6364136223846793005, increment 127) from a state made of
    // the seed, then the initialisation (its round included) and the count at 0
    void seed(uint32_t s) {
        uint64_t state = (uint64_t)s << 32 | (uint32_t)((((s ^ (s << 15)) << 8) ^ (s >> 9)) ^ s);
        for (int i = 0; i < RANDSIZ; ++i) {
            uint64_t old = state;
            state = old * 6364136223846793005ull + 0x7f;
            uint32_t x = (uint32_t)(((old >> 18) ^ old) >> 27), rot = (uint32_t)(old >> 59);
            rsl[i] = (x >> rot) | (x << ((32 - rot) & 31));
        }
        init_from_rsl();
    }
};

bool same_ctx(uint32_t ctx, const Isaac& g) {
    Isaac game;
    game.load(ctx);
    return !memcmp(&game, &g, sizeof(g));
}

// ---- tinfl streams the host holds, by the game's decompressor
struct Stream {
    tinfl_decompressor d;
    uint8_t window[1024];                               // ISAAC360_ARCHIVE_CHECK: the host's own copy of the window
};
std::mutex g_streamsLock;                               // held through each call: the map and the states it holds
std::unordered_map<uint32_t, Stream*> g_streams;
uint32_t g_streamsMost = 0;                             // the most held at once (the frame lines)

// The guest heap leaves a freed block's bytes as they were, so a mark outlives its stream until the memory is used
// again, and a held state can't be told dead by its mark: boot 24's host heap grew 11 MB over a run. A stream's
// state goes at its end (the game reads its entries whole), and past 64 held, those whose word was written since
// (the memory reused) go too.
Stream* held(uint32_t r, bool make) {                   // with g_streamsLock
    std::unordered_map<uint32_t, Stream*>::iterator it = g_streams.find(r);
    if (it != g_streams.end()) return it->second;
    if (!make) return 0;
    if (g_streams.size() >= 64)
        for (it = g_streams.begin(); it != g_streams.end();) {
            if (RD32(it->first) != kHostMark) {
                delete it->second;
                it = g_streams.erase(it);
            } else {
                ++it;
            }
        }
    Stream* s = new Stream();
    g_streams[r] = s;
    if (g_streams.size() > g_streamsMost) g_streamsMost = (uint32_t)g_streams.size();
    if (g_streams.size() == 256) recomp_log("archive: 256 tinfl states held (~3 MB): streams left unfinished pile up");
    return s;
}

void drop(uint32_t r) {                                 // with g_streamsLock
    std::unordered_map<uint32_t, Stream*>::iterator it = g_streams.find(r);
    if (it == g_streams.end()) return;
    delete it->second;
    g_streams.erase(it);
}
}  // namespace

// F_9aee80: the generator at this seeded; eax this
extern "C" int hle_isaac_seed(X86* c) {
    if (checking() && t_inCheck) return 0;     // the game's own code, run by the check
    uint32_t ctx = c->r[1], esp = c->r[4], s = RD32(esp + 4);
    if (!ctx || ctx + CTX_BYTES > g_mem_size) return 0;
    Isaac g;
    g.seed(s);
    if (checking()) {
        t_inCheck = true;
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        uint32_t args[1] = {s};
        guest_call(c, 0x9aee80, 1, args, true);
        memcpy(c->r, regs, sizeof(regs));
        t_inCheck = false;
        if (!same_ctx(ctx, g)) differ("an ISAAC seeding", ctx);
        checked();
    } else {
        g.store(ctx);
    }
    c->r[0] = ctx;
    c->r[4] = esp + 8;
    return 1;
}

// F_9aef40: one round of the generator at this
extern "C" int hle_isaac_round(X86* c) {
    if (checking() && t_inCheck) return 0;     // the game's own code, run by the check
    uint32_t ctx = c->r[1], esp = c->r[4];
    if (!ctx || ctx + CTX_BYTES > g_mem_size) return 0;
    Isaac g;
    g.load(ctx);
    g.round();
    if (checking()) {
        t_inCheck = true;
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        guest_call(c, 0x9aef40, 0, 0, true);
        memcpy(c->r, regs, sizeof(regs));
        t_inCheck = false;
        if (!same_ctx(ctx, g)) differ("an ISAAC round", ctx);
        checked();
    } else {
        g.store(ctx);
    }
    c->r[4] = esp + 4;
    return 1;
}

// F_99ce00: buf's len bytes XORed with the generator this holds, a word each 4 bytes (a round when its words run out)
extern "C" int hle_isaac_xor(X86* c) {
    if (checking() && t_inCheck) return 0;     // the game's own code, run by the check
    uint32_t self = c->r[1], esp = c->r[4], buf = RD32(esp + 4), len = RD32(esp + 8);
    uint32_t ctx = self ? RD32(self) : 0;
    if (!ctx || ctx + CTX_BYTES > g_mem_size || buf > g_mem_size || len > g_mem_size - buf) return 0;
    Isaac g;
    g.load(ctx);
    bool check = checking();
    // in place, or into a copy to compare with the game's
    std::vector<uint8_t> copy;
    uint8_t* bytes = g_mem + buf;
    if (check && len) {
        copy.assign(g_mem + buf, g_mem + buf + len);
        bytes = &copy[0];
    }
    uint32_t w = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (!(i & 3)) {
            w = g.rsl[g.cnt < RANDSIZ ? g.cnt : 0];
            if (++g.cnt >= RANDSIZ) {
                g.round();
                g.cnt = 0;
            }
        } else {
            w >>= 8;
        }
        bytes[i] ^= (uint8_t)w;
    }
    if (check) {
        t_inCheck = true;
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        uint32_t args[2] = {buf, len};
        guest_call(c, 0x99ce00, 2, args, true);
        memcpy(c->r, regs, sizeof(regs));
        t_inCheck = false;
        if (len && memcmp(&copy[0], g_mem + buf, len)) differ("a keystream XOR", buf);
        if (!same_ctx(ctx, g)) differ("the generator after an XOR", ctx);
        checked();
    } else {
        g.store(ctx);
    }
    c->r[4] = esp + 12;
    return 1;
}

// F_99b210: tinfl_decompress(r ecx, in edx, in size*, out start, out next, out size*, flags), the window 1 KB
extern "C" int hle_tinfl(X86* c) {
    if (checking() && t_inCheck) return 0;     // the game's own code, run by the check
    uint32_t r = c->r[1], in = c->r[2], esp = c->r[4];
    uint32_t inSizeAt = RD32(esp + 4), outStart = RD32(esp + 8), outNext = RD32(esp + 12), outSizeAt = RD32(esp + 16);
    uint32_t flags = RD32(esp + 20);
    if (!r || !inSizeAt || !outSizeAt) return 0;
    uint32_t inSize = RD32(inSizeAt), outSize = RD32(outSizeAt);
    uint32_t span = outNext - outStart + outSize;
    // the game's use: a power-of-two window it wraps in, its input inside guest memory
    if ((flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) || span != 1024 || outNext < outStart ||
        outStart + span > g_mem_size || in > g_mem_size || inSize > g_mem_size - in) {
        if (RD32(r) == kHostMark) recomp_log("archive: a stream the host inflates asked for another window (%08x)", r);
        return 0;
    }
    uint32_t word = RD32(r);
    bool check = checking();
    if (word == kDoneMark && !check) {                  // after its end tinfl gives DONE, taking and giving nothing
        WR32(inSizeAt, 0);
        WR32(outSizeAt, 0);
        c->r[0] = (uint32_t)TINFL_STATUS_DONE;
        c->r[4] = esp + 4;
        return 1;
    }
    std::lock_guard<std::mutex> l(g_streamsLock);
    bool fresh = word == 0;
    Stream* found = held(r, fresh);
    if (!fresh && (check ? !found : word != kHostMark)) {
        // a state the game's code holds (begun before the host saw it), or one the check has no copy of
        if (found) drop(r);
        return 0;
    }
    if (!found) {
        static bool logged = false;
        if (!logged) recomp_log("archive: the host has no state for a stream it marked (%08x)", r);
        logged = true;
        WR32(inSizeAt, 0);
        WR32(outSizeAt, 0);
        c->r[0] = (uint32_t)TINFL_STATUS_FAILED;
        c->r[4] = esp + 4;
        return 1;
    }
    Stream& s = *found;
    if (fresh) tinfl_init(&s.d);
    size_t inBytes = inSize, outBytes = outSize;
    if (check) {
        if (fresh) memset(s.window, 0, sizeof(s.window));
        int mine = tinfl_decompress(&s.d, g_mem + in, &inBytes, s.window, s.window + (outNext - outStart), &outBytes,
                                    flags);
        t_inCheck = true;
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        uint32_t args[5] = {inSizeAt, outStart, outNext, outSizeAt, flags};
        int theirs = (int)guest_call(c, 0x99b210, 5, args, false);
        memcpy(c->r, regs, sizeof(regs));
        t_inCheck = false;
        if (mine != theirs || inBytes != RD32(inSizeAt) || outBytes != RD32(outSizeAt) ||
            memcmp(s.window + (outNext - outStart), g_mem + outNext, outBytes))
            differ("a block's inflate", r);
        checked();
        if (mine == TINFL_STATUS_DONE) drop(r);
        c->r[0] = (uint32_t)theirs;
        c->r[4] = esp + 4;
        return 1;
    }
    int st = tinfl_decompress(&s.d, g_mem + in, &inBytes, g_mem + outStart, g_mem + outNext, &outBytes, flags);
    WR32(inSizeAt, (uint32_t)inBytes);
    WR32(outSizeAt, (uint32_t)outBytes);
    if (st == TINFL_STATUS_DONE) {
        drop(r);
        WR32(r, kDoneMark);
    } else {
        WR32(r, kHostMark);
    }
    c->r[0] = (uint32_t)st;
    c->r[4] = esp + 4;
    return 1;
}

// the host's tinfl states: held now and the most at once (the frame lines)
void hle_archive_streams(uint32_t* now, uint32_t* most) {
    std::lock_guard<std::mutex> l(g_streamsLock);
    *now = (uint32_t)g_streams.size();
    *most = g_streamsMost;
}

// F_988670: Read(dst, size, count) from the stream's window, a block at a time through the game's F_988190; eax the
// bytes copied
extern "C" int hle_archive_read(X86* c) {
    uint32_t self = c->r[1], esp = c->r[4];
    if (!self || self + AT_END + 1 > g_mem_size) return 0;
    uint32_t dst = RD32(esp + 4), want = RD32(esp + 8) * RD32(esp + 12);
    if (dst > g_mem_size || want > g_mem_size - dst) return 0;
    uint32_t pos = RD32(self + WIN_POS), len = RD32(self + WIN_LEN);
    uint32_t n = pos < len ? len - pos : 0;
    if (n > want) n = want;
    if (pos + n > 1024) return 0;                       // not the window this reads (the game's code decides)
    memcpy(g_mem + dst, g_mem + self + WINDOW + pos, n);
    WR32(self + STREAM_POS, RD32(self + STREAM_POS) + n);
    WR32(self + WIN_POS, pos + n);
    uint32_t total = n;
    dst += n;
    want -= n;
    while (want && !RD8(self + AT_END)) {
        uint32_t regs[8];
        memcpy(regs, c->r, sizeof(regs));
        c->r[1] = self;
        guest_call(c, 0x988190, 0, 0, true);            // the next block (the file's read, the inflate or the XOR)
        memcpy(c->r, regs, sizeof(regs));
        if (RD8(self + AT_END)) break;
        len = RD32(self + WIN_LEN);
        n = len < want ? len : want;
        if (self + WINDOW + n > g_mem_size) break;
        memcpy(g_mem + dst, g_mem + self + WINDOW, n);
        WR32(self + STREAM_POS, RD32(self + STREAM_POS) + n);
        WR32(self + WIN_POS, RD32(self + WIN_POS) + n);
        total += n;
        dst += n;
        want -= n;
    }
    c->r[0] = total;
    c->r[4] = esp + 16;
    return 1;
}
