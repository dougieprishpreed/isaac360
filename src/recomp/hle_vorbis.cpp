// The game's music on a host decoder (translate.py HOST, docs/RECOMP_PLAN.md). isaac-ng.exe holds stb_vorbis (an
// older version: its decoder object has no vendor or comment fields, and it reads through the game's own file
// objects, not stdio) and decodes each music stream on its sound thread inside
// stb_vorbis_get_samples_short_interleaved, holding the sound manager's lock, which the main thread takes every
// frame. Translated, that decode costs ~30% of a console core and holds the lock up to 120 ms; so would the game's
// reader, which unpacks its archives a 1 KB block at a time (~100 us a KB on the console).
//
// Here a host thread streams each music stream as a disc game would: it finds the archive entry the game's file
// object reads (resources/packed/music.a, afterbirth.a, afterbirthp.a, repentance.a: same size, same first 4 KB),
// reads it from the drive itself in 64 KB pieces, unpacks it (tools/recomp/archives.py's formats) and feeds a
// native stb_vorbis (src/third_party/stb, 1.22) through its push API, ~1.5 s of samples ahead; the game's
// get_samples copies what is ready. A few hundred KB a stream, nothing whole in memory, and nothing on the game's
// threads but the copy. A stream from anywhere else (the decoder's own memory, an entry not found) comes whole
// through the game's file object once and is fed from memory. The host takes a stream when the game opens it
// (vorbis_pump_first_frame, the end of an open) or at its first read; from then on the game's decoder stays as it
// was: seek_start's restart (set_file_offset, vorbis_pump_first_frame) restarts the host's instead. A stream the
// host can't take stays with the game's decoder. ISAAC360_VORBIS_CHECK=1 runs the game's decoder as well and logs
// how far the host's differs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#define STB_VORBIS_NO_STDIO
#include "../third_party/stb/stb_vorbis.c"
#include "../third_party/miniz/miniz_tinfl.h"

extern "C" void F_9aa630(X86* c);                       // the game's get_samples, for ISAAC360_VORBIS_CHECK

