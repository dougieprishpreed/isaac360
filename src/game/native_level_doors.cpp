#include "native_layout.h"
namespace {
int Cell(int x,int y){return x>=0&&x<13&&y>=0&&y<13?y*13+x:-1;}
bool Apply(NativeLevelLayout& level,NativePlacedRoom& room) {
    const NativeRoomDef* data=NativeRoom(room.definition);
    if(room.gridIndex<0){if(data)room.doors=data->doors;return true;}
    if(!data||room.gridIndex>=169||data->shape<1||data->shape>12||room.dimension<0||room.dimension>2)return false;
    int x=room.gridIndex%13,y=room.gridIndex/13,w=data->width/13,h=data->height/7;
    int cells[8]={Cell(x-1,y),Cell(x,y-1),Cell(x+w,y),Cell(x,y+h),Cell(x-1,y+1),Cell(x+1,y-1),Cell(x+w,y+1),Cell(x+1,y+h)};
    const unsigned int masks[]={0,0xf0,0xfa,0xf5,0xa0,0xf5,0x50,0xfa,0,0,0,0,0};
    //006f3876..006f38b5 remaps the two slots facing an L-shaped missing cell.
    switch(data->shape) {
    case 9:cells[0]=cells[1]=Cell(x,y);break;
    case 10:cells[2]=cells[5]=Cell(x+1,y);break;
    case 11:cells[3]=cells[4]=Cell(x,y+1);break;
    case 12:cells[6]=cells[7]=Cell(x+1,y+1);break;
    }
    for(int slot=0;slot<8;++slot)if((masks[data->shape]&(1U<<slot))&&!(data->doors&(1U<<slot)))cells[slot]=-1;
    for(int slot=0;slot<8;++slot) {
        int at=cells[slot];room.doorTargets[slot]=at;room.doorTargetSlots[slot]=-1;
        //Native leaves this bit untouched when a potential cell is negative.
        if(at<0)continue;
        int owner=level.owners[room.dimension][at],opposite=-1;NativePlacedRoom* target=0;
        if(owner>=0) {
            if(owner>=level.count)return false;
            target=&level.rooms[owner];const NativeRoomDef* other=NativeRoom(target->definition);if(!other)return false;
            int delta=at-target->gridIndex,dx=delta%13,dy=delta/13;opposite=(slot+2)&3;
            if(((opposite==1||opposite==3)&&dx>0)||((opposite==0||opposite==2)&&dy>0))opposite+=4;
            room.doorTargetSlots[slot]=opposite;
            if((data->doors&(1U<<slot))&&(other->doors&(1U<<opposite))) {
                room.doors|=1U<<slot;target->doors|=1U<<opposite;continue;
            }
        }
        room.doors&=~(1U<<slot);if(target)target->doors&=~(1U<<opposite);
    }
    return true;
}
}
bool NativeLevelLayout::FinalizeDoors() {
    if(count<1||count>507||currentDimension<0||currentDimension>2)return false;
    for(int i=0;i<count;++i)if(!Apply(*this,rooms[i]))return false;
    for(int i=0;i<18;++i)if(!Apply(*this,offGrid[i]))return false;
    return true;
}
const NativePlacedRoom* NativeLevelLayout::DoorTarget(int listIndex,int slot,int* reverseSlot) const {
    const NativePlacedRoom* source=ByList(listIndex);
    if(!source||slot<0||slot>=8||!(source->doors&(1U<<slot)))return 0;
    int cell=source->doorTargets[slot],reverse=source->doorTargetSlots[slot];
    const NativePlacedRoom* target=ByCell(cell,source->dimension);
    if(!target||target->definition<0||reverse<0||reverse>=8||!(target->doors&(1U<<reverse)))return 0;
    if(reverseSlot)*reverseSlot=reverse;return target;
}
