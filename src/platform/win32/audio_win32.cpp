#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "../../game/native_audio.h"

namespace {
const int kBlockFrames=1024;
const int kBlockBytes=kBlockFrames*2*2;
const int kBuffers=4;

struct WinAudioState {
    HWAVEOUT output;
    WAVEHDR headers[kBuffers];
    short data[kBuffers][kBlockFrames*2];
    bool inFlight[kBuffers];
    unsigned int next;
    WinAudioState():output(0),next(0){memset(headers,0,sizeof(headers));memset(data,0,sizeof(data));memset(inFlight,0,sizeof(inFlight));}
    void Reap(){
        for(int i=0;i<kBuffers;++i)
            if(inFlight[i]&&(headers[i].dwFlags&WHDR_DONE))inFlight[i]=false;
    }
    int Queued(){
        Reap();int count=0;for(int i=0;i<kBuffers;++i)if(inFlight[i])++count;return count;
    }
};

bool Open(void* user,int rate,int channels){
    WinAudioState* state=(WinAudioState*)user;
    if(!state||channels!=2||rate<=0)return false;
    WAVEFORMATEX format;ZeroMemory(&format,sizeof(format));
    format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=(DWORD)rate;
    format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=(DWORD)rate*4;
    if(waveOutOpen(&state->output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){
        state->output=0;return false;
    }
    for(int i=0;i<kBuffers;++i){
        ZeroMemory(&state->headers[i],sizeof(WAVEHDR));
        state->headers[i].lpData=(LPSTR)state->data[i];
        state->headers[i].dwBufferLength=0;
        if(waveOutPrepareHeader(state->output,&state->headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){
            waveOutClose(state->output);state->output=0;return false;
        }
    }
    return true;
}
void Close(void* user){
    WinAudioState* state=(WinAudioState*)user;if(!state||!state->output)return;
    waveOutReset(state->output);
    for(int i=0;i<kBuffers;++i){
        state->headers[i].dwFlags|=WHDR_DONE;
        waveOutUnprepareHeader(state->output,&state->headers[i],sizeof(WAVEHDR));
    }
    waveOutClose(state->output);state->output=0;
    memset(state->inFlight,0,sizeof(state->inFlight));
}
int Submit(void* user,const short* interleaved,int frames){
    WinAudioState* state=(WinAudioState*)user;
    if(!state||!state->output||!interleaved||frames<=0||frames>kBlockFrames)return 0;
    state->Reap();
    int slot=-1;for(int i=0;i<kBuffers;++i)if(!state->inFlight[i]){slot=i;break;}
    if(slot<0)return 0;
    memcpy(state->data[slot],interleaved,(size_t)frames*2*sizeof(short));
    WAVEHDR& header=state->headers[slot];
    header.dwBufferLength=(DWORD)frames*4;
    header.dwFlags&=~WHDR_DONE;
    if(waveOutWrite(state->output,&header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)return 0;
    state->inFlight[slot]=true;
    return frames;
}
int Queued(void* user){
    WinAudioState* state=(WinAudioState*)user;return state?state->Queued():0;
}
}

const NativeAudioBackend* NativeWindowsAudioBackend(){
    static WinAudioState state;
    static NativeAudioBackend backend;
    backend.user=&state;backend.open=Open;backend.close=Close;backend.submit=Submit;backend.queued=Queued;
    return &backend;
}
