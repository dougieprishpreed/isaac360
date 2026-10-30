#include "native_door_resources.h"
#include "native_room_state.h"
#include "native_player_state.h"
bool NativeDoorVisual::UpdateInitialClosed(NativeDoorActor& door,const NativeRoomState& room,unsigned int targetFlags,unsigned int curses,const NativeEntityBody& player,const NativePlayerState& state,unsigned int* next){
    if(next)*next=0x6c7590;
    if(!next||!ready||!door.initialized||door.state!=1||door.sourceType!=1||door.variant!=8||
       (door.targetType!=1&&door.targetType!=2&&door.targetType!=4&&door.targetType!=10))return false;
    //The closed branch writes3c=4. The opening event query cannot reach
    //frame2 from the actual freshly selected KeyClosed frame0.
    if(!main.mainState.clip||main.mainState.frame!=0||!main.IsPlaying("KeyClosed"))return false;
    if(door.targetType==2||door.targetType==4){bool held;if(!state.HasCollectible(380,&held)||held){*next=0x6cbf46;return false;}}
    const bool cursedCorridor=(curses&16)&&door.sourceType==1&&door.targetType==1;
    if((door.flags&4)&&door.direction>=0&&door.direction<4&&door.auxiliary==0){
        const float rectangles[4][4]={{-43,-20,15,20},{-20,-43,20,15},{-15,-20,43,20},{-20,-15,20,43}};
        const float* r=rectangles[door.direction];const NativePoint& p=player.position;
        if((player.gridCollisionClass==5||room.type==10||cursedCorridor)&&
           door.x+r[0]<=p.x&&door.y+r[1]<=p.y&&p.x<=door.x+r[2]&&p.y<=door.y+r[3]){
            *next=0x6c7937;return false; //Reached checkpoint/damage cannot be ignored.
        }
    }
    if(main.Update()!=NSPR_OK||overlay.Update()!=NSPR_OK){*next=0x6c7a42;return false;}
    if(main.mainState.events||main.overlayState.events){*next=0x6c0170;return false;} //Game screen-shake/rumble service.
    if(targetFlags&0x40000U){*next=0x6c7b35;return false;}
    *next=0;return true;
}
