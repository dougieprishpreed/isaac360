#include "native_player_manager.h"
#include "native_player_creation.h"
#include "native_world.h"
#include "normal_run.h"
bool NativePlayerManager::PlaceInitial(const NormalRunFlow& run,const NativeWorld& world,NativePlayerCreation& creation,const NativePlayerState& state,NativeEntityBody& body,unsigned int* next){
    if(next)*next=0x905cd0;
    const NativePlacedRoom* p=world.CurrentDescriptor();
    if(!next||!initialized||members.size()!=1||!p||entityToken!=body.entityToken||!body.ready||!worldRegistered||
       run.context.stage!=1||run.context.stageType<0||run.context.stageType>2||run.context.challenge||run.context.nativeMode||
       body.variant||state.native1734||creation.postGenerationPrepared||world.currentGrid!=world.previousGrid)return false;
    //7562c3..2ef, before the variant/caller tests. These are live stores,
    //not edits to the immutable constructor export or cache snapshot.
    creation.constructorFields.AssignScalar(0x1a34,0);creation.constructorFields.AssignScalar(0x1a3c,0);
    creation.constructorFields.AssignScalar(0x1a2c,0);creation.constructorFields.AssignScalar(0x1a30,0);
    //428b30 returns stage, or stage+1 for styles4/5.703680 tests flag48
    //during chapters1..6. Ordinary stage1 therefore reaches75633e's actual
    //(320,380) operand, not7bd5c0's center fallback.
    if(run.runFlagsHigh&0x10000U){*next=0x75635c;return false;}
    //906950 copies a nonzero supplied position unchanged for a single
    //registered player. Native1540=0 is the authoritative manager ordinal.
    if(creation.native1540!=0)return false;
    body.position.x=320.0f;body.position.y=380.0f;
    //7566b2 sees the real pre-grant1734=false and returns75740a.905d39
    //then visits the actual room list, whose only types are1 and1000.
    for(unsigned int i=0;i<worldTypes.size();++i)if(worldTypes[i]==3){*next=0x905d63;return false;}
    *next=0;return true;
}
