// OpenAL for the recompiled game (cdecl, AL_APIENTRY). A software implementation of the subset the game imports:
// buffers, sources (gain, pitch, looping, position for panning), streaming queues, listener. A mixer thread runs
// at 44.1 kHz stereo in real time; its output goes to the platform's audio device, or nowhere when muted (test
// runs), so queued music still drains and the game's refill logic runs as on the PC.
#include "runtime.h"
#include "vfile.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <math.h>
#include <mutex>
#include <string.h>
#include <thread>
#include <vector>

void audio_out_open(int rate);                       // platform: start the device (no-op when muted)
void audio_out_submit(const int16_t* stereo, int frames);   // platform: blocks until the device takes it

namespace {
enum {
    AL_NONE = 0, AL_SOURCE_RELATIVE = 0x202, AL_PITCH = 0x1003, AL_POSITION = 0x1004, AL_VELOCITY = 0x1006,
    AL_LOOPING = 0x1007, AL_BUFFER = 0x1009, AL_GAIN = 0x100A, AL_ORIENTATION = 0x100F, AL_SOURCE_STATE = 0x1010,
    AL_INITIAL = 0x1011, AL_PLAYING = 0x1012, AL_PAUSED = 0x1013, AL_STOPPED = 0x1014, AL_BUFFERS_QUEUED = 0x1015,
    AL_BUFFERS_PROCESSED = 0x1016, AL_SEC_OFFSET = 0x1024, AL_SAMPLE_OFFSET = 0x1025, AL_SOURCE_TYPE = 0x1027,
    AL_FORMAT_MONO8 = 0x1100, AL_FORMAT_MONO16 = 0x1101, AL_FORMAT_STEREO8 = 0x1102, AL_FORMAT_STEREO16 = 0x1103,
    AL_REFERENCE_DISTANCE = 0x1020, AL_MAX_DISTANCE = 0x1023, AL_ROLLOFF_FACTOR = 0x1021,
};
const int kRate = 44100;

typedef std::shared_ptr<const std::vector<int16_t> > Samples;
struct Buffer {
    Samples pcm;                 // interleaved; shared with the effect cache (effect_samples)
    int channels, rate;
    bool waiting;                // a "B3RF" effect whose samples the loader has not read yet: its sources wait
    uint32_t effect;             // its offset in sfx.pcm
    Buffer() : channels(1), rate(44100), waiting(false), effect(0) {}
};
struct Source {
    std::deque<uint32_t> queue;  // buffer ids; the first is playing
    std::deque<uint32_t> done;   // played buffers, handed back by alSourceUnqueueBuffers in order
    double pos;                  // sample frames into the current buffer
    float gain, pitch;
    float x, y, z;
    bool looping, relative;
    int state;
    Source() : pos(0), gain(1), pitch(1), x(0), y(0), z(0), looping(false), relative(false), state(AL_INITIAL) {}
};
std::mutex g_lock;
std::map<uint32_t, Buffer> g_buffers;
std::map<uint32_t, Source> g_sources;
uint32_t g_nextId = 1;
float g_listener[3];
float g_listenerGain = 1.0f;
std::atomic<bool> g_running(false);

void mix(int16_t* out, int frames) {
    std::vector<float> acc((size_t)frames * 2, 0.0f);
    {
        std::lock_guard<std::mutex> l(g_lock);
        for (std::map<uint32_t, Source>::iterator it = g_sources.begin(); it != g_sources.end(); ++it) {
            Source& s = it->second;
            if (s.state != AL_PLAYING) continue;
            float pan = 0;
            if (!s.relative) pan = s.x - g_listener[0];
            else pan = s.x;
            pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
            float gl = s.gain * g_listenerGain * (pan > 0 ? 1 - pan : 1), gr = s.gain * g_listenerGain * (pan < 0 ? 1 + pan : 1);
            for (int i = 0; i < frames && s.state == AL_PLAYING; ++i) {
                if (s.queue.empty()) {
                    s.state = AL_STOPPED;
                    break;
                }
                Buffer& b = g_buffers[s.queue.front()];
                if (b.waiting) break;                   // starts when its samples arrive, a few ms late
                int n = b.channels && b.pcm ? (int)(b.pcm->size() / b.channels) : 0;
                if ((int)s.pos >= n) {
                    if (s.looping && s.queue.size() == 1 && n) {
                        s.pos -= n;
                    } else {
                        s.pos = 0;
                        if (s.queue.size() > 1 || !s.looping) {
                            s.done.push_back(s.queue.front());
                            s.queue.pop_front();
                            // a finished queue keeps its buffers listed as processed until unqueued
                            if (s.queue.empty()) {
                                s.state = AL_STOPPED;
                                break;
                            }
                        }
                    }
                    --i;
                    continue;
                }
                int at = (int)s.pos;
                const int16_t* d = &(*b.pcm)[0];
                float l, r;
                if (b.channels == 2) {
                    l = d[at * 2];
                    r = d[at * 2 + 1];
                } else {
                    l = r = d[at];
                }
                acc[i * 2] += l * gl;
                acc[i * 2 + 1] += r * gr;
                s.pos += s.pitch * (double)b.rate / kRate;
            }
        }
    }
    for (int i = 0; i < frames * 2; ++i) {
        float v = acc[i];
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

void mixer_thread(void*) {
    if (recomp_config().threadStarted) recomp_config().threadStarted("audio");
    std::vector<int16_t> block(1024 * 2);
    audio_out_open(kRate);
    auto next = std::chrono::steady_clock::now();
    while (g_running) {
        mix(&block[0], 1024);
        audio_out_submit(&block[0], 1024);
        next += std::chrono::microseconds(1024 * 1000000LL / kRate);
        std::this_thread::sleep_until(next);
    }
}

void al_gen(X86* c, bool sources) {
    uint32_t n = ARG(c, 0), out = ARG(c, 1);
    std::lock_guard<std::mutex> l(g_lock);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t id = g_nextId++;
        if (sources) g_sources[id] = Source();
        else g_buffers[id] = Buffer();
        WR32(out + 4 * i, id);
    }
    RET_CDECL(c, 0);
}
void a_alGenBuffers(X86* c) { al_gen(c, false); }
void a_alGenSources(X86* c) { al_gen(c, true); }
void a_alDeleteBuffers(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    for (uint32_t i = 0; i < ARG(c, 0); ++i) g_buffers.erase(RD32(ARG(c, 1) + 4 * i));
    RET_CDECL(c, 0);
}
void a_alDeleteSources(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    for (uint32_t i = 0; i < ARG(c, 0); ++i) g_sources.erase(RD32(ARG(c, 1) + 4 * i));
    RET_CDECL(c, 0);
}
// ---- sound effects read when first played
// The console's archives hold each effect's WAV with a "data" chunk that only says where its samples are:
// "B3RF", u32 offset, u32 bytes into resources/packed/sfx.pcm, the original 16-bit samples as the game ships them
// (tools/recomp/console_sfx.py). The game loads all 1,150 effects at start and keeps them; their samples (243 MB)
// stay on the drive until a sound plays. A loader thread reads them; the recently played stay in memory up to
// BOI360_SFX_CACHE MB (32). A source playing an effect still on its way waits for it rather than playing silence.
std::mutex g_fxLock;
std::condition_variable g_fxWake;
std::map<uint32_t, Samples> g_fx;                   // by offset in sfx.pcm
std::list<uint32_t> g_fxRecent;                     // most recently used first
uint64_t g_fxBytes = 0;
std::deque<std::pair<uint32_t, uint32_t> > g_fxQueue;   // (offset, bytes) to read
std::set<uint32_t> g_fxQueued;
uint64_t g_fxLimit = (uint64_t)(getenv("BOI360_SFX_CACHE") ? atoi(getenv("BOI360_SFX_CACHE")) : 32) << 20;
bool g_fxLoader = false;

void fx_loader(void*);

// the effect's samples if they are in memory; otherwise its read is queued and null comes back
Samples effect_samples(uint32_t offset, uint32_t bytes) {
    std::lock_guard<std::mutex> l(g_fxLock);
    std::map<uint32_t, Samples>::iterator it = g_fx.find(offset);
    if (it != g_fx.end()) {
        g_fxRecent.remove(offset);
        g_fxRecent.push_front(offset);
        return it->second;
    }
    if (g_fxQueued.insert(offset).second) g_fxQueue.push_back(std::make_pair(offset, bytes));
    if (!g_fxLoader) {
        g_fxLoader = true;
        host_thread_start(fx_loader, 0, 256u << 10);
    }
    g_fxWake.notify_one();
    return Samples();
}

void fx_loader(void*) {
    if (recomp_config().threadStarted) recomp_config().threadStarted("sound effects");
    VFile* file = vf_open(recomp_host_path("resources/packed/sfx.pcm"), "rb", true);
    if (!file) recomp_log("sfx: resources/packed/sfx.pcm is missing: converted effects stay silent");
    uint32_t loads = 0;
    double slowest = 0;
    std::vector<uint8_t> raw;
    for (;;) {
        std::pair<uint32_t, uint32_t> job;
        {
            std::unique_lock<std::mutex> l(g_fxLock);
            while (g_fxQueue.empty()) g_fxWake.wait(l);
            job = g_fxQueue.front();
            g_fxQueue.pop_front();
        }
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        std::shared_ptr<std::vector<int16_t> > pcm(new std::vector<int16_t>(job.second / 2));
        raw.resize(job.second);
        if (file && job.second && vf_seek(file, job.first, SEEK_SET) == 0 && vf_read(file, &raw[0], 1, job.second) == job.second)
            for (size_t i = 0; i < pcm->size(); ++i) (*pcm)[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > slowest) slowest = ms;
        Samples ready(pcm);
        {
            std::lock_guard<std::mutex> l(g_fxLock);
            g_fx[job.first] = ready;
            g_fxRecent.push_front(job.first);
            g_fxQueued.erase(job.first);
            g_fxBytes += pcm->size() * 2;
            // the least recently played go first; samples a buffer still holds stay until it lets them go
            for (std::list<uint32_t>::iterator it = g_fxRecent.end(); g_fxBytes > g_fxLimit && it != g_fxRecent.begin();) {
                --it;
                std::map<uint32_t, Samples>::iterator e = g_fx.find(*it);
                if (e != g_fx.end() && e->second.use_count() == 1 && *it != job.first) {
                    g_fxBytes -= e->second->size() * 2;
                    g_fx.erase(e);
                    it = g_fxRecent.erase(it);
                }
            }
            if (++loads % 200 == 0)
                recomp_log("sfx: %u effects read, %u MB in memory, slowest read %.1f ms", loads,
                           (unsigned)(g_fxBytes >> 20), slowest);
        }
        std::lock_guard<std::mutex> l(g_lock);
        for (std::map<uint32_t, Buffer>::iterator it = g_buffers.begin(); it != g_buffers.end(); ++it)
            if (it->second.waiting && it->second.effect == job.first) {
                it->second.pcm = ready;
                it->second.waiting = false;
            }
    }
}

// alBufferData(buffer, format, data, size, freq)
void a_alBufferData(X86* c) {
    uint32_t id = ARG(c, 0), fmt = ARG(c, 1), data = ARG(c, 2), size = ARG(c, 3), freq = ARG(c, 4);
    Buffer b;
    b.rate = (int)freq;
    b.channels = (fmt == AL_FORMAT_STEREO8 || fmt == AL_FORMAT_STEREO16) ? 2 : 1;
    bool sixteen = fmt == AL_FORMAT_MONO16 || fmt == AL_FORMAT_STEREO16;
    if (sixteen && size >= 12 && RD32(data) == 0x46523342u) {          // "B3RF"
        b.effect = RD32(data + 4);
        b.pcm = effect_samples(b.effect, RD32(data + 8));
        b.waiting = !b.pcm;
    } else {
        std::shared_ptr<std::vector<int16_t> > pcm(new std::vector<int16_t>(sixteen ? size / 2 : size));
        if (sixteen)
            for (uint32_t i = 0; i < size / 2; ++i) (*pcm)[i] = (int16_t)RD16(data + 2 * i);
        else
            for (uint32_t i = 0; i < size; ++i) (*pcm)[i] = (int16_t)(((int)RD8(data + i) - 128) << 8);
        b.pcm = pcm;
    }
    std::lock_guard<std::mutex> l(g_lock);
    if (b.waiting) {                                // the loader may have finished meanwhile
        std::lock_guard<std::mutex> f(g_fxLock);
        std::map<uint32_t, Samples>::iterator it = g_fx.find(b.effect);
        if (it != g_fx.end()) {
            b.pcm = it->second;
            b.waiting = false;
        }
    }
    g_buffers[id] = b;
    RET_CDECL(c, 0);
}
void a_alSourcei(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    uint32_t p = ARG(c, 1), v = ARG(c, 2);
    if (p == AL_BUFFER) {
        s.queue.clear();
        s.done.clear();
        s.pos = 0;
        if (v) s.queue.push_back(v);
    } else if (p == AL_LOOPING) s.looping = v != 0;
    else if (p == AL_SOURCE_RELATIVE) s.relative = v != 0;
    RET_CDECL(c, 0);
}
void a_alSourcef(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    uint32_t p = ARG(c, 1);
    float v = ARGF(c, 2);
    if (p == AL_GAIN) s.gain = v;
    else if (p == AL_PITCH) s.pitch = v > 0.01f ? v : 0.01f;
    else if (p == AL_SEC_OFFSET) s.pos = v * (s.queue.empty() ? kRate : g_buffers[s.queue.front()].rate);
    RET_CDECL(c, 0);
}
void a_alSource3f(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    if (ARG(c, 1) == AL_POSITION) {
        s.x = ARGF(c, 2);
        s.y = ARGF(c, 3);
        s.z = ARGF(c, 4);
    }
    RET_CDECL(c, 0);
}
void a_alGetSourcei(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    uint32_t p = ARG(c, 1), out = ARG(c, 2);
    uint32_t v = 0;
    if (p == AL_SOURCE_STATE) v = (uint32_t)s.state;
    else if (p == AL_BUFFERS_PROCESSED) v = (uint32_t)s.done.size();
    else if (p == AL_BUFFERS_QUEUED) v = (uint32_t)(s.queue.size() + s.done.size());
    else if (p == AL_SAMPLE_OFFSET) v = (uint32_t)s.pos;
    else if (p == AL_LOOPING) v = s.looping;
    WR32(out, v);
    RET_CDECL(c, 0);
}
void a_alGetSourcef(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    uint32_t p = ARG(c, 1), out = ARG(c, 2);
    float v = 0;
    if (p == AL_GAIN) v = s.gain;
    else if (p == AL_PITCH) v = s.pitch;
    else if (p == AL_SEC_OFFSET) v = (float)(s.pos / (s.queue.empty() ? kRate : g_buffers[s.queue.front()].rate));
    WRF(out, v);
    RET_CDECL(c, 0);
}
void a_alSourcePlay(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    if (s.state != AL_PAUSED) s.pos = 0;
    s.state = s.queue.empty() ? AL_STOPPED : AL_PLAYING;
    RET_CDECL(c, 0);
}
void a_alSourceStop(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    s.state = AL_STOPPED;
    while (!s.queue.empty()) {                  // a stopped source has played its whole queue
        s.done.push_back(s.queue.front());
        s.queue.pop_front();
    }
    s.pos = 0;
    RET_CDECL(c, 0);
}
void a_alSourcePause(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    if (s.state == AL_PLAYING) s.state = AL_PAUSED;
    RET_CDECL(c, 0);
}
// alSourceQueueBuffers(source, n, ids)
void a_alSourceQueueBuffers(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    for (uint32_t i = 0; i < ARG(c, 1); ++i) s.queue.push_back(RD32(ARG(c, 2) + 4 * i));
    RET_CDECL(c, 0);
}
// alSourceUnqueueBuffers(source, n, ids*): hands back processed buffers in order
void a_alSourceUnqueueBuffers(X86* c) {
    std::lock_guard<std::mutex> l(g_lock);
    Source& s = g_sources[ARG(c, 0)];
    uint32_t n = ARG(c, 1), out = ARG(c, 2);
    for (uint32_t i = 0; i < n && !s.done.empty(); ++i) {
        WR32(out + 4 * i, s.done.front());
        s.done.pop_front();
    }
    RET_CDECL(c, 0);
}
void a_alListener3f(X86* c) {
    if (ARG(c, 0) == AL_POSITION) {
        g_listener[0] = ARGF(c, 1);
        g_listener[1] = ARGF(c, 2);
        g_listener[2] = ARGF(c, 3);
    }
    RET_CDECL(c, 0);
}
void a_alListenerfv(X86* c) { RET_CDECL(c, 0); }
void a_alGetError(X86* c) { RET_CDECL(c, 0); }
void a_alcOpenDevice(X86* c) { RET_CDECL(c, 0x7000); }
void a_alcCloseDevice(X86* c) {
    g_running = false;
    RET_CDECL(c, 1);
}
void a_alcCreateContext(X86* c) {
    if (!g_running) {
        g_running = true;
        host_thread_start(mixer_thread, 0, 256u << 10);
    }
    RET_CDECL(c, 0x7004);
}
void a_alcDestroyContext(X86* c) { RET_CDECL(c, 0); }
void a_alcMakeContextCurrent(X86* c) { RET_CDECL(c, 1); }
void a_alcProcessContext(X86* c) { RET_CDECL(c, 0); }

const ShimDef kAl[] = {
    {"openal32.dll!alGenBuffers", a_alGenBuffers}, {"openal32.dll!alGenSources", a_alGenSources},
    {"openal32.dll!alDeleteBuffers", a_alDeleteBuffers}, {"openal32.dll!alDeleteSources", a_alDeleteSources},
    {"openal32.dll!alBufferData", a_alBufferData}, {"openal32.dll!alSourcei", a_alSourcei},
    {"openal32.dll!alSourcef", a_alSourcef}, {"openal32.dll!alSource3f", a_alSource3f},
    {"openal32.dll!alGetSourcei", a_alGetSourcei}, {"openal32.dll!alGetSourcef", a_alGetSourcef},
    {"openal32.dll!alSourcePlay", a_alSourcePlay}, {"openal32.dll!alSourceStop", a_alSourceStop},
    {"openal32.dll!alSourcePause", a_alSourcePause}, {"openal32.dll!alSourceQueueBuffers", a_alSourceQueueBuffers},
    {"openal32.dll!alSourceUnqueueBuffers", a_alSourceUnqueueBuffers}, {"openal32.dll!alListener3f", a_alListener3f},
    {"openal32.dll!alListenerfv", a_alListenerfv}, {"openal32.dll!alGetError", a_alGetError},
    {"openal32.dll!alcOpenDevice", a_alcOpenDevice}, {"openal32.dll!alcCloseDevice", a_alcCloseDevice},
    {"openal32.dll!alcCreateContext", a_alcCreateContext}, {"openal32.dll!alcDestroyContext", a_alcDestroyContext},
    {"openal32.dll!alcMakeContextCurrent", a_alcMakeContextCurrent}, {"openal32.dll!alcProcessContext", a_alcProcessContext},
};
}

void shims_al_register() { shim_register(kAl, (int)(sizeof(kAl) / sizeof(kAl[0]))); }