namespace {
// the game's decoder object: its input fields (stb_vorbis before the vendor and comment fields)
enum { G_FILE = 0x14, G_FILE_START = 0x18, G_STREAM_START = 0x24, G_STREAM_END = 0x28, G_STREAM_LEN = 0x2c };
const uint32_t kRingFrames = 1 << 16;                   // ~1.5 s at 44.1 kHz decoded ahead
const uint32_t kStepFrames = 4096;                      // one decode step
const uint32_t kMaxFrame = 8192;                        // the most one Vorbis packet gives (blocksize 8192: 4096)
const uint32_t kMaxStream = 32u << 20;                  // more is not a music file: the game's decoder keeps it
const size_t kMaxStreams = 16;                          // past this the least recently read goes (a safety net)
const size_t kFeed = 16u << 10;                         // bytes fed to the decoder at a time
const uint32_t kMatchBytes = 4096;                      // the first bytes compared to find a stream's entry

// ------------------------------------------------------------------------------------------------ archives
// An archive: "ARCH000", a version byte, u32 table offset, u16 entry count; the table: hash1, hash2, offset, size,
// checksum per entry (u32 each, little-endian). Version 0 entries are words XORed with a running seed; version 2
// entries are blocks (u32 header: final << 31 | bytes) of one deflate stream, or, from a non-final block of exactly
// 1024 bytes on, bytes XORed with a generator seeded by hash2 (the game's reader, F_988190, decides it per block).
struct ArchiveFile {
    std::string path;
    int version;
#if defined(_WIN32)
    FILE* f;
    std::mutex lock;
#else
    int fd;
#endif
    size_t read(int64_t off, void* buf, size_t n) {
#if defined(_WIN32)
        std::lock_guard<std::mutex> l(lock);
        if (_fseeki64(f, off, SEEK_SET) != 0) return 0;
        return fread(buf, 1, n, f);
#else
        ssize_t r = pread(fd, buf, n, (off_t)off);
        return r < 0 ? 0 : (size_t)r;
#endif
    }
};

struct Entry {
    uint32_t size;
    uint32_t archive;                                   // in g_archives
    uint32_t hash2, offset;
    bool operator<(const Entry& o) const { return size < o.size; }
};

std::mutex g_indexLock;                                 // build_index's, once
std::vector<ArchiveFile*> g_archives;
std::vector<Entry> g_entries;                           // by size
bool g_indexed = false;
const char* const kMusicArchives[] = {"music.a", "afterbirth.a", "afterbirthp.a", "repentance.a"};

void build_index() {
    g_indexed = true;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    for (size_t a = 0; a < sizeof(kMusicArchives) / sizeof(kMusicArchives[0]); ++a) {
        std::string path = recomp_host_path(std::string("resources/packed/") + kMusicArchives[a]);
        ArchiveFile* file = new ArchiveFile;
        file->path = path;
#if defined(_WIN32)
        file->f = fopen(path.c_str(), "rb");
        bool open = file->f != 0;
#else
        file->fd = open(path.c_str(), O_RDONLY);
        bool open = file->fd >= 0;
#endif
        uint8_t head[14];
        if (!open || file->read(0, head, sizeof(head)) != sizeof(head) || memcmp(head, "ARCH000", 7) != 0) {
            recomp_log("music: no archive %s", path.c_str());
            delete file;                                // (a handle left open: once, at start)
            continue;
        }
        file->version = head[7];
        uint32_t table = (uint32_t)head[8] | (uint32_t)head[9] << 8 | (uint32_t)head[10] << 16 | (uint32_t)head[11] << 24;
        uint32_t count = (uint32_t)head[12] | (uint32_t)head[13] << 8;
        std::vector<uint8_t> rows((size_t)count * 20);
        if (count && file->read(table, &rows[0], rows.size()) != rows.size()) {
            recomp_log("music: %s's table didn't read", path.c_str());
            delete file;
            continue;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* r = &rows[(size_t)i * 20];
            Entry e;
            e.hash2 = (uint32_t)r[4] | (uint32_t)r[5] << 8 | (uint32_t)r[6] << 16 | (uint32_t)r[7] << 24;
            e.offset = (uint32_t)r[8] | (uint32_t)r[9] << 8 | (uint32_t)r[10] << 16 | (uint32_t)r[11] << 24;
            e.size = (uint32_t)r[12] | (uint32_t)r[13] << 8 | (uint32_t)r[14] << 16 | (uint32_t)r[15] << 24;
            e.archive = (uint32_t)g_archives.size();
            g_entries.push_back(e);
        }
        g_archives.push_back(file);
    }
    std::stable_sort(g_entries.begin(), g_entries.end());
    recomp_log("music: %u archive entries indexed in %.0f ms", (unsigned)g_entries.size(),
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

// the generator of version 2's XORed blocks (tools/recomp/archives.py ArchiveRng)
struct ArchiveRng {
    uint32_t memory[256], results[256], a, b, c;
    int position;

    static void mix(uint32_t* v) {
        v[0] ^= v[1] << 11; v[3] += v[0]; v[1] += v[2];
        v[1] ^= v[2] >> 2;  v[4] += v[1]; v[2] += v[3];
        v[2] ^= v[3] << 8;  v[5] += v[2]; v[3] += v[4];
        v[3] ^= v[4] >> 16; v[6] += v[3]; v[4] += v[5];
        v[4] ^= v[5] << 10; v[7] += v[4]; v[5] += v[6];
        v[5] ^= v[6] >> 4;  v[0] += v[5]; v[6] += v[7];
        v[6] ^= v[7] << 8;  v[1] += v[6]; v[7] += v[0];
        v[7] ^= v[0] >> 9;  v[2] += v[7]; v[0] += v[1];
    }

    explicit ArchiveRng(uint32_t seed) {
        uint64_t state = (uint64_t)seed << 32 | (uint32_t)((seed << 8) ^ (seed >> 9) ^ seed ^ (seed << 23));
        uint32_t made[256];
        for (int i = 0; i < 256; ++i) {
            uint32_t word = (uint32_t)(((state >> 18) ^ state) >> 27);
            unsigned rotate = (unsigned)(state >> 59);
            made[i] = (word >> rotate) | (word << ((32 - rotate) & 31));
            state = state * 0x5851f42d4c957f2dull + 0x7f;
        }
        uint32_t v[8];
        for (int i = 0; i < 8; ++i) v[i] = 0x9e3779b9u;
        for (int i = 0; i < 4; ++i) mix(v);
        for (int i = 0; i < 256; i += 8) {
            for (int j = 0; j < 8; ++j) v[j] += made[i + j];
            mix(v);
            memcpy(&memory[i], v, sizeof(v));
        }
        for (int i = 0; i < 256; i += 8) {
            for (int j = 0; j < 8; ++j) v[j] += memory[i + j];
            mix(v);
            memcpy(&memory[i], v, sizeof(v));
        }
        a = b = c = 0;
        generate();
    }

    void generate() {
        ++c;
        uint32_t x, y, aa = a, bb = b + c;
        for (int i = 0; i < 256; ++i) {
            x = memory[i];
            switch (i & 3) {
            case 0: aa ^= aa << 13; break;
            case 1: aa ^= aa >> 6; break;
            case 2: aa ^= aa << 2; break;
            default: aa ^= aa >> 16; break;
            }
            aa += memory[(i + 128) & 255];
            memory[i] = y = memory[(x >> 2) & 255] + aa + bb;
            results[i] = bb = memory[(y >> 10) & 255] + x;
        }
        a = aa;
        b = bb;
        position = 0;
    }

    uint32_t next() {
        uint32_t v = results[position++];
        if (position == 256) generate();
        return v;
    }
};

// one archive entry's bytes from its start, in order (the decoder thread's)
struct EntryReader {
    Entry e;
    ArchiveFile* file;
    int64_t at;                                         // the next archive byte to read
    std::vector<uint8_t> raw;                           // archive bytes read ahead
    size_t rawPos, chunk;                               // the next drive read's size: 16 KB first, then 64 KB
    uint32_t done;                                      // entry bytes handed out
    bool failed;
    // version 0
    uint32_t seed;
    uint8_t word[4];                                    // an unscrambled word's bytes not handed out yet
    int wordLeft;
    // version 2
    uint32_t blockLeft;                                 // bytes of the current block not taken yet
    bool final, xorMode, inflateDone, moreOutput;
    std::unique_ptr<ArchiveRng> rng;
    tinfl_decompressor inf;
    std::vector<uint8_t> dict;                          // the inflater's window, its output
    size_t dictOfs, outPos, outEnd;                     // unread output: dict[outPos, outEnd)

    explicit EntryReader(const Entry& entry) : e(entry), file(g_archives[entry.archive]) { rewind(); }

    void rewind() {
        at = e.offset;
        raw.clear();
        rawPos = 0;
        chunk = 16u << 10;
        done = 0;
        failed = false;
        seed = (e.hash2 ^ 0xf9524287u) | 1;
        wordLeft = 0;
        blockLeft = 0;
        final = xorMode = inflateDone = moreOutput = false;
        rng.reset();
        tinfl_init(&inf);
        dict.assign(TINFL_LZ_DICT_SIZE, 0);
        dictOfs = outPos = outEnd = 0;
    }

    // at least n archive bytes after rawPos, if the archive has them
    bool fill(size_t n) {
        if (raw.size() - rawPos >= n) return true;
        raw.erase(raw.begin(), raw.begin() + (long)rawPos);
        rawPos = 0;
        size_t have = raw.size(), want = n > chunk ? n : chunk;
        chunk = 64u << 10;
        raw.resize(have + want);
        size_t got = file->read(at, &raw[have], want);
        at += (int64_t)got;
        raw.resize(have + got);
        return raw.size() >= n;
    }

    // up to n of the entry's next bytes into out
    size_t read(uint8_t* out, size_t n) {
        if (n > e.size - done) n = e.size - done;
        size_t put = file->version == 0 ? read_words(out, n) : file->version == 2 ? read_blocks(out, n) : 0;
        done += (uint32_t)put;
        return put;
    }

    size_t read_words(uint8_t* out, size_t n) {
        size_t put = 0;
        while (put < n) {
            if (wordLeft) {                             // the rest of a word a read ended in
                size_t take = n - put < (size_t)wordLeft ? n - put : (size_t)wordLeft;
                memcpy(out + put, word + 4 - wordLeft, take);
                wordLeft -= (int)take;
                put += take;
                continue;
            }
            if (!fill(4)) {
                failed = true;
                break;
            }
            const uint8_t* s = &raw[rawPos];
            uint32_t w = ((uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24) ^ seed;
            switch (seed & 15) {
            case 9: w = ((w & 0x00ff00ffu) << 8) | ((w & 0xff00ff00u) >> 8); break;
            case 13: w = (w << 16) | (w >> 16); break;
            case 2: w = (w >> 24) | ((w >> 8) & 0xff00u) | ((w << 8) & 0xff0000u) | (w << 24); break;
            }
            seed ^= seed << 8;
            seed ^= seed >> 9;
            seed ^= seed << 23;
            word[0] = (uint8_t)w;
            word[1] = (uint8_t)(w >> 8);
            word[2] = (uint8_t)(w >> 16);
            word[3] = (uint8_t)(w >> 24);
            wordLeft = 4;
            rawPos += 4;
        }
        return put;
    }

    // the next block's header; false at the entry's last block's end or a bad header
    bool next_block() {
        if (final || !fill(4)) return false;
        const uint8_t* s = &raw[rawPos];
        uint32_t h = (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24;
        rawPos += 4;
        final = (h >> 31) != 0;
        blockLeft = h & 0x7fffffffu;
        if (blockLeft >= 0x800) {
            failed = true;
            return false;
        }
        if (!xorMode && !final && blockLeft == 1024) {
            xorMode = true;
            rng.reset(new ArchiveRng(e.hash2));
        }
        return true;
    }

    size_t read_blocks(uint8_t* out, size_t n) {
        size_t put = 0;
        while (put < n && !failed) {
            if (outPos < outEnd) {                      // inflated bytes waiting
                size_t take = outEnd - outPos < n - put ? outEnd - outPos : n - put;
                memcpy(out + put, &dict[outPos], take);
                outPos += take;
                put += take;
                continue;
            }
            if (!blockLeft && !moreOutput) {
                if (!next_block()) break;
                continue;
            }
            if (xorMode) {                              // a whole XORed block (at most 1024 bytes) at once
                if (!fill(blockLeft)) {
                    failed = true;
                    break;
                }
                uint8_t* s = &raw[rawPos];
                for (uint32_t i = 0; i < blockLeft; i += 4) {
                    uint32_t key = rng->next();
                    for (uint32_t k = 0; k < 4 && i + k < blockLeft; ++k) s[i + k] ^= (uint8_t)(key >> (8 * k));
                }
                memcpy(&dict[0], s, blockLeft);         // handed out from dict like inflated bytes
                outPos = 0;
                outEnd = blockLeft;
                rawPos += blockLeft;
                blockLeft = 0;
                continue;
            }
            if (inflateDone) {                          // the deflate stream ended before the entry did
                failed = true;
                break;
            }
            // the block's input as far as it is read (none: the inflater has output left from earlier input)
            static const uint8_t none = 0;
            size_t inSize = 0;
            if (blockLeft) {
                if (!fill(1)) {
                    failed = true;
                    break;
                }
                inSize = raw.size() - rawPos < blockLeft ? raw.size() - rawPos : blockLeft;
            }
            bool last = final && inSize == blockLeft;
            size_t outSize = TINFL_LZ_DICT_SIZE - dictOfs;
            tinfl_status st = tinfl_decompress(&inf, inSize ? &raw[rawPos] : &none, &inSize, &dict[0], &dict[dictOfs],
                                               &outSize, last ? 0 : TINFL_FLAG_HAS_MORE_INPUT);
            rawPos += inSize;
            blockLeft -= (uint32_t)inSize;
            outPos = dictOfs;
            outEnd = dictOfs + outSize;
            dictOfs = (dictOfs + outSize) & (TINFL_LZ_DICT_SIZE - 1);
            moreOutput = st == TINFL_STATUS_HAS_MORE_OUTPUT;
            if (st < 0) failed = true;
            else if (st == TINFL_STATUS_DONE) inflateDone = true;
        }
        return put;
    }
};

// ------------------------------------------------------------------------------------------------ streams
struct Stream {
    uint32_t file, fileStart, memStart, memEnd;         // its source as the game's decoder has it (and who it is)
    int channels;                                       // as the game reads it
    bool native = false;                                // an archive entry the host reads, else data
    Entry entry;
    std::vector<uint8_t> data;                          // the Ogg stream whole (from memory or the game's reader)
    // the decoder thread's
    std::unique_ptr<EntryReader> reader;
    size_t dataPos = 0;
    std::vector<uint8_t> in;                            // fed, not yet decoded
    size_t inPos = 0;
    bool srcEnd = false;
    stb_vorbis* v = 0;
    // shared, with g_lock
    std::vector<int16_t> ring;                          // kRingFrames frames, interleaved
    uint32_t head = 0, count = 0;                       // the first frame held, and how many
    bool opened = false, failed = false, atEnd = false, restart = false, busy = false, gone = false;
    bool owned = false;                                 // the game's decoder was left unpumped for the host
    uint64_t served = 0, lastRead = 0;
};

std::mutex g_lock;
std::condition_variable g_work, g_ready;
std::map<uint32_t, std::shared_ptr<Stream> > g_streams;   // by the game's decoder object
std::vector<std::shared_ptr<Stream> > g_retired;          // closed by the decoder thread
bool g_threadStarted = false;
int g_check = -1;
uint32_t g_opened = 0;
uint64_t g_reads = 0;
thread_local bool t_bypass = false;
// The decoder objects the host serves, for the free() shim to look through without a lock: the game closes a
// streamed decoder with stb_vorbis_close inlined, whose last step frees the object, and no vorbis_deinit call
const int kWatch = 32;
std::atomic<uint32_t> g_watch[kWatch];
std::atomic<int> g_watching(0);

void watch(uint32_t f, bool on) {                       // with g_lock
    for (int i = 0; i < kWatch; ++i) {
        uint32_t want = on ? 0 : f, have = g_watch[i].load(std::memory_order_relaxed);
        if (have == want) {
            g_watch[i].store(on ? f : 0, std::memory_order_relaxed);
            g_watching += on ? 1 : -1;
            return;
        }
    }
}

void decoder_thread(void*);

void retire(std::map<uint32_t, std::shared_ptr<Stream> >::iterator it) {   // with g_lock
    watch(it->first, false);
    it->second->gone = true;
    g_retired.push_back(it->second);
    g_streams.erase(it);
    g_work.notify_one();
}

bool same_source(const Stream& s, uint32_t f) {
    return s.file == RD32(f + G_FILE) && s.fileStart == RD32(f + G_FILE_START) && s.memStart == RD32(f + G_STREAM_START) &&
           s.memEnd == RD32(f + G_STREAM_END);
}

// a thiscall on the game's file object, from the game's thread
uint32_t vcall(X86* c, uint32_t object, uint32_t slot, int nargs, const uint32_t* args) {
    c->r[1] = object;
    return guest_call(c, RD32(RD32(object) + slot), nargs, args, true);
}

// up to `length` bytes of the stream from fileStart through the game's file object (Read(ptr, size, count) at
// vtable +0x14, Seek at +0xc, Tell at +0x8), its position put back for the game's decoder
bool read_through_game(X86* c, const Stream& s, uint32_t length, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = guest_malloc(64u << 10);
    if (!buffer) return false;
    out.reserve(length);
    uint32_t was = vcall(c, s.file, 0x8, 0, 0);
    uint32_t seek[2] = {s.fileStart, 0};
    vcall(c, s.file, 0xc, 2, seek);
    while (out.size() < length) {
        uint32_t want = length - (uint32_t)out.size();
        if (want > (64u << 10)) want = 64u << 10;
        uint32_t args[3] = {buffer, 1, want};
        uint32_t n = vcall(c, s.file, 0x14, 3, args);
        if (!n || n > want) break;
        out.insert(out.end(), g_mem + buffer, g_mem + buffer + n);
    }
    uint32_t back[2] = {was, 0};
    vcall(c, s.file, 0xc, 2, back);
    guest_free(buffer);
    return !out.empty();
}

// where the stream's bytes come from: an archive entry of the same size whose first bytes are the stream's, or
// the stream whole (from the decoder's memory, or through the game's file object)
bool find_source(X86* c, Stream& s, uint32_t length) {
    if (s.memStart && s.memEnd > s.memStart && s.memEnd <= g_mem_size && s.memEnd - s.memStart <= kMaxStream) {
        s.data.assign(g_mem + s.memStart, g_mem + s.memEnd);
        return true;
    }
    if (!s.file || !length || length > kMaxStream) return false;
    {
        std::lock_guard<std::mutex> g(g_indexLock);
        if (!g_indexed) build_index();
    }
    Entry key;
    key.size = length + s.fileStart;
    std::pair<std::vector<Entry>::iterator, std::vector<Entry>::iterator> same =
        std::equal_range(g_entries.begin(), g_entries.end(), key);
    if (same.first != same.second && !s.fileStart) {
        std::vector<uint8_t> theirs, mine(kMatchBytes < length ? kMatchBytes : length);
        if (read_through_game(c, s, (uint32_t)mine.size(), theirs) && theirs.size() == mine.size())
            for (std::vector<Entry>::iterator it = same.first; it != same.second; ++it) {
                EntryReader r(*it);
                if (r.read(&mine[0], mine.size()) == mine.size() && mine == theirs) {
                    s.native = true;
                    s.entry = *it;
                    return true;
                }
            }
    }
    return read_through_game(c, s, length, s.data);
}

// the host's stream for the game's decoder object f, made if new (null: f stays with the game's decoder).
// Called with g_lock held in l; the stream's source is found without it (the game's file read can wait on game locks).
std::shared_ptr<Stream> stream_for(std::unique_lock<std::mutex>& l, X86* c, uint32_t f, int channels) {
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(f);
    if (it != g_streams.end()) {
        if (same_source(*it->second, f)) {
            if (it->second->failed) return std::shared_ptr<Stream>();
            if (it->second->channels != channels) {     // read with other channels than it was opened for
                it->second->channels = channels;
                it->second->ring.assign((size_t)kRingFrames * channels, 0);
                it->second->restart = true;
                it->second->head = it->second->count = 0;
                g_work.notify_one();
            }
            return it->second;
        }
        retire(it);                                     // the object was freed and made again for another stream
    }
    std::shared_ptr<Stream> s(new Stream);
    s->file = RD32(f + G_FILE);
    s->fileStart = RD32(f + G_FILE_START);
    s->memStart = RD32(f + G_STREAM_START);
    s->memEnd = RD32(f + G_STREAM_END);
    s->channels = channels;
    uint32_t length = RD32(f + G_STREAM_LEN);
    l.unlock();
    bool found = find_source(c, *s, length);
    l.lock();
    it = g_streams.find(f);                             // one thread opens a decoder object at a time, but check
    if (it != g_streams.end()) {
        if (same_source(*it->second, f)) return it->second->failed ? std::shared_ptr<Stream>() : it->second;
        retire(it);
    }
    while (g_streams.size() >= kMaxStreams) {           // the least recently read goes
        std::map<uint32_t, std::shared_ptr<Stream> >::iterator old = g_streams.begin();
        for (it = g_streams.begin(); it != g_streams.end(); ++it)
            if (it->second->lastRead < old->second->lastRead) old = it;
        retire(old);
    }
    if (!found) {
        s->failed = true;
        g_streams[f] = s;                               // remembered, so the game's decoder keeps it
        recomp_log("music: a stream the host can't read (file %08x, %u bytes): the game decodes it", s->file, length);
        return std::shared_ptr<Stream>();
    }
    static const bool tracing = getenv("ISAAC360_TRACE_MUSIC") != 0;
    if (++g_opened <= 12 || tracing)
        recomp_log("music: the host streams %u KB from %s (entry %08x, decoder %08x)",
                   (s->native ? s->entry.size : (uint32_t)s->data.size()) >> 10,
                   s->native ? g_archives[s->entry.archive]->path.c_str() : "memory (read whole)",
                   s->native ? s->entry.hash2 : 0, f);
    s->ring.resize((size_t)kRingFrames * channels);
    g_streams[f] = s;
    watch(f, true);
    if (!g_threadStarted) {
        g_threadStarted = true;
        host_thread_start(decoder_thread, 0, 512u << 10);
    }
    g_work.notify_one();
    return s;
}

// ------------------------------------------------------------------------------------------------ decoding
// up to n more bytes of the stream for the decoder; false at its end
bool feed(Stream& s, size_t n) {
    if (s.inPos) {
        s.in.erase(s.in.begin(), s.in.begin() + (long)s.inPos);
        s.inPos = 0;
    }
    size_t have = s.in.size();
    s.in.resize(have + n);
    size_t got = 0;
    if (s.native) {
        got = s.reader->read(&s.in[have], n);
    } else {
        got = s.data.size() - s.dataPos < n ? s.data.size() - s.dataPos : n;
        if (got) memcpy(&s.in[have], &s.data[s.dataPos], got);
        s.dataPos += got;
    }
    s.in.resize(have + got);
    if (!got) s.srcEnd = true;
    return got != 0;
}

// the decoder from the stream's start (an open or a restart); false if it can't open
bool start(Stream& s) {
    if (s.v) stb_vorbis_close(s.v);
    s.v = 0;
    if (s.native) {
        if (!s.reader) s.reader.reset(new EntryReader(s.entry));
        else s.reader->rewind();
    }
    s.dataPos = 0;
    s.in.clear();
    s.inPos = 0;
    s.srcEnd = false;
    for (;;) {
        int used = 0, err = 0;
        if (s.in.size() > s.inPos)
            s.v = stb_vorbis_open_pushdata(&s.in[s.inPos], (int)(s.in.size() - s.inPos), &used, &err, 0);
        else
            err = VORBIS_need_more_data;
        if (s.v) {
            s.inPos += (size_t)used;
            return true;
        }
        if (err != VORBIS_need_more_data || !feed(s, kFeed)) {
            recomp_log("music: the host decoder can't open a stream (error %d): the game decodes it", err);
            return false;
        }
    }
}

// up to `frames` frames into out (channels interleaved); fewer only at the stream's end
uint32_t decode(Stream& s, int16_t* out, uint32_t frames) {
    uint32_t got = 0;
    while (got + kMaxFrame <= frames) {
        int ch = 0, samples = 0;
        float** pcm = 0;
        int used = s.in.size() > s.inPos ? stb_vorbis_decode_frame_pushdata(s.v, &s.in[s.inPos], (int)(s.in.size() - s.inPos),
                                                                             &ch, &pcm, &samples)
                                         : 0;
        if (!used && !samples) {                        // a whole packet isn't in yet
            if (s.srcEnd || !feed(s, kFeed)) break;
            continue;
        }
        s.inPos += (size_t)used;
        if (samples > 0) {
            convert_channels_short_interleaved(s.channels, out + (size_t)got * s.channels, ch, pcm, 0, samples);
            got += (uint32_t)samples;
        }
        if (got >= kStepFrames) break;
    }
    return got;
}

void decoder_thread(void*) {
    if (recomp_config().threadStarted) recomp_config().threadStarted("music");
    std::vector<int16_t> step;
    std::unique_lock<std::mutex> l(g_lock);
    for (;;) {
        for (size_t i = 0; i < g_retired.size();) {
            if (g_retired[i]->busy) {
                ++i;
                continue;
            }
            if (g_retired[i]->v) stb_vorbis_close(g_retired[i]->v);
            g_retired[i]->v = 0;
            g_retired.erase(g_retired.begin() + (long)i);
        }
        std::shared_ptr<Stream> s;
        for (std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.begin(); it != g_streams.end(); ++it) {
            Stream& x = *it->second;
            if (x.failed) continue;
            if (!x.opened || x.restart || (!x.atEnd && kRingFrames - x.count >= kStepFrames + kMaxFrame)) {
                s = it->second;
                break;
            }
        }
        if (!s) {
            g_work.wait(l);
            continue;
        }
        s->busy = true;
        bool open = !s->opened, restart = s->restart;
        int channels = s->channels;
        s->restart = false;
        if (restart) {
            s->head = s->count = 0;
            s->atEnd = false;
        }
        l.unlock();
        bool ok = true;
        uint32_t got = 0;
        if (open || restart) ok = start(*s);
        if (ok && s->v) {
            step.resize((size_t)(kStepFrames + kMaxFrame) * channels);
            got = decode(*s, &step[0], kStepFrames + kMaxFrame);
        }
        l.lock();
        s->busy = false;
        if (open) s->opened = true;
        if (!ok) s->failed = true;
        if (s->v && !s->restart && !s->gone && s->channels == channels) {   // a restart asked for meanwhile drops this step
            if (!got) s->atEnd = true;
            for (uint32_t i = 0; i < got; ++i) {
                uint32_t at = (s->head + s->count + i) % kRingFrames;
                memcpy(&s->ring[(size_t)at * channels], &step[(size_t)i * channels], (size_t)channels * 2);
            }
            s->count += got;
        }
        g_ready.notify_all();
    }
}

// the host serves f: once it has, the game's decoder object stays where it was
std::shared_ptr<Stream> served(uint32_t f) {           // with g_lock
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(f);
    if (it == g_streams.end() || it->second->failed || !(it->second->served || it->second->owned) ||
        !same_source(*it->second, f))
        return std::shared_ptr<Stream>();
    return it->second;
}

// A stream the host took at its open and then couldn't decode before serving any of it goes back to the game's
// decoder, which gets the first frame it was spared (vorbis_pump_first_frame, 0x9a7910). True if f was such a stream.
bool give_back(std::unique_lock<std::mutex>& l, X86* c, uint32_t f, const uint32_t* regs) {
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(f);
    if (it == g_streams.end() || !it->second->failed || it->second->served) return false;
    bool pump = it->second->owned;
    it->second->owned = false;
    l.unlock();
    if (pump) {
        t_bypass = true;
        c->r[1] = f;
        guest_call(c, 0x9a7910, 0, 0, true);
        t_bypass = false;
        memcpy(c->r, regs, 8 * sizeof(uint32_t));
    }
    return true;
}

struct CheckStats {
    uint64_t reads, frames, differ;
    int maxDiff;
} g_checkStats;

}  // namespace

// stb_vorbis_get_samples_short_interleaved(f in ecx, channels in edx, short* buffer, int num_shorts): frames read
extern "C" int hle_vorbis_get_samples(X86* c) {
    if (t_bypass) return 0;
    // ISAAC360_VORBIS_DELAY_MS=N: each read waits N ms first, the console's slower sound thread on the PC
    static const int delayMs = getenv("ISAAC360_VORBIS_DELAY_MS") ? atoi(getenv("ISAAC360_VORBIS_DELAY_MS")) : 0;
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    uint32_t f = c->r[1], esp = c->r[4], buffer = RD32(esp + 4);
    int channels = (int)c->r[2], shorts = (int)RD32(esp + 8);
    if (!f || channels <= 0 || channels > 8 || shorts <= 0) return 0;
    if (g_check < 0) g_check = getenv("ISAAC360_VORBIS_CHECK") ? 1 : 0;
    uint32_t regs[8];                                   // a new stream may call the game's file object: the game's
    memcpy(regs, c->r, sizeof(regs));                   // get_samples, if it runs, gets its registers back
    std::unique_lock<std::mutex> l(g_lock);
    std::shared_ptr<Stream> s = stream_for(l, c, f, channels);
    memcpy(c->r, regs, sizeof(regs));
    if (!s) {
        give_back(l, c, f, regs);
        return 0;                                       // the game's decoder has it
    }
    s->lastRead = ++g_reads;
    uint32_t want = (uint32_t)(shorts / channels), got = 0;
    std::vector<int16_t> mine;
    while (got < want) {
        if (!s->opened || s->restart || (!s->count && !s->atEnd && !s->failed)) {
            g_work.notify_one();
            g_ready.wait(l);
            continue;
        }
        if (s->failed || !s->count) break;              // the host can't decode it, or the stream's end
        uint32_t k = want - got;
        if (k > s->count) k = s->count;
        if (k > kRingFrames - s->head) k = kRingFrames - s->head;
        const int16_t* src = &s->ring[(size_t)s->head * channels];
        if (g_check)
            mine.insert(mine.end(), src, src + (size_t)k * channels);
        else
            for (uint32_t i = 0; i < k * (uint32_t)channels; ++i) WR16(buffer + 2 * ((uint32_t)got * channels + i), (uint16_t)src[i]);
        s->head = (s->head + k) % kRingFrames;
        s->count -= k;
        got += k;
        g_work.notify_one();
    }
    if (s->failed && !s->served) {                      // the host can't decode it: the game's decoder has it
        give_back(l, c, f, regs);
        return 0;
    }
    s->served += got ? got : 1;
    l.unlock();
    if (g_check) {
        // the game's decoder on the same read: its samples go to the game, the host's are compared with them
        t_bypass = true;
        F_9aa630(c);
        t_bypass = false;
        uint32_t theirs = c->r[0];
        int maxDiff = 0;
        uint64_t differ = 0;
        for (uint32_t i = 0; i < theirs && i < got; ++i)
            for (int ch = 0; ch < channels; ++ch) {
                int a = (int16_t)RD16(buffer + 2 * (i * channels + ch)), b = mine[(size_t)i * channels + ch];
                int d = a > b ? a - b : b - a;
                if (d) ++differ;
                if (d > maxDiff) maxDiff = d;
            }
        std::lock_guard<std::mutex> g(g_lock);
        CheckStats& st = g_checkStats;
        ++st.reads;
        st.frames += theirs;
        st.differ += differ + (uint64_t)(theirs > got ? theirs - got : got - theirs) * channels;
        if (maxDiff > st.maxDiff) st.maxDiff = maxDiff;
        static int told = 0;
        if ((differ || theirs != got) && ++told <= 6) {
            uint32_t first = 0;
            while (first < theirs && first < got && (int16_t)RD16(buffer + 2 * first * channels) == mine[(size_t)first * channels]) ++first;
            recomp_log("music check: read %llu of a %s stream (%llu frames served): the game's %u frames, the host's %u, "
                       "first difference at frame %u", (unsigned long long)st.reads, s->native ? "native" : "memory",
                       (unsigned long long)s->served, theirs, got, first);
        }
        if (st.reads % 100 == 0)
            recomp_log("music check: %llu reads, %llu frames, %llu samples differ (largest by %d)", (unsigned long long)st.reads,
                       (unsigned long long)st.frames, (unsigned long long)st.differ, st.maxDiff);
        return 1;
    }
    c->r[0] = got;
    c->r[4] = esp + 4;
    return 1;
}

// set_file_offset(f in ecx, loc in edx): seek_start's first half (the game inlines the rest); the game's decoder
// of a stream the host serves stays where it was
extern "C" int hle_vorbis_set_file_offset(X86* c) {
    if (g_check > 0) return 0;                          // the game's decoder runs alongside
    std::lock_guard<std::mutex> l(g_lock);
    if (!served(c->r[1])) return 0;
    c->r[0] = 1;
    c->r[4] += 4;
    return 1;
}

// vorbis_pump_first_frame(f in ecx): the end of an open, and of stb_vorbis_seek_start. A stream the host serves
// restarts; a new one whose archive entry the host finds is taken now, so its first samples are ready by the
// game's first read, and the game's decoder skips its first frame (it never decodes for this stream)
extern "C" int hle_vorbis_pump_first_frame(X86* c) {
    if (t_bypass) return 0;
    if (g_check < 0) g_check = getenv("ISAAC360_VORBIS_CHECK") ? 1 : 0;
    uint32_t f = c->r[1];
    std::unique_lock<std::mutex> l(g_lock);
    std::shared_ptr<Stream> s = served(f);
    if (s) {
        s->restart = true;
        s->head = s->count = 0;
        s->atEnd = false;
        g_work.notify_one();
        if (g_check > 0) return 0;                      // the game's decoder restarts too, to stay comparable
        c->r[0] = 1;
        c->r[4] += 4;
        return 1;
    }
    if (g_check > 0 || g_streams.count(f)) return 0;
    uint32_t regs[8];
    memcpy(regs, c->r, sizeof(regs));
    s = stream_for(l, c, f, 2);                         // the game reads its music as stereo; a first read asking
    memcpy(c->r, regs, sizeof(regs));                   // otherwise restarts it with its channels
    if (!s || !s->native) return 0;                     // a stream read whole: the game's decoder stays ready too
    s->owned = true;
    c->r[0] = 1;
    c->r[4] += 4;
    return 1;
}

// the game frees guest memory at p (the free() shim): a decoder object the host serves takes its stream with it
void hle_vorbis_freed(uint32_t p) {
    if (!p || !g_watching.load(std::memory_order_relaxed)) return;
    int i = 0;
    while (i < kWatch && g_watch[i].load(std::memory_order_relaxed) != p) ++i;
    if (i == kWatch) return;
    std::lock_guard<std::mutex> l(g_lock);
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(p);
    if (it != g_streams.end()) retire(it);
}

// vorbis_deinit(f in ecx): the game lets its decoder go; the host's goes with it
extern "C" int hle_vorbis_deinit(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(c->r[1]);
    if (it != g_streams.end()) retire(it);
    return 0;
}
