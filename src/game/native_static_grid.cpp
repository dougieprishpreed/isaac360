#include "native_static_grid.h"
#include <string.h>
bool NativeSelectedWall(int roomType,int grid,unsigned int variant,unsigned int subtype,
    unsigned int seed,unsigned int frame,NativeStaticGrid* output) {
    if(!output||roomType==16||roomType<0||grid<0||grid>=kNativeRoomGridLimit||variant>65535||subtype>65535||!seed)return false;
    NativeStaticGrid wall;memset(&wall,0,sizeof(wall));
    wall.grid=grid;wall.type=15;wall.variant=(int)variant;
    //006c5670 constructor sentinel survives initialization in native+20.
    //0079c347..380 writes variant, native18=0, and the current selected seed.
    //Subtype is not used for this dispatch (only type23 consumes it).
    wall.seed=seed;wall.native20=0x16a9de81U;
    wall.collisionClass=4; //006d8a74; native76c=1000 is written by the room.
    wall.initializations=1;wall.initialized=true;wall.initializedFrame=frame;
    *output=wall;return true;
}
