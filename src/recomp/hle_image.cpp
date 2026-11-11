// Decoded images kept across loads (translate.py HOST). isaac-ng.exe loads each PNG through an image's load-from-file
// (0x98cd10: open the archive entry, decode it with libpng in 0x98ced0, then the image's own finishing virtual), and
// the game frees and loads the same images again at every room and floor: 5-14 MB of pixels a door, decoded a byte
// at a time (inflate, unfilter), the most of a console door's pause. The first load of a file runs the game's code;
// at its pixel hand-off (0x99d030, which makes the GL texture and frees the buffer) the pixels and the image's
// fields go into a cache, packed (lz.h). A later load of the same file sets the same fields, fills a buffer from the
// game's own allocator and calls the hand-off and the finishing virtual as the game's code does, without opening,
// reading or decoding the file. Palette images (whose palette goes to another virtual) are left to the game.
// BOI360_IMAGE_CACHE=MB sets the cache's size (default 24; 0 turns it off). BOI360_IMAGE_CHECK=1 decodes every
// load as the game does and compares a cached file's pixels and fields with the fresh decode (logged when they differ).
#include "runtime.h"
#include "lz.h"
#include <ctype.h>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

uint32_t gl_frame_count();                          // shims_gl.cpp

namespace {
struct CachedImage {
    uint16_t w, h, pw, ph;                          // the image's +80h..+86h: size and padded size
    uint32_t bpp, format;                           // +88h: bytes a pixel; +18h: 1 RGB, 2 RGBA
    std::vector<uint8_t> packed;                    // pw * ph * bpp bytes, packed
    uint32_t lastUse;
};
std::mutex g_lock;
std::unordered_map<std::string, CachedImage> g_cache;
std::unordered_map<uint32_t, std::string> g_loading;    // image -> the key its load-from-file is decoding
int g_check = -1;
uint64_t g_checked = 0, g_differ = 0;
size_t g_bytes = 0;
bool g_replaying = false;
uint64_t g_hits = 0, g_misses = 0, g_hitBytes = 0;

size_t budget() {
    static const long mb = getenv("BOI360_IMAGE_CACHE") ? atol(getenv("BOI360_IMAGE_CACHE")) : 24;
    return mb > 0 ? (size_t)mb << 20 : 0;
}

// the path as the game's file lookups take it, with the settings the decoder reads (premultiplied alpha, its gamma
// table, texture scaling): a change of them decodes again
std::string key_of(uint32_t pathAddr) {
    std::string k = guest_str(pathAddr, 512);
    for (size_t i = 0; i < k.size(); ++i) k[i] = k[i] == '\\' ? '/' : (char)tolower((unsigned char)k[i]);
    if (k.empty()) return k;
    k += '|';
    k += (char)('0' + RD8(0xbc9afa));
    k += (char)('0' + RD8(0xba3b3f));
    k += (char)('0' + RD8(0xba3b40));
    return k;
}

// the least recently used go until the cache fits (the lock held)
void trim(size_t keep) {
    while (g_bytes > keep && !g_cache.empty()) {
        std::unordered_map<std::string, CachedImage>::iterator oldest = g_cache.begin();
        for (std::unordered_map<std::string, CachedImage>::iterator it = g_cache.begin(); it != g_cache.end(); ++it)
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        g_bytes -= oldest->second.packed.size();
        g_cache.erase(oldest);
    }
}
}  // namespace

