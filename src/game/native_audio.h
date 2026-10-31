#ifndef GENERIC_NATIVE_AUDIO_H
#define GENERIC_NATIVE_AUDIO_H
#include <string>
//Platform audio sink. NativeAudio owns the mixer and the submitted PCM
//buffers; a backend must copy accepted frames before returning.
struct NativeAudioBackend {
    void* user;
    bool (*open)(void*,int sampleRate,int channels);
    void (*close)(void*);
    int (*submit)(void*,const short* interleaved,int frames);
    int (*queued)(void*);
};
//Shared 44.1 kHz stereo mixer for original WAV sound effects and the reached
//music streams. A missing device is not a failure: software mixing and the
//submission counters remain available for headless verification. Device
//submission is never treated as playback completion.
class NativeAudio {
public:
    NativeAudio();
    ~NativeAudio();
    bool Initialize(const char* assetRoot);
    void SetBackend(const NativeAudioBackend* backend,void* user);
    void Shutdown();
    bool PlaySfx(const char* virtualPath,float volume=0.8f,float pitch=1.0f);
    bool PlayMusic(const char* virtualPath,bool loop,float volume=0.7f);
    //Original music records can carry one-shot intro/layer-intro streams and a
    //looping main/layer pair. Empty paths are skipped; loopPath is the key.
    bool PlayMusicSet(const char* introPath,const char* loopPath,const char* layerIntroPath,
        const char* layerPath,bool loop,float volume,float layerVolume=0.55f);
    void StopMusic();
    void SetMusicVolume(float volume);
    void Update();
    void Mix(short* interleaved,int frames);
    float LastRms() const;
    bool initialized,deviceReady;
    unsigned int sfxLoaded,sfxLoadFailures,sfxStarted,sfxFinished;
    unsigned int musicStarted,musicLoops,musicFrames,musicLayerFrames,musicIntros,musicLayers,sfxFrames,framesMixed,deviceSubmits;
    std::string root;
private:
    struct Impl;
    Impl* impl;
    NativeAudio(const NativeAudio&);
    NativeAudio& operator=(const NativeAudio&);
};
#endif
