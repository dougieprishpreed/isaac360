#include "native_audio.h"
#include <algorithm>
#include <map>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {
const int kEngineRate=44100;
const int kEngineChannels=2;
const int kMixFrames=1024;
const int kVoiceLimit=32;

unsigned int LE32(const unsigned char* p){return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
unsigned short LE16(const unsigned char* p){return (unsigned short)((unsigned int)p[0]|((unsigned int)p[1]<<8));}

void JoinAssetPath(std::string& out,const std::string& root,const std::string& relative){
    out=root;
    if(!out.empty()&&out[out.size()-1]!='/'&&out[out.size()-1]!='\\')out+='/';
    out+=relative;
#ifdef _XBOX
    for(unsigned int i=0;i<out.size();++i)if(out[i]=='/')out[i]='\\';
#endif
}

struct WavInfo {
    int format,channels,bits,rate;
    unsigned int dataOffset,dataBytes;
    WavInfo():format(0),channels(0),bits(0),rate(0),dataOffset(0),dataBytes(0){}
};

bool ReadWavInfo(FILE* file,WavInfo* info){
    if(!file||!info)return false;
    unsigned char header[12];
    if(fread(header,1,12,file)!=12||memcmp(header,"RIFF",4)!=0||memcmp(header+8,"WAVE",4)!=0)return false;
    bool haveFormat=false,haveData=false;
    for(int guard=0;guard<64&&!(haveFormat&&haveData);++guard){
        unsigned char chunk[8];
        if(fread(chunk,1,8,file)!=8)return false;
        const unsigned int size=LE32(chunk+4);
        if(memcmp(chunk,"fmt ",4)==0){
            if(size<16||size>256)return false;
            unsigned char fmt[256];
            if(fread(fmt,1,size,file)!=size)return false;
            info->format=(int)LE16(fmt);
            info->channels=(int)LE16(fmt+2);
            info->rate=(int)LE32(fmt+4);
            info->bits=(int)LE16(fmt+14);
            if(info->format==0xFFFE&&size>=26){
                //WAVE_FORMAT_EXTENSIBLE with a PCM subformat.
                info->format=(int)LE16(fmt+24);
            }
            haveFormat=true;
        }else if(memcmp(chunk,"data",4)==0){
            info->dataOffset=(unsigned int)ftell(file);
            info->dataBytes=size;
            haveData=true;
            if(size&1U)++info->dataBytes;
            break;
        }else{
            if(fseek(file,(long)(size+(size&1U)),SEEK_CUR)!=0)return false;
        }
    }
    if(!haveFormat||!haveData)return false;
    if(info->format!=1||info->channels<1||info->channels>2||info->rate<=0||
       (info->bits!=8&&info->bits!=16)||info->dataBytes==0)return false;
    return true;
}

short Sample16(const unsigned char* data,int frame,int channel,const WavInfo& info){
    const int bytes=info.bits/8;
    const int at=(frame*info.channels+channel)*bytes;
    if(info.bits==8)return (short)(((int)data[at]-128)<<8);
    return (short)((int)LE16(data+at));
}

struct NativeSoundClip {
    int frames;
    std::vector<short> samples; //Stereo at kEngineRate.
    NativeSoundClip():frames(0){}
};

bool LoadClip(const std::string& path,NativeSoundClip* clip){
    if(!clip)return false;
    FILE* file=fopen(path.c_str(),"rb");if(!file)return false;
    WavInfo info;
    if(!ReadWavInfo(file,&info)||info.dataBytes>32U*1024U*1024U){fclose(file);return false;}
    std::vector<unsigned char> data(info.dataBytes);
    if(fseek(file,(long)info.dataOffset,SEEK_SET)!=0||
       fread(&data[0],1,info.dataBytes,file)!=info.dataBytes){fclose(file);return false;}
    fclose(file);
    const int bytes=info.bits/8;
    const int sourceFrames=(int)(info.dataBytes/(unsigned int)(info.channels*bytes));
    if(sourceFrames<=0)return false;
    int outFrames=(int)(((double)sourceFrames*(double)kEngineRate)/(double)info.rate+0.5);
    if(outFrames<1)outFrames=1;
    clip->frames=outFrames;clip->samples.resize((size_t)outFrames*kEngineChannels);
    const double step=(double)info.rate/(double)kEngineRate;
    for(int i=0;i<outFrames;++i){
        double source=(double)i*step;
        int at=(int)source;if(at<0)at=0;if(at>=sourceFrames)at=sourceFrames-1;
        int next=at+1;if(next>=sourceFrames)next=sourceFrames-1;
        const double frac=source-(double)at;
        for(int c=0;c<kEngineChannels;++c){
            const int sc=(info.channels==1)?0:c;
            const double a=Sample16(&data[0],at,sc,info);
            const double b=Sample16(&data[0],next,sc,info);
            double value=a+(b-a)*frac;
            if(value>32767.0)value=32767.0;if(value<-32768.0)value=-32768.0;
            clip->samples[(size_t)i*kEngineChannels+c]=(short)value;
        }
    }
    return true;
}

struct WavStream {
    FILE* file;
    WavInfo info;
    unsigned int positionBytes;
    bool loop;
    unsigned int loops;
    std::vector<unsigned char> readBuffer;
    WavStream():file(0),positionBytes(0),loop(false),loops(0){}
    ~WavStream(){Close();}
    void Close(){if(file){fclose(file);file=0;}positionBytes=0;}
    bool Open(const std::string& path,int rate){
        Close();
        file=fopen(path.c_str(),"rb");if(!file)return false;
        if(!ReadWavInfo(file,&info)||info.rate!=rate||info.channels<1||info.channels>2||info.bits!=16){
            Close();return false;
        }
        if(fseek(file,(long)info.dataOffset,SEEK_SET)!=0){Close();return false;}
        positionBytes=0;readBuffer.resize(4096);return true;
    }
    int Read(short* output,int frames){
        if(!file||!output||frames<=0)return 0;
        for(int i=0;i<frames*kEngineChannels;++i)output[i]=0;
        int produced=0;
        const int frameBytes=info.channels*2;
        int guard=0;
        while(produced<frames&&guard++<8){
            const unsigned int remainingBytes=info.dataBytes>positionBytes?info.dataBytes-positionBytes:0;
            int available=(int)(remainingBytes/(unsigned int)frameBytes);
            if(available<=0){
                if(!loop)break;
                if(fseek(file,(long)info.dataOffset,SEEK_SET)!=0)break;
                positionBytes=0;++loops;continue;
            }
            int take=frames-produced;if(take>available)take=available;
            const size_t bytesNeeded=(size_t)take*frameBytes;
            if(readBuffer.size()<bytesNeeded)readBuffer.resize(bytesNeeded);
            if(fread(&readBuffer[0],1,bytesNeeded,file)!=bytesNeeded)break;
            for(int i=0;i<take;++i){
                const unsigned char* at=&readBuffer[(size_t)i*frameBytes];
                if(info.channels==1){
                    const short sample=(short)LE16(at);
                    output[(size_t)(produced+i)*2]=(short)(output[(size_t)(produced+i)*2]+sample);
                    output[(size_t)(produced+i)*2+1]=(short)(output[(size_t)(produced+i)*2+1]+sample);
                }else{
                    output[(size_t)(produced+i)*2]=(short)(output[(size_t)(produced+i)*2]+(short)LE16(at));
                    output[(size_t)(produced+i)*2+1]=(short)(output[(size_t)(produced+i)*2+1]+(short)LE16(at+2));
                }
            }
            positionBytes+=(unsigned int)take*frameBytes;
            produced+=take;
        }
        return produced;
    }
};
}

