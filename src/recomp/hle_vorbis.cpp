// The game's music on a host decoder (translate.py HOST, docs/RECOMP_PLAN.md). isaac-ng.exe holds stb_vorbis (an
// older version: its decoder object has no vendor or comment fields, and it reads through the game's own file
// objects, not stdio) and decodes each music stream on its sound thread inside
// stb_vorbis_get_samples_short_interleaved, holding the sound manager's lock, which the main thread takes every
// frame. Translated, that decode costs ~30% of a console core and holds the lock up to 120 ms.
//
// Here, on a stream's first read, the stream's Ogg data comes whole into host memory (through the game's file
// object, whose Read unpacks the archive entry, its position put back after; or from the decoder's own memory), and
// a native stb_vorbis (src/third_party/stb, 1.22) decodes it ahead on a host thread; the game's get_samples copies
// what is ready. Nothing on the host thread touches the game's objects. Once the host serves a stream the game's
// decoder stays as it was: seek_start's restart (set_file_offset, vorbis_pump_first_frame) restarts the host's
// instead. A stream the host can't take stays with the game's. BOI360_VORBIS_CHECK=1 runs the game's decoder as
// well and logs how far the host's differs.
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../third_party/stb/stb_vorbis.c"

extern "C" void F_9aa630(X86* c);                       // the game's get_samples, for BOI360_VORBIS_CHECK