// the image's load-from-file (thiscall, the path on the stack, ret 4): a cached file's image made without the file
extern "C" int hle_image_load(X86* c) {
    if (!budget()) return 0;
    uint32_t self = c->r[1], esp = c->r[4], path = RD32(esp + 4);
    std::string key = key_of(path);
    if (key.empty()) return 0;
    CachedImage img;
    {
        std::lock_guard<std::mutex> l(g_lock);
        std::unordered_map<std::string, CachedImage>::iterator it = g_cache.find(key);
        if (g_check < 0) g_check = getenv("BOI360_IMAGE_CHECK") ? 1 : 0;
        if (it == g_cache.end() || g_check) {
            g_loading[self] = key;
            ++g_misses;
            return 0;
        }
        it->second.lastUse = gl_frame_count();
        img = it->second;
    }
    uint32_t size = (uint32_t)img.pw * img.ph * img.bpp;
    // the decoder's buffer: its allocator (0x9863b0 on the pool at 0xbc3fc8, a 64-bit size)
    c->r[1] = 0xbc3fc8;
    uint32_t sizeArgs[2] = {size, 0};
    uint32_t buf = guest_call(c, 0x9863b0, 2, sizeArgs, true);
    if (!buf || buf >= g_mem_size || size > g_mem_size - buf ||
        !lz_decompress(&img.packed[0], img.packed.size(), g_mem + buf, size)) {
        c->r[1] = self;
        return 0;                                   // the game's code (an unfreed buffer at worst)
    }
    WR16(self + 0x80, img.w);
    WR16(self + 0x82, img.h);
    WR16(self + 0x84, img.pw);
    WR16(self + 0x86, img.ph);
    WR32(self + 0x88, img.bpp);
    WR32(self + 0x18, img.format);
    // the hand-off takes a pointer to the buffer's pointer: a slot below the stack
    c->r[4] = esp - 16;
    WR32(esp - 16, buf);
    uint32_t slotArg[1] = {esp - 16};
    c->r[1] = self;
    g_replaying = true;
    guest_call(c, 0x99d030, 1, slotArg, true);
    g_replaying = false;
    c->r[4] = esp;
    // then as 0x98cd10 goes on: the image's name (0x94fb90 on 0xba3bd0) in place of any before, its finishing virtual
    uint32_t old = RD32(self + 0x3c);
    if (old) {
        uint32_t oldArg[1] = {old};
        guest_call(c, 0x9cd2d5, 1, oldArg, false);
        WR32(self + 0x3c, 0);
    }
    c->r[1] = 0xba3bd0;
    uint32_t pathArg[1] = {path};
    WR32(self + 0x3c, guest_call(c, 0x94fb90, 1, pathArg, true));
    c->r[1] = self;
    uint32_t ok = guest_call(c, RD32(RD32(self) + 0x6c), 0, 0, true) & 0xff;
    {
        std::lock_guard<std::mutex> l(g_lock);
        ++g_hits;
        g_hitBytes += size;
    }
    c->r[0] = ok;
    c->r[4] = esp + 8;                              // the return address and the path
    return 1;
}

// the pixel hand-off (thiscall, a pointer to the buffer's pointer): from the decoder's call (return 0x98d5fb) during
// a load-from-file that missed, the pixels and fields go into the cache; the game's code always runs
extern "C" int hle_image_pixels(X86* c) {
    if (g_replaying || !budget()) return 0;
    uint32_t self = c->r[1], esp = c->r[4];
    if (RD32(esp) != 0x98d5fbu) return 0;
    std::string key;
    {
        std::lock_guard<std::mutex> l(g_lock);
        std::unordered_map<uint32_t, std::string>::iterator it = g_loading.find(self);
        if (it == g_loading.end()) return 0;
        key.swap(it->second);
        g_loading.erase(it);
    }
    CachedImage img;
    img.w = RD16(self + 0x80);
    img.h = RD16(self + 0x82);
    img.pw = RD16(self + 0x84);
    img.ph = RD16(self + 0x86);
    img.bpp = RD32(self + 0x88);
    img.format = RD32(self + 0x18);
    if ((img.format != 1 && img.format != 2) || img.bpp < 3 || img.bpp > 4) return 0;
    uint32_t buf = RD32(RD32(esp + 4)), size = (uint32_t)img.pw * img.ph * img.bpp;
    if (!buf || buf >= g_mem_size || size > g_mem_size - buf || size > budget() * 4) return 0;
    lz_compress(g_mem + buf, size, img.packed);
    img.lastUse = gl_frame_count();
    std::lock_guard<std::mutex> l(g_lock);
    std::unordered_map<std::string, CachedImage>::iterator old = g_cache.find(key);
    if (g_check > 0 && old != g_cache.end()) {
        const CachedImage& o = old->second;
        bool same = o.w == img.w && o.h == img.h && o.pw == img.pw && o.ph == img.ph && o.bpp == img.bpp &&
                    o.format == img.format && o.packed == img.packed;
        ++g_checked;
        if (!same && ++g_differ <= 20)
            recomp_log("image check: %s differs from its cached copy (%ux%u, %u a pixel)", key.c_str(), img.w, img.h,
                       img.bpp);
        if (g_checked % 200 == 0)
            recomp_log("image check: %llu loads compared, %llu differ", (unsigned long long)g_checked,
                       (unsigned long long)g_differ);
    }
    if (old != g_cache.end()) {
        g_bytes -= old->second.packed.size();
        g_cache.erase(old);
    }
    g_bytes += img.packed.size();
    g_cache[key] = std::move(img);
    trim(budget());
    return 0;
}

// the room lines' counts, and the cache's share of host memory
void hle_image_stats(uint64_t* hits, uint64_t* misses, uint64_t* hitBytes, size_t* bytes) {
    std::lock_guard<std::mutex> l(g_lock);
    *hits = g_hits;
    *misses = g_misses;
    *hitBytes = g_hitBytes;
    *bytes = g_bytes;
}

// with the console short of memory the cache gives back half of what it holds
void hle_image_trim_low() {
    std::lock_guard<std::mutex> l(g_lock);
    trim(g_bytes / 2);
}
