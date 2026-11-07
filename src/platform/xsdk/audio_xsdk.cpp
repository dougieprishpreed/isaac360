// Console audio on xsdk. The shared mixer (NativeAudio) stays on the game thread at 44.1 kHz, as on Windows, and
// submits 1024-frame blocks into a ring; a feeder thread of our own fills the kernel's render frames (256 samples
// of 6 channels at 48 kHz, big-endian floats, a channel after another) from it, resampled. The feeder runs alone
// on hardware thread 4, where the kernel's audio callback runs (threads of equal priority sharing a hardware
// thread take 20 ms turns), 12 frames (64 ms) ahead.
#include "../../game/native_audio.h"
#include "../../app/port_app.h"
#include <xsdk/audio.h>
#include <xsdk/thread.h>
#include <pthread.h>
#include <string.h>

namespace {
const int kRing = 16384;                 // stereo frames, ~370 ms at 44.1 kHz
short gRing[kRing * 2];
volatile unsigned int gWrite = 0, gRead = 0;   // frames, wrapping; one producer, one consumer
volatile bool gRunning = false;
bool gOpen = false;
pthread_t gThread;
double gSourcePos = 0;                   // fractional source frame within the ring
const double kStep = 44100.0 / 48000.0;

unsigned int Filled() { return gWrite - gRead; }

void* Feed(void*) {
    xsdk_thread_move(4);
    unsigned int frames = 0, starved = 0;
    while (gRunning) {
        float* out = 0;
        if (xsdk_audio_next_frame(100, &out) != 0 || !out) continue;
        memset(out, 0, XSDK_AUDIO_FRAME_FLOATS * sizeof(float));
        float* left = out + XSDK_AUDIO_FRONT_LEFT * XSDK_AUDIO_FRAME_SAMPLES;
        float* right = out + XSDK_AUDIO_FRONT_RIGHT * XSDK_AUDIO_FRAME_SAMPLES;
        const float scale = 1.0f / 32768.0f;
        __sync_synchronize();
        for (int i = 0; i < XSDK_AUDIO_FRAME_SAMPLES; ++i) {
            // linear interpolation between ring frames; silence while the mixer is behind
            if (Filled() < 2) {
                ++starved;
                break;
            }
            const unsigned int a = gRead % kRing, b = (gRead + 1) % kRing;
            const float t = (float)gSourcePos;
            left[i] = (gRing[a * 2] + (gRing[b * 2] - gRing[a * 2]) * t) * scale;
            right[i] = (gRing[a * 2 + 1] + (gRing[b * 2 + 1] - gRing[a * 2 + 1]) * t) * scale;
            gSourcePos += kStep;
            while (gSourcePos >= 1.0) {
                gSourcePos -= 1.0;
                ++gRead;
            }
        }
        __sync_synchronize();
        xsdk_audio_queue_frame();
        if (++frames % (188 * 60) == 0) {        // about a minute
            XsdkAudioStats st;
            xsdk_audio_stats(&st);
            PortLog("audio: %u frames played, %u silent, fewest queued %u, longest wait %u us, starved %u",
                    (unsigned int)st.played, (unsigned int)st.silent, (unsigned int)st.fewest_queued,
                    (unsigned int)st.longest_wait_us, starved);
        }
    }
    return 0;
}

bool Open(void*, int rate, int channels) {
    if (rate != 44100 || channels != 2) return false;
    if (gRunning) return true;
    if (xsdk_audio_open(12) != 0) {
        PortLog("audio: no audio output");
        return false;
    }
    gOpen = true;
    gRunning = true;
    if (pthread_create(&gThread, 0, Feed, 0) != 0) {
        gRunning = false;
        PortLog("audio: no feeder thread");
        return false;
    }
    PortLog("audio: xsdk feeder, 44.1 kHz mix resampled to 48 kHz, 12 frames ahead");
    return true;
}

void Close(void*) {}

// The mixer stops filling at three "blocks" queued: count two of ours as one, so ~140 ms are kept ahead of a
// slow tick (a room load) at a 30 Hz game rate.
int Queued(void*) { return (int)(Filled() / 2048); }

int Submit(void*, const short* interleaved, int frames) {
    if (!gRunning || frames <= 0) return 0;
    if (Filled() + (unsigned int)frames > (unsigned int)kRing) return 0;
    for (int i = 0; i < frames; ++i) {
        const unsigned int at = (gWrite + (unsigned int)i) % kRing;
        gRing[at * 2] = interleaved[i * 2];
        gRing[at * 2 + 1] = interleaved[i * 2 + 1];
    }
    __sync_synchronize();
    gWrite += (unsigned int)frames;
    return frames;
}
}

const NativeAudioBackend* NativeXsdkAudioBackend() {
    static NativeAudioBackend backend;
    static int user;
    backend.user = &user;
    backend.open = Open;
    backend.close = Close;
    backend.submit = Submit;
    backend.queued = Queued;
    return &backend;
}

void NativeXsdkAudioStop() {
    if (!gRunning) return;
    gRunning = false;
    if (gOpen) xsdk_audio_close();       // wakes a feeder waiting for a frame
    pthread_join(gThread, 0);
    gOpen = false;
}