namespace {
// the game's decoder object: its input fields (stb_vorbis before the vendor and comment fields)
enum { G_FILE = 0x14, G_FILE_START = 0x18, G_STREAM_START = 0x24, G_STREAM_END = 0x28, G_STREAM_LEN = 0x2c };
const uint32_t kRingFrames = 1 << 16;                   // ~1.5 s at 44.1 kHz decoded ahead
const uint32_t kStepFrames = 4096;                      // one decode step
const uint32_t kMaxStream = 32u << 20;                  // more is not a music file: the game's decoder keeps it
const size_t kMaxStreams = 16;                          // past this the least recently read goes (a safety net)

struct Stream {
    uint32_t file, fileStart, memStart, memEnd;         // its source as the game's decoder has it (and who it is)
    int channels;                                       // as the game reads it
    std::vector<uint8_t> data;                          // the whole Ogg stream
    stb_vorbis* v = 0;
    std::vector<int16_t> ring;                          // kRingFrames frames, interleaved
    uint32_t head = 0, count = 0;                       // the first frame held, and how many
    bool opened = false, failed = false, atEnd = false, restart = false, busy = false, gone = false;
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

void watch(uint32_t f, bool on) {                       // with g_lock
    for (int i = 0; i < kWatch; ++i) {
        uint32_t want = on ? 0 : f, have = g_watch[i].load(std::memory_order_relaxed);
        if (have == want) {
            g_watch[i].store(on ? f : 0, std::memory_order_relaxed);
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

// the stream's Ogg data, whole: through the game's file object (Read(ptr, size, count) at vtable +0x14, Seek at
// +0xc, Tell at +0x8), its position put back for the game's decoder, or from the decoder's memory
bool read_stream(X86* c, Stream& s, uint32_t length) {
    if (s.memStart && s.memEnd > s.memStart && s.memEnd <= g_mem_size && s.memEnd - s.memStart <= kMaxStream) {
        s.data.assign(g_mem + s.memStart, g_mem + s.memEnd);
        return true;
    }
    if (!s.file || !length || length > kMaxStream) return false;
    uint32_t buffer = guest_malloc(64u << 10);
    if (!buffer) return false;
    uint32_t was = vcall(c, s.file, 0x8, 0, 0);
    uint32_t seek[2] = {s.fileStart, 0};
    vcall(c, s.file, 0xc, 2, seek);
    s.data.clear();
    while (s.data.size() < length) {
        uint32_t want = length - (uint32_t)s.data.size();
        if (want > (64u << 10)) want = 64u << 10;
        uint32_t args[3] = {buffer, 1, want};
        uint32_t n = vcall(c, s.file, 0x14, 3, args);
        if (!n || n > want) break;
        s.data.insert(s.data.end(), g_mem + buffer, g_mem + buffer + n);
    }
    uint32_t back[2] = {was, 0};
    vcall(c, s.file, 0xc, 2, back);
    guest_free(buffer);
    return !s.data.empty();
}

// the host decoder for the game's decoder object f, made on its first read; null leaves f to the game's decoder.
// Called with g_lock held in l; the stream's data is read without it (the game's file read can wait on game locks).
std::shared_ptr<Stream> stream_for(std::unique_lock<std::mutex>& l, X86* c, uint32_t f, int channels) {
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(f);
    if (it != g_streams.end()) {
        if (same_source(*it->second, f) && it->second->channels == channels) return it->second;
        if (it->second->failed && same_source(*it->second, f)) return std::shared_ptr<Stream>();
        retire(it);                                     // the object was freed and made again for another stream
    }
    std::shared_ptr<Stream> s(new Stream);
    s->file = RD32(f + G_FILE);
    s->fileStart = RD32(f + G_FILE_START);
    s->memStart = RD32(f + G_STREAM_START);
    s->memEnd = RD32(f + G_STREAM_END);
    s->channels = channels;
    l.unlock();
    bool read = read_stream(c, *s, RD32(f + G_STREAM_LEN));
    l.lock();
    it = g_streams.find(f);                             // one thread reads a decoder object at a time, but check
    if (it != g_streams.end()) {
        if (same_source(*it->second, f) && it->second->channels == channels) return it->second->failed ? std::shared_ptr<Stream>() : it->second;
        retire(it);
    }
    while (g_streams.size() >= kMaxStreams) {           // the least recently read goes
        std::map<uint32_t, std::shared_ptr<Stream> >::iterator old = g_streams.begin();
        for (it = g_streams.begin(); it != g_streams.end(); ++it)
            if (it->second->lastRead < old->second->lastRead) old = it;
        retire(old);
    }
    if (!read) {
        s->failed = true;
        g_streams[f] = s;                               // remembered, so the game's decoder keeps it
        recomp_log("music: a stream the host can't read (file %08x, %u bytes): the game decodes it", s->file,
                   RD32(f + G_STREAM_LEN));
        return std::shared_ptr<Stream>();
    }
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
            g_retired.erase(g_retired.begin() + (long)i);
        }
        std::shared_ptr<Stream> s;
        for (std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.begin(); it != g_streams.end(); ++it) {
            Stream& x = *it->second;
            if (x.failed) continue;
            if (!x.opened || x.restart || (!x.atEnd && kRingFrames - x.count >= kStepFrames)) {
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
        s->restart = false;
        if (restart) {
            s->head = s->count = 0;
            s->atEnd = false;
        }
        l.unlock();
        int err = 0, got = 0;
        if (open) s->v = stb_vorbis_open_memory(&s->data[0], (int)s->data.size(), &err, 0);
        else if (restart && s->v) stb_vorbis_seek_start(s->v);
        if (s->v) {
            step.resize((size_t)kStepFrames * s->channels);
            got = stb_vorbis_get_samples_short_interleaved(s->v, s->channels, &step[0], (int)(kStepFrames * s->channels));
        }
        l.lock();
        s->busy = false;
        if (open) {
            s->opened = true;
            s->failed = !s->v;
            if (s->failed) recomp_log("music: the host decoder can't open a stream (error %d): the game decodes it", err);
            else if (++g_opened <= 8)
                recomp_log("music: the host decodes a stream of %u KB (%u Hz, %d channels)", (unsigned)(s->data.size() >> 10),
                           s->v->sample_rate, s->v->channels);
        }
        if (s->v && !s->restart && !s->gone) {          // a restart asked for meanwhile drops this step
            if (!got) s->atEnd = true;
            for (int i = 0; i < got; ++i) {
                uint32_t at = (s->head + s->count + (uint32_t)i) % kRingFrames;
                memcpy(&s->ring[(size_t)at * s->channels], &step[(size_t)i * s->channels], (size_t)s->channels * 2);
            }
            s->count += (uint32_t)got;
        }
        g_ready.notify_all();
    }
}

// the host serves f: once it has, the game's decoder object stays where it was
std::shared_ptr<Stream> served(uint32_t f) {           // with g_lock
    std::map<uint32_t, std::shared_ptr<Stream> >::iterator it = g_streams.find(f);
    if (it == g_streams.end() || it->second->failed || !it->second->served || !same_source(*it->second, f))
        return std::shared_ptr<Stream>();
    return it->second;
}

struct CheckStats {
    uint64_t reads, frames, differ;
    int maxDiff;
} g_checkStats;

}  // namespace

// stb_vorbis_get_samples_short_interleaved(f in ecx, channels in edx, short* buffer, int num_shorts): frames read
extern "C" int hle_vorbis_get_samples(X86* c) {
    if (t_bypass) return 0;
    uint32_t f = c->r[1], esp = c->r[4], buffer = RD32(esp + 4);
    int channels = (int)c->r[2], shorts = (int)RD32(esp + 8);
    if (!f || channels <= 0 || channels > 8 || shorts <= 0) return 0;
    if (g_check < 0) g_check = getenv("BOI360_VORBIS_CHECK") ? 1 : 0;
    uint32_t regs[8];                                   // a first read calls the game's file object: the game's
    memcpy(regs, c->r, sizeof(regs));                   // get_samples, if it runs, gets its registers back
    std::unique_lock<std::mutex> l(g_lock);
    std::shared_ptr<Stream> s = stream_for(l, c, f, channels);
    memcpy(c->r, regs, sizeof(regs));
    if (!s) return 0;
    s->lastRead = ++g_reads;
    uint32_t want = (uint32_t)(shorts / channels), got = 0;
    std::vector<int16_t> mine;
    while (got < want) {
        if (!s->opened || (!s->count && !s->atEnd && !s->failed)) {
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
    if (s->failed && !s->served) return 0;              // the game's decoder keeps it
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
            recomp_log("music check: read %llu of a %u KB stream (%llu frames served): the game's %u frames, the host's %u, "
                       "first difference at frame %u", (unsigned long long)st.reads, (unsigned)(s->data.size() >> 10),
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

// vorbis_pump_first_frame(f in ecx): the end of stb_vorbis_seek_start, and of an open
extern "C" int hle_vorbis_pump_first_frame(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    std::shared_ptr<Stream> s = served(c->r[1]);
    if (!s) return 0;
    s->restart = true;
    s->head = s->count = 0;
    s->atEnd = false;
    g_work.notify_one();
    if (g_check > 0) return 0;                          // the game's decoder restarts too, to stay comparable
    c->r[0] = 1;
    c->r[4] += 4;
    return 1;
}

// the game frees guest memory at p (the free() shim): a decoder object the host serves takes its stream with it
void hle_vorbis_freed(uint32_t p) {
    if (!p) return;
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
