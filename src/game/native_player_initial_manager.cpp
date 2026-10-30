#include "native_player_creation.h"
namespace {
bool NoCollectible(const NativePlayerState& state,int id,unsigned int pc,unsigned int* next){
    bool held;if(next)*next=pc;return state.HasCollectible(id,&held)&&!held;
}
bool NoTrinket(const NativePlayerState& state,int id,unsigned int pc,unsigned int* next){
    int count;if(next)*next=pc;return state.TrinketMultiplier(id,&count)&&count<=0;
}
}
bool NativePlayerCreation::FinishInitialManager(NativePlayerState& state,const NativeEntityBody& body,unsigned int* next){
    if(next)*next=0x759b30;
    if(!next||!postGenerationPrepared||!initialSaveNormalized||!state.native1734||!state.stats.ready||!state.runInitializing)return false;
    //Preserve759b30's actual query order. Each of these absent live trinkets
    //bypasses both its global draw and its indexed-item RNG advance.
    if(!NoTrinket(state,32,0x759b47,next)||!NoTrinket(state,132,0x759c2c,next)||!NoTrinket(state,153,0x759d03,next)||
       !NoCollectible(state,673,0x759dfa,next))return false;
    unsigned int prior;
    if(!constructorFields.Scalar(0x1ea8,&prior)||prior){*next=0x759e4f;return false;}
    constructorFields.AssignScalar(0x1ea8,0);
    if(!NoTrinket(state,166,0x759e9e,next))return false;
    unsigned char native12fc;
    if(!constructorFields.Byte(0x12fc,&native12fc)||native12fc){*next=0x75a009;return false;}
    if(!NoCollectible(state,313,0x75a0ad,next)||!NoCollectible(state,535,0x75a11d,next)||!NoCollectible(state,299,0x75a18a,next))return false;
    //777ef0's slot0 getter. All four active owners are real Init records;
    //an earned D6 belongs to that earlier grant path, never suppressed here.
    if(state.activeItems[0]==299){*next=0x75a195;return false;}
    if(!NoCollectible(state,122,0x75a24d,next)||!NoTrinket(state,33,0x75a2f3,next))return false;
    const int items[]={409,142,407,415,442};
    for(int i=0;i<5;++i)if(!NoCollectible(state,items[i],0x759b30,next))return false;
    if(!NoTrinket(state,121,0x759b30,next))return false;
    if(state.effects.Has(2,46)){*next=0x879800;return false;}
    if(body.variant==0&&!NoCollectible(state,497,0x759b30,next))return false;
    if(!NoCollectible(state,588,0x759b30,next))return false;
    //1f14/1f80 gate the queued1654 item-effect restoration. Its actual
    //constructor-owned begin/end are equal on this clean first entry.
    unsigned char native1f14,ghost;
    if(!constructorFields.Byte(0x1f14,&native1f14)||!constructorFields.Byte(0x1f80,&ghost))return false;
    if(!native1f14&&!ghost){unsigned int begin,end;
        if(!constructorFields.Scalar(0x1654,&begin)||!constructorFields.Scalar(0x1658,&end)||begin!=end){*next=0x75a68e;return false;}}
    constructorFields.AssignBytes(0x12fc,native12fc>1?1:0,1);
    *next=0;return true;
}