struct NativeAudio::Impl {
    NativeAudioBackend backend;
    void* backendUser;
    std::map<std::string,NativeSoundClip> clips;
    struct Voice {
        const NativeSoundClip* clip;
        double position,step;
        float volume;
    };
    std::vector<Voice> voices;
    WavStream music;
    std::string musicKey;
    bool musicActive,musicLoop;
    float musicGain,musicTarget;
    float lastRms;
    std::vector<int> accum;
    std::vector<short> musicBuffer;
    Impl():backendUser(0),musicActive(false),musicLoop(false),musicGain(0),musicTarget(0),lastRms(0){
        memset(&backend,0,sizeof(backend));
    }
};

NativeAudio::NativeAudio():initialized(false),deviceReady(false),sfxLoaded(0),sfxLoadFailures(0),sfxStarted(0),sfxFinished(0),
    musicStarted(0),musicLoops(0),musicFrames(0),sfxFrames(0),framesMixed(0),deviceSubmits(0),impl(new Impl){}
NativeAudio::~NativeAudio(){Shutdown();delete impl;impl=0;}

bool NativeAudio::Initialize(const char* assetRoot){
    if(!assetRoot||!*assetRoot)return false;
    root=assetRoot;initialized=true;return true;
}
void NativeAudio::SetBackend(const NativeAudioBackend* backend,void* user){
    if(!impl)return;
    if(deviceReady&&impl->backend.close)impl->backend.close(impl->backendUser);
    deviceReady=false;
    if(backend){impl->backend=*backend;impl->backendUser=user;}
    else{memset(&impl->backend,0,sizeof(impl->backend));impl->backendUser=0;}
    if(impl->backend.open&&impl->backendUser&&impl->backend.open(impl->backendUser,kEngineRate,kEngineChannels))
        deviceReady=true;
}
void NativeAudio::Shutdown(){
    StopMusic();
    if(impl){
        if(deviceReady&&impl->backend.close)impl->backend.close(impl->backendUser);
        impl->backendUser=0;memset(&impl->backend,0,sizeof(impl->backend));
        impl->clips.clear();impl->voices.clear();
    }
    deviceReady=false;initialized=false;
}
bool NativeAudio::PlaySfx(const char* virtualPath,float volume,float pitch){
    if(!initialized||!impl||!virtualPath||!*virtualPath)return false;
    std::string key(virtualPath);
    std::map<std::string,NativeSoundClip>::iterator found=impl->clips.find(key);
    if(found==impl->clips.end()){
        std::string path;JoinAssetPath(path,root,key);
        NativeSoundClip clip;
        if(!LoadClip(path,&clip)){
            ++sfxLoadFailures;return false;
        }
        found=impl->clips.insert(std::make_pair(key,clip)).first;++sfxLoaded;
    }
    if(found->second.frames<=1)return false;
    if(impl->voices.size()>=kVoiceLimit){
        //Drop the oldest voice rather than refusing an actual game event.
        impl->voices.erase(impl->voices.begin());
    }
    Impl::Voice voice;
    voice.clip=&found->second;voice.position=0;
    if(pitch<0.5f)pitch=0.5f;if(pitch>2.0f)pitch=2.0f;
    voice.step=(double)pitch;if(volume<0.0f)volume=0.0f;if(volume>1.0f)volume=1.0f;
    voice.volume=volume;
    impl->voices.push_back(voice);++sfxStarted;return true;
}
bool NativeAudio::PlayMusic(const char* virtualPath,bool loop,float volume){
    if(!initialized||!impl||!virtualPath||!*virtualPath)return false;
    if(impl->musicActive&&impl->musicKey==virtualPath){
        impl->musicLoop=loop;impl->musicTarget=volume;return true;
    }
    std::string path;JoinAssetPath(path,root,std::string(virtualPath)+".wav");
    if(!impl->music.Open(path,kEngineRate))return false;
    impl->musicKey=virtualPath;impl->musicActive=true;impl->musicLoop=loop;
    impl->musicGain=0;impl->musicTarget=volume;
    ++musicStarted;return true;
}
void NativeAudio::StopMusic(){
    if(!impl)return;
    impl->music.Close();impl->musicActive=false;impl->musicKey.clear();
}
void NativeAudio::SetMusicVolume(float volume){
    if(!impl)return;if(volume<0.0f)volume=0.0f;if(volume>1.0f)volume=1.0f;
    impl->musicTarget=volume;
}
void NativeAudio::Mix(short* output,int frames){
    if(!impl||!output||frames<=0)return;
    if(impl->accum.size()<(size_t)frames*kEngineChannels)impl->accum.resize((size_t)frames*kEngineChannels);
    if(impl->musicBuffer.size()<(size_t)frames*kEngineChannels)impl->musicBuffer.resize((size_t)frames*kEngineChannels);
    std::vector<int>& accum=impl->accum;
    std::vector<short>& music=impl->musicBuffer;
    for(size_t i=0;i<(size_t)frames*kEngineChannels;++i){accum[i]=0;music[i]=0;}
    for(unsigned int v=0;v<impl->voices.size();){
        Impl::Voice& voice=impl->voices[v];
        const NativeSoundClip* clip=voice.clip;
        double position=voice.position;
        int advanced=0;
        for(int i=0;i<frames&&clip&&clip->frames>1;++i){
            if(position>=(double)(clip->frames-1))break;
            int at=(int)position;if(at<0)at=0;if(at>=clip->frames-1)at=clip->frames-2;
            const double frac=position-(double)at;
            for(int c=0;c<kEngineChannels;++c){
                const double a=clip->samples[(size_t)at*kEngineChannels+c];
                const double b=clip->samples[(size_t)(at+1)*kEngineChannels+c];
                accum[(size_t)i*kEngineChannels+c]+=(int)((a+(b-a)*frac)*voice.volume);
            }
            position+=voice.step;++advanced;
        }
        voice.position=position;sfxFrames+=(unsigned int)advanced;
        if(!clip||position>=(double)(clip->frames-1)){
            impl->voices.erase(impl->voices.begin()+v);++sfxFinished;
        }else ++v;
    }
    if(impl->musicActive){
        const int got=impl->music.Read(&music[0],frames);
        musicFrames+=(unsigned int)got;musicLoops=impl->music.loops;
        impl->musicGain+=(impl->musicTarget-impl->musicGain)*0.12f;
        if(impl->musicGain<0.0f)impl->musicGain=0.0f;if(impl->musicGain>1.0f)impl->musicGain=1.0f;
        for(int i=0;i<got*kEngineChannels;++i)
            accum[(size_t)i]+=(int)((float)music[(size_t)i]*impl->musicGain);
        if(got<frames&&!impl->musicLoop){
            impl->music.Close();impl->musicActive=false;impl->musicKey.clear();
        }
    }
    double squares=0;
    for(int i=0;i<frames*kEngineChannels;++i){
        int value=accum[(size_t)i];
        if(value>32767)value=32767;if(value<-32768)value=-32768;
        output[i]=(short)value;
        squares+=(double)value*(double)value;
    }
    impl->lastRms=(float)sqrt(squares/(double)(frames*kEngineChannels))/32768.0f;
    framesMixed+=(unsigned int)frames;
}
float NativeAudio::LastRms() const{return impl?impl->lastRms:0.0f;}
void NativeAudio::Update(){
    if(!impl)return;
    if(deviceReady&&impl->backend.submit){
        short block[kMixFrames*kEngineChannels];
        for(int guard=0;guard<8;++guard){
            const int queued=impl->backend.queued?impl->backend.queued(impl->backendUser):0;
            if(queued>=3)break;
            Mix(block,kMixFrames);
            const int accepted=impl->backend.submit(impl->backendUser,block,kMixFrames);
            if(accepted<=0)break;
            ++deviceSubmits;
        }
    }else{
        short block[kMixFrames*kEngineChannels];
        Mix(block,kMixFrames);
    }
}
