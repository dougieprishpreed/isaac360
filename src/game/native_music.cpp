#include "native_music.h"
#include "normal_run.h"
#include "native_world.h"
#include "native_player_state.h"
#include "native_room_effects.h"
#include "native_music_data.h"
#include <string.h>
namespace {
float F(unsigned int bits){float f;memcpy(&f,&bits,4);return f;}
//799599..7995bd loads these transition rates from .rdata. They select the
//crossfade service below one and the immediate791110 service at one.
const float kMusicRoomPairRate=F(0x3c03126fU); //00b493a4
const float kMusicPrevious65Rate=F(0x3c23d70aU); //00b493a8
const float kMusicImmediateSelectionRate=F(0x3f800000U); //00b497b4
const NativeMusicDef* Definition(int id){
    for(unsigned int i=0;i<sizeof(kMusicDefs)/sizeof(kMusicDefs[0]);++i)if(kMusicDefs[i].id==id)return kMusicDefs+i;
    return 0;
}
}
const NativeMusicDef* NativeMusicDefinition(int id){return Definition(id);}
void NativeMusicState::Begin(){
    memset(this,0,sizeof(*this));
    //8997db..9981a and78e6d0. Options MusicVolume default86e1fe.
    pitch=targetPitch=1;volume=F(kMusicDefaultVolume);
    for(int i=0;i<2;++i){voices[i].current=voices[i].target=1;voices[i].rate=F(kMusicDefaultFade);}
    ready=true;
}
bool NativeMusicState::StartFloorTrackCrossfade(int id,float rate){
    if(!ready||id<0||rate<0.0f)return false;
    const NativeMusicDef* d=Definition(id);
    if(!d||!d->path||!*d->path)return false;
    //791210 clears the active target, switches slots and starts the selected
    //record at zero toward the global music volume. Its float argument lands
    //in direct+180, the voice interpolation rate (nested+17c), not the target.
    NativeMusicVoice& old=voices[slot];old.target=0;
    slot=(slot+1)%2;NativeMusicVoice& voice=voices[slot];
    if(voice.definition)voice.stopSerial=++serial;
    voice.definition=d;voice.current=0;voice.target=volume;voice.rate=rate;
    voice.startSerial=++serial;currentTrack=id;queuedTrack=0;return true;
}
bool NativeMusicState::StartCharacter(int page){
    if(page<0||page>1)return false;
    return StartFloorTrackCrossfade(page==1?64:95,F(kMusicDefaultFade));
}
bool NativeMusicState::StartFloorTrack(int id,float gain){
    if(!ready||id<0||gain<0.0f)return false;
    const NativeMusicDef* d=Definition(id);
    if(!d||!d->path||!*d->path)return false;
    //791110 selects the record in the current voice. It does not advance the
    //stinger slot or place the requested id in310's queued owner.
    NativeMusicVoice& voice=voices[slot];
    if(voice.definition&&voice.definition!=d)voice.stopSerial=++serial;
    voice.definition=d;voice.current=voice.target=gain;voice.rate=F(kMusicDefaultFade);
    voice.startSerial=++serial;currentTrack=id;queuedTrack=0;return true;
}
bool NativeMusicState::EnterStartingRoom(const NormalRunFlow& run,const NativeWorld& world,const NativePlayerState& player,unsigned int* next){
    if(next)*next=0x7990b0;
    const NativeRoomState* room=world.currentList>=0?world.resident[world.currentList].room:0;
    const NativePlacedRoom* p=world.CurrentDescriptor();
    if(!next||!ready||!room||!p||run.context.challenge||run.context.stage!=1||run.context.stageType<0||run.context.stageType>2||
       world.layout.currentDimension||(run.runFlagsHigh&0x10000U)||room->type!=1||p->gridIndex<0)return false;
    const int config=world.resident[world.currentList].configStage;int desired=0;
    for(unsigned int i=0;i<sizeof(kMusicStages)/sizeof(kMusicStages[0]);++i)if(kMusicStages[i][0]==config)desired=kMusicStages[i][1];
    if(!desired||!Definition(desired))return false;
    //7995c5..7996d1: stingers95/64 queue the floor track at7996cb. Other
    //owned tracks take7995d7/79965f. The transition rate selects791210 below
    //one and791110 at one; 7996b5/bf reset only after that non-stinger call.
    if(currentTrack==95||currentTrack==64)queuedTrack=desired;
    else if(currentTrack!=desired&&queuedTrack!=desired){
        float transitionRate=F(kMusicDefaultFade);
        if((currentTrack==104&&desired==114)||(currentTrack==114&&desired==104))
            transitionRate=kMusicRoomPairRate;
        else if(currentTrack==65)transitionRate=kMusicPrevious65Rate;
        else if(currentTrack==45)transitionRate=kMusicImmediateSelectionRate;
        if(transitionRate<kMusicImmediateSelectionRate){
            if(!StartFloorTrackCrossfade(desired,transitionRate)){*next=0x791210;return false;}
        }else if(!StartFloorTrack(desired,volume)){*next=0x791110;return false;}
        //7996b5/bf belong only to this non-stinger transition. Stinger and
        //already-selected/queued branches jump directly to7996d1.
        native348=0;native364=-100;
    }
    //797de0: inspect the actual room-owned effect list, not the player's.
    const NativeRoomEffects* effects=world.resident[world.currentList].effects;
    if(!effects||!effects->roomEffects.Entries().empty()||room->native1210){*next=0x797df5;return false;}
    //7a2cc3 reset1210.
    //No post-generation grant has happened. Preserve the manager232 query.
    bool held;if(!player.HasCollectible(232,&held)){*next=0x797eb4;return false;}
    targetIntensity=held?F(kMusicItem232Intensity):1.0f;
    ++roomSelections;*next=0;return true;
}
