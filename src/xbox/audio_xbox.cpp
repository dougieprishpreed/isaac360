#include <xtl.h>
#include <xaudio2.h>
#include "../game/native_audio.h"

namespace {
const int kBlockFrames=1024;
const int kBlockBytes=kBlockFrames*2*2;
const int kBuffers=4;

struct XboxAudioState {
    IXAudio2* engine;
    IXAudio2MasteringVoice* master;
    IXAudio2SourceVoice* voice;
    XAUDIO2_BUFFER buffers[kBuffers];
    short data[kBuffers][kBlockFrames*2];
    unsigned int next;
    XboxAudioState():engine(0),master(0),voice(0),next(0){ZeroMemory(buffers,sizeof(buffers));ZeroMemory(data,sizeof(data));}
};

bool Open(void* user,int rate,int channels){
    XboxAudioState* state=(XboxAudioState*)user;
    if(!state||channels!=2||rate<=0)return false;
    if(FAILED(XAudio2Create(&state->engine,0,0))){state->engine=0;return false;}
    if(FAILED(state->engine->CreateMasteringVoice(&state->master,(UINT32)channels,(UINT32)rate,0,0,0))){
        state->engine->Release();state->engine=0;state->master=0;return false;
    }
    WAVEFORMATEX format;ZeroMemory(&format,sizeof(format));
    format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=(DWORD)rate;
    format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=(DWORD)rate*4;
    if(FAILED(state->engine->CreateSourceVoice(&state->voice,&format))){
        state->master->DestroyVoice();state->master=0;state->engine->Release();state->engine=0;state->voice=0;return false;
    }
    state->voice->Start(0);
    return true;
}
void Close(void* user){
    XboxAudioState* state=(XboxAudioState*)user;if(!state)return;
    if(state->voice){state->voice->Stop(0);state->voice->FlushSourceBuffers();state->voice->DestroyVoice();state->voice=0;}
    if(state->master){state->master->DestroyVoice();state->master=0;}
    if(state->engine){state->engine->Release();state->engine=0;}
}
int Queued(void* user){
    XboxAudioState* state=(XboxAudioState*)user;
    if(!state||!state->voice)return 0;
    XAUDIO2_VOICE_STATE voiceState;ZeroMemory(&voiceState,sizeof(voiceState));
    state->voice->GetState(&voiceState,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return (int)voiceState.BuffersQueued;
}
int Submit(void* user,const short* interleaved,int frames){
    XboxAudioState* state=(XboxAudioState*)user;
    if(!state||!state->voice||!interleaved||frames<=0||frames>kBlockFrames)return 0;
    if(Queued(user)>=kBuffers)return 0;
    const unsigned int slot=state->next;
    memcpy(state->data[slot],interleaved,(size_t)frames*2*sizeof(short));
    XAUDIO2_BUFFER& buffer=state->buffers[slot];
    ZeroMemory(&buffer,sizeof(buffer));
    buffer.AudioBytes=(UINT32)frames*4;
    buffer.pAudioData=(const BYTE*)state->data[slot];
    if(FAILED(state->voice->SubmitSourceBuffer(&buffer,0)))return 0;
    state->next=(slot+1)%kBuffers;
    return frames;
}
}

const NativeAudioBackend* NativeXboxAudioBackend(){
    static XboxAudioState state;
    static NativeAudioBackend backend;
    backend.user=&state;backend.open=Open;backend.close=Close;backend.submit=Submit;backend.queued=Queued;
    return &backend;
}
