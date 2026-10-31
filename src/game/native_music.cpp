#include "native_music.h"
#include "normal_run.h"
#include "native_world.h"
#include "native_player_state.h"
#include "native_room_effects.h"
#include "native_music_data.h"
#include <string.h>
namespace {
float F(unsigned int bits){float f;memcpy(&f,&bits,4);return f;}
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
bool NativeMusicState::StartCharacter(int page){
    if(!ready||page<0||page>1)return false;
    const int id=page==1?64:95;const NativeMusicDef* d=Definition(id);
    if(!d||!d->path||!*d->path)return false;
    //791210 fades the old slot and switches the current slot immediately.
    //The six extra layer handles are absent in these original jingle records.
    NativeMusicVoice& old=voices[slot];old.target=0;old.rate=F(kMusicDefaultFade);
    slot=(slot+1)%2;NativeMusicVoice& voice=voices[slot];
    if(voice.definition)voice.stopSerial=++serial;
    voice.definition=d;voice.current=0;voice.target=volume;voice.rate=F(kMusicDefaultFade);
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
    //7995c5..7996cb: both character-page stingers queue the floor track.
    //They are the reached owner here; no audio timing or completion is faked.
    if(currentTrack!=95&&currentTrack!=64){*next=0x79965f;return false;}
    queuedTrack=desired;
    //797de0: inspect the actual room-owned effect list, not the player's.
    const NativeRoomEffects* effects=world.resident[world.currentList].effects;
    if(!effects||!effects->roomEffects.Entries().empty()||room->native1210){*next=0x797df5;return false;}
    //7a2cc3 reset1210.
    //No post-generation grant has happened. Preserve the manager232 query.
    bool held;if(!player.HasCollectible(232,&held)){*next=0x797eb4;return false;}
    targetIntensity=held?F(kMusicItem232Intensity):1.0f;
    ++roomSelections;*next=0;return true;
}
