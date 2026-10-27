#include "native_room_setup.h"
#include "native_room_setup_data.h"
#include <string.h>
void NativeApplyRoomMarker(const RoomRecordOption& e,NativeRoomMarkers* m) {
    if(!m)return;
    switch(e.type) {
    case 825:case 907:m->waterPits=true;break;
    case 829:m->noWaterPits=true;break;
    case 876:case 918:m->noWater=true;m->flags|=0x1000;break;
    case 882:m->flags|=0x1000;break;
    case 905:m->native45=true;break;
    case 919:m->native37=true;break;
    case 970:
        if(e.variant==0)m->flags|=0x200;
        else if(e.variant==3)m->flags|=0x2000;
        else if(e.variant==1) {
            if(e.subtype==20)m->noWaterPits=true;
            else if(e.subtype==11)m->water=true;
            else if(e.subtype==10){m->noWater=true;m->flags|=0x1000;}
            else if(e.subtype<4){m->directionSet=true;m->direction=e.subtype;}
        }
        break;
    }
}
bool NativeBackdropWaterPitsMode(int backdrop,int* mode) {
    if(!mode||backdrop<0||backdrop>=kNativeBackdropSetupCount||kNativeBackdropWaterPitsMode[backdrop]<0)return false;
    *mode=kNativeBackdropWaterPitsMode[backdrop];return true;
}
bool NativeSetupRoom(unsigned int type,unsigned int flags,unsigned int visits,const NativeRoomSeeds& seeds,
    const NativeRoomMarkers& markers,const NativeRoomSetupContext& c,NativeRoomSetupState* output) {
    int mode;if(!output||!seeds.spawn||c.configStage<0||!NativeBackdropWaterPitsMode(c.backdrop,&mode))return false;
    NativeRoomSetupState s;memset(&s,0,sizeof(s));s.flags=flags|markers.flags;s.markers=markers;
    s.waterSeed=s.styleState=seeds.spawn;s.firstVisit=visits==0;
    //The backdrop12 draw uses a separate index6 clone, not index35 or award.
    s.waterAmount=(c.backdrop==6||c.backdrop==44)?1.0f:0.0f;
    if(c.backdrop==12&&type==1){s.waterSeed=NativeShift(seeds.spawn,1,19,3);s.waterDraws=1;s.waterAmount=s.waterSeed%5==0?1.0f:0.0f;}
    if((c.configStage==27||c.configStage==28)&&type!=27)s.waterAmount=1.0f;
    if((s.flags&0x100)||markers.water)s.waterAmount=1.0f;
    if(c.backdrop==16||c.backdrop==35||type==16||(markers.noWater&&!(s.flags&0x100)))s.waterAmount=0.0f;
    if(s.firstVisit) {
        bool pits;
        if(c.backdrop==6&&!c.drownedWaterDisabled)pits=true;
        else if(mode==1){s.styleState=NativeShift(s.styleState,5,9,7);++s.styleDraws;pits=(s.styleState&1)==0;}
        else pits=mode==2;
        if(pits)s.flags|=0x20;else s.flags&=~0x20U;
        s.styleState=NativeShift(s.styleState,5,9,7);++s.styleDraws;
        if(s.styleState&1)s.flags&=~0x40U;else s.flags|=0x40;
        s.shopVisit=type==2;
        if(c.backdrop==32&&(s.flags&0x200))s.flags&=~0x20U;
        bool force=markers.waterPits,deny=markers.noWaterPits;
        if(c.backdrop==58||c.backdrop==59){s.flags|=0x2000;if(c.levelFlags&0x400000){force=true;deny=false;}}
        if(force)s.flags|=0x20;
        if(deny)s.flags&=~0x20U;
        if(c.backdrop==46&&(s.flags&0x20))s.flags|=0x1000;
    }
    *output=s;return true;
}
