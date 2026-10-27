#include "native_doors.h"
#include <string.h>
bool NativeDoorGrid(int shape,const NativeRoomGrid& g,int slot,int* output) {
    NativeRoomGrid expected;if(!output||!expected.Init(shape,g.width,g.height)||g.stride!=expected.stride||g.rows!=expected.rows||g.cells!=expected.cells||slot<0||slot>7)return false;
    int left=0,right=g.stride-1,top=0,bottom=g.rows-1;
    if(shape==2||shape==7){top=3;bottom-=3;}
    if(shape==3||shape==5){left=4;right-=4;}
    int x[8]={left,7,right,7,left,20,right,20};int y[8]={4,top,4,bottom,11,top,11,bottom};
    if(shape==9){x[0]=13;y[1]=7;}
    if(shape==10){x[2]=14;y[5]=7;}
    if(shape==11){y[3]=8;x[4]=13;}
    if(shape==12){x[6]=14;y[7]=8;}
    int cell=y[slot]*g.stride+x[slot];if(cell<0||cell>=kNativeRoomGridLimit)return false;*output=cell;return true;
}
NativeDoorResult NativeCreateDoor(int slot,int grid,unsigned int frame,NativeMt& global,NativeDoorSet* output,const NativeDoorActor* saved) {
    if(!output||slot<0||slot>7)return NDR_BAD_REQUEST;
    if(output->attached&(1U<<slot))return NDR_ALREADY_ATTACHED;
    if(grid<0||grid>=kNativeRoomGridLimit||!global.Valid()||(saved&&(saved->grid!=grid||saved->type!=16)))return NDR_BAD_REQUEST;
    NativeDoorActor door;
    if(saved)door=*saved;
    else {memset(&door,0,sizeof(door));door.type=16;door.local.Default();}
    door.grid=grid;door.seed=global.Next();
    //006cbe50 resets state0/2 to1; other saved states and variants survive.
    if(door.state==0||door.state==2)door.state=1;
    door.previousState=door.state;door.previousVariant=door.variant;
    door.sourceType=door.targetType=0;door.targetGrid=-1;
    door.native278=door.native279=door.native27a=false;door.flags=0;
    ++door.initializations;door.initialized=true;door.spawnFrame=frame;
    static const float angles[4]={-90,0,90,180};door.direction=slot&3;
    door.rotation=door.overlayRotation=angles[door.direction];door.slot=slot;
    //Position needs the actual stride; NativeInitDoorSet fills it below.
    output->actors[slot]=door;output->native76c[grid]=1000;output->attached|=1U<<slot;++output->draws;return NDR_OK;
}
bool NativeInitDoorSet(int shape,const NativeRoomGrid& geometry,unsigned int allowed,unsigned int frame,NativeMt& global,NativeDoorSet* output) {
    if(!output||allowed>255||!global.Valid())return false;
    NativeDoorSet doors;memset(&doors,0,sizeof(doors));
    for(int i=0;i<8;++i)if(!NativeDoorGrid(shape,geometry,i,&doors.potentialGrid[i]))return false;
    NativeMt owner=global;
    for(int slot=0;slot<8;++slot)if(allowed&(1U<<slot)) {
        int cell=doors.potentialGrid[slot];if(NativeCreateDoor(slot,cell,frame,owner,&doors)!=NDR_OK)return false;
        doors.actors[slot].x=(float)(cell%geometry.stride)*40.0f+40.0f;
        doors.actors[slot].y=(float)(cell/geometry.stride)*40.0f+120.0f;
    }
    *output=doors;global=owner;return true;
}
namespace {
bool Secret(int type){return type==7||type==8||type==29;}
int Custom(const NativeDoorBinding& c,int from,int to) {
    if(to==27)return c.targetData?c.targetVariant:0;
    if(from==27)return c.sourceData?c.sourceVariant:0;
    return 0;
}
}
bool NativeSelectDoor(const NativeDoorBinding& c,int variant,int auxiliary,NativeDoorSelection* output) {
    if(!output||c.nativeMode<0||c.nativeMode>3||c.stage<0||c.stageType<0||c.stageType>5||c.dimension<0||c.dimension>2||c.sourceType<0||c.targetType<0||variant<0)return false;
    NativeDoorSelection d={c.sourceType,c.targetType,variant,auxiliary,false};
    bool alternate=c.nativeMode==2||c.nativeMode==3;
    if((c.sourceFlags|c.targetFlags)&0x400){if(d.sourceType==7||d.sourceType==8)d.sourceType=1;if(d.targetType==7||d.targetType==8)d.targetType=1;}
    if(!d.variant) {
        bool selected=false;
        if(alternate&&d.targetType==23&&c.generatedRoomCount<10+(c.nativeMode==3)){d.variant=6;selected=true;}
        int custom=Custom(c,d.sourceType,d.targetType);
        bool special=false;
        if(!(Secret(d.sourceType)||d.sourceType==10)) {
            if(d.targetType==27)special=c.targetData&&custom!=0;
            else if(d.sourceType==27)special=c.sourceData&&custom!=0;
            else if(d.targetType==5)special=c.targetData&&c.targetVariant==90;
            else if(d.sourceType==5)special=c.sourceData&&c.sourceVariant==90;
        }
        if(!selected&&special){d.variant=custom==2||custom==4?3:1;if(custom==3)d.auxiliary=1;if(d.targetType!=27&&d.sourceType!=27)d.variant=3;if(c.currentGrid==-10)d.variant=8;selected=true;}
        if(!selected&&c.currentGrid==-16){d.variant=8;selected=true;}
        if(!selected&&c.targetGrid==-101&&c.dimension==0){d.variant=3;selected=true;}
        if(!selected&&c.targetGrid==-100){d.variant=1;selected=true;}
        if(!selected) {
            bool sourceSpecial=Secret(d.sourceType)||d.sourceType==10;
            bool firstFree=c.stage==1&&c.stageType!=4&&c.stageType!=5;
            bool primaryVariant=!sourceSpecial&&(
                d.targetType==9||d.targetType==24||d.targetType==12||
                (d.targetType==4&&!(c.reflectedStage&&c.dimension==1)&&!firstFree&&!alternate)||
                (d.targetType==2&&!(c.reflectedStage&&c.dimension==1)&&!firstFree&&!alternate&&!c.seedEffect33)||
                (d.targetType==5&&c.targetGrid>=0&&c.stage==9));
            if(!sourceSpecial&&c.challenge==24&&!Secret(d.targetType))primaryVariant=true;
            if(alternate&&d.targetType==4&&d.sourceType!=7&&d.sourceType!=8&&c.targetGrid==c.treasureGrid)primaryVariant=true;
            if(primaryVariant){d.variant=1;selected=true;}
            if(!selected&&!Secret(d.sourceType)&&d.targetType==18){d.variant=3;selected=true;}
            if(!selected&&!Secret(d.sourceType)&&d.targetType==19){d.variant=3;selected=true;}
            if(!selected&&!Secret(d.sourceType)&&(d.targetType==20||d.targetType==21)){d.variant=2;selected=true;}
            if(!selected) {
                if((d.sourceType==6||c.room711c)&&!(c.sourceFlags&1)&&!Secret(d.targetType))d.variant=4;
                else if(c.targetGrid==-7)d.variant=5;
                else if(Secret(d.sourceType)||Secret(d.targetType))d.variant=7;
                else d.variant=8;
            }
        }
    }
    if((d.variant==1||d.variant==2||d.variant==3)&&(c.sourceFlags&0x400)&&!(c.targetFlags&0x400))d.variant=8;
    d.openAfterAssetSetup=d.targetType==29;*output=d;return true;
}
