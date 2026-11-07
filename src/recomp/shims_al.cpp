// OpenAL for the recompiled game (cdecl, AL_APIENTRY). A software implementation of the subset the game imports:
// buffers, sources (gain, pitch, looping, position for panning), streaming queues, listener. A mixer thread runs
// at 44.1 kHz stereo in real time; its output goes to the platform's audio device, or nowhere when muted (test
// runs), so queued music still drains and the game's refill logic runs as on the PC.
#include "runtime.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
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

struct Buffer {
    std::vector<int16_t> data;   // interleaved
    int channels, rate;
    int scale;                   // the rate the game gave over the one stored ("B3AH" effects: 2)
    Buffer() : channels(1), rate(44100), scale(1) {}
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
                int n = b.channels ? (int)(b.data.size() / b.channels) : 0;
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
                float l, r;
                if (b.channels == 2) {
                    l = b.data[at * 2];
                    r = b.data[at * 2 + 1];
                } else {
                    l = r = b.data[at];
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
// The console's sound effects are IMA ADPCM behind a marker (tools/recomp/console_sfx.py): "B3AD", u32 frames,
// u32 channels, then per channel s16 first sample, u8 step index, u8 0 and 4-bit codes, low nibble first.
// "B3AH": the same at half the rate the game declares (effects of 32 kHz and up).
const int kImaSteps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
const int kImaIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

bool decode_ima(uint32_t data, uint32_t size, Buffer& b) {
    if (size < 12 || (RD32(data) != 0x44413342u && RD32(data) != 0x48413342u)) return false;   // "B3AD", "B3AH"
    uint32_t frames = RD32(data + 4), channels = RD32(data + 8);
    uint32_t perChannel = 4 + (frames > 1 ? (frames - 1 + 1) / 2 : 0);
    if (!frames || channels < 1 || channels > 2 || 12 + perChannel * channels > size) return false;
    b.channels = (int)channels;
    b.scale = RD32(data) == 0x48413342u ? 2 : 1;
    b.data.resize((size_t)frames * channels);
    for (uint32_t ch = 0; ch < channels; ++ch) {
        uint32_t s = data + 12 + ch * perChannel;
        int pred = (int16_t)RD16(s), index = RD8(s + 2);
        if (index > 88) index = 88;
        b.data[ch] = (int16_t)pred;
        for (uint32_t i = 0; i + 1 < frames; ++i) {
            uint8_t byte = RD8(s + 4 + (i >> 1));
            int code = (i & 1) ? byte >> 4 : byte & 15, step = kImaSteps[index], delta = step >> 3;
            if (code & 4) delta += step;
            if (code & 2) delta += step >> 1;
            if (code & 1) delta += step >> 2;
            pred += (code & 8) ? -delta : delta;
            pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
            index += kImaIndex[code & 7];
            index = index < 0 ? 0 : index > 88 ? 88 : index;
            b.data[(size_t)(i + 1) * channels + ch] = (int16_t)pred;
        }
    }
    return true;
}

// alBufferData(buffer, format, data, size, freq)
void a_alBufferData(X86* c) {
    uint32_t id = ARG(c, 0), fmt = ARG(c, 1), data = ARG(c, 2), size = ARG(c, 3), freq = ARG(c, 4);
    Buffer b;
    b.rate = (int)freq;
    b.channels = (fmt == AL_FORMAT_STEREO8 || fmt == AL_FORMAT_STEREO16) ? 2 : 1;
    if ((fmt == AL_FORMAT_MONO16 || fmt == AL_FORMAT_STEREO16) && decode_ima(data, size, b)) {
        b.rate = (int)freq / b.scale;
    } else if (fmt == AL_FORMAT_MONO16 || fmt == AL_FORMAT_STEREO16) {
        b.data.resize(size / 2);
        for (uint32_t i = 0; i < size / 2; ++i) b.data[i] = (int16_t)RD16(data + 2 * i);
    } else {
        b.data.resize(size);
        for (uint32_t i = 0; i < size; ++i) b.data[i] = (int16_t)(((int)RD8(data + i) - 128) << 8);
    }
    std::lock_guard<std::mutex> l(g_lock);
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
    else if (p == AL_SAMPLE_OFFSET)              // in frames at the rate the game gave
        v = (uint32_t)(s.pos * (s.queue.empty() ? 1 : g_buffers[s.queue.front()].scale));
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
