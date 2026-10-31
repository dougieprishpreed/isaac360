#include "native_door_resources.h"
#include "native_player_state.h"
#include "native_door_resource_data.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int u){float f;memcpy(&f,&u,4);return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
}
const NativeSpriteAsset* NativePreparedDoorSprite(unsigned int i){return i<sizeof(kDoorSprites)/sizeof(kDoorSprites[0])?kDoorSprites[i]:0;}
const NativeSpriteSheet* NativePreparedDoorSheet(unsigned int i){return i<sizeof(kDoorSheets)/sizeof(kDoorSheets[0])?kDoorSheets+i:0;}
const char* NativeDoorClosedAnimation(int targetType){
    //Treasure and sacrifice doors keep their key lock; ordinary, shop and
    //boss doors use the plain closed arch.
    return (targetType==4||targetType==10)?"KeyClosed":"Closed";
}
const char* NativeDoorOpenAnimation(int targetType){(void)targetType;return "Opened";}
bool NativeDoorVisual::BindFresh(NativeDoorActor& door,int backdrop,unsigned int fromFlags,unsigned int toFlags,const NativePlayerState& player){
    nextAddress=0x6c8140;unsupportedTarget=false;
    if(ready||!door.initialized||door.sourceType!=1||door.targetGrid<0||(door.state!=1&&door.state!=2)||backdrop<1||backdrop>2)return false;
    if(door.variant!=8){unsupportedTarget=true;return false;}
    int kind=door.targetType;const NativeSpriteAsset* asset=0;const char* replace=0;int trinket;
    //6c842c..8468: no secret endpoint;6c80b0=0 for these real definitions.
    //No labyrinth curse, alternate stage, custom definition or late chapter.
    if(kind==10){
        asset=kDoorSprites[2];door.flags=4;
        nextAddress=0x6c94e9;
        if(!player.TrinketMultiplier(151,&trinket)||trinket||door.auxiliary==1)return false;
    }else if(kind==4){
        //6c8a67: neither endpoint has flag800 (alternate treasure art).
        if((fromFlags|toFlags)&0x800U)return false;
        asset=kDoorSprites[1];door.flags=0;
    }else if(kind==1||kind==2){
        //6c91bc: generic door then every layer is rebound to the inherited
        //backdrop's normal texture, or6c9232's original shop texture.
        if((fromFlags|toFlags)&0x400U)return false;
        asset=kDoorSprites[0];replace=kind==2?kDoorSheets[2].path:kDoorSheets[backdrop-1].path;door.flags=0;
    }else {unsupportedTarget=true;return false;}
    nextAddress=0x406d50;
    if(main.Load(asset->path)!=NSPR_OK)return false;
    if(replace){for(unsigned int i=0;i<asset->layerCount;++i)if(main.ReplaceLayerSheet((int)i,replace)!=NSPR_OK)return false;
        if(main.RefreshSheets()!=NSPR_OK)return false;}
    //6c9824's offsets are rotation-relative. XDK uses the same float
    //operands. The native secret branch uses distance0; ordinary doors15.
    float angle=Mul(door.rotation+F(kDoorRightAngle),F(kDoorRadians)),distance=F(kDoorOffset);
    main.rotation=door.rotation;overlay.rotation=door.overlayRotation;
    main.offset.x=overlay.offset.x=Mul((float)cos((double)angle),distance);
    main.offset.y=overlay.offset.y=Mul((float)sin((double)angle),distance);
    door.native278=false;
    //6c99e0: state1 selects the target kind's closed clip and restarts at
    //frame0. Normal doors use Closed; treasure/sacrifice doors keep KeyClosed.
    //6c9df6 observes frame delta0 >= inactive room's -1; no finish-last-frame.
    nextAddress=0x6c9dcf;
    const char* clip=door.state==2?NativeDoorOpenAnimation(kind):NativeDoorClosedAnimation(kind);
    if(main.Play(clip,true)!=NSPR_OK)return false;
    door.previousState=door.state;door.previousVariant=door.variant;
    ready=true;nextAddress=0;return true;
}
bool NativeDoorVisual::Open(NativeDoorActor& door){
    if(!ready||!main.asset||!main.mainState.clip)return false;
    return main.Play(NativeDoorOpenAnimation(door.targetType),true)==NSPR_OK;
}
