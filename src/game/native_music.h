#ifndef GENERIC_NATIVE_MUSIC_H
#define GENERIC_NATIVE_MUSIC_H
struct NormalRunFlow;
class NativeWorld;
class NativePlayerState;
struct NativeMusicDef {
    int id;const char* intro;const char* path;const char* layerIntro;const char* layer;bool loop;
};
//CPU stream submissions are retained for the platform audio adapter. A submitted
//start is not a claim that a platform voice has played or finished the stream.
//In particular, headless verification must never invent jingle completion.
struct NativeMusicVoice {
    const NativeMusicDef* definition;
    float current,target,rate;
    unsigned int startSerial,stopSerial;
};
//POD: the existing frontend owns this across menu actions and New Run, like
//Main+29b60. New Run does not construct a new, silent music manager.
struct NativeMusicState {
    NativeMusicVoice voices[2];
    int slot,currentTrack,queuedTrack,overrideTrack,native348,native364;
    float volume,pitch,targetPitch,intensity,targetIntensity;
    unsigned int serial,roomSelections;
    bool ready;
    void Begin();
    bool StartCharacter(int page); //896bfd/8ce325 ->791210.
    //79965f selects791210 with the evaluated transition rate when that rate
    //is below one, and otherwise selects791110 with the owned music volume.
    bool StartFloorTrackCrossfade(int id,float rate);
    bool StartFloorTrack(int id,float gain);
    bool EnterStartingRoom(const NormalRunFlow&,const NativeWorld&,const NativePlayerState&,unsigned int* next);
};
//Resolved original stream identity for the platform audio adapter.
const NativeMusicDef* NativeMusicDefinition(int id);
#endif
