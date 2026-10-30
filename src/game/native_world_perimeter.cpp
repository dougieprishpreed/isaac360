#include "native_world.h"
#include "native_room_space_data.h"
namespace {
bool AddWall(NativeRoomState& room,int x,int y,NativeMt& random,unsigned int frame){
    int grid=y*room.geometry.stride+x;
    if(grid<0||grid>=room.geometry.cells||room.staticGridCount>=kNativeRoomGridLimit)return false;
    //79c52c returns before allocation/MT when ANY grid already occupies it,
    //including an attached door. Collision class0 is still an occupant.
    if(room.collision.occupied[grid])return true;
    unsigned int seed=random.Next();
    NativeStaticGrid& wall=room.staticGrids[room.staticGridCount];
    if(!NativePerimeterWall(room.type,grid,seed,frame,&wall))return false;
    ++room.staticGridCount;return room.collision.Set(grid,true,wall.collisionClass,1000,15);
}
}
NativeWorldResult NativeWorld::PreparePerimeter(NativeMt& random,unsigned int frame){
    NativeRoomState* room=Current();const NativePlacedRoom* descriptor=CurrentDescriptor();
    if(!room||!descriptor||!resident[currentList].doors||resident[currentList].perimeterReady||descriptor->visitedCount)return NWR_BAD_REQUEST;
    //The actual normal starting selector requires shape1. Types16 and the
    //four L-shape loops have additional native branches; they are not repicked
    //or replaced by a rectangle when entered later in the game.
    if(room->type==16||room->shape<1||room->shape>8)return NWR_CONSTRUCTION_GAP;
    const int* rect=kNativeRoomSpaceCells[room->shape];
    int left=rect[0]-1,top=rect[1]-1,right=rect[2]+1,bottom=rect[3]+1;
    //7c8d17..7c8d90: top,bottom interleaved per X, then left,right per Y,
    //including the duplicate corners. Those return occupied without draws.
    for(int x=left;x<=right;++x)if(!AddWall(*room,x,top,random,frame)||!AddWall(*room,x,bottom,random,frame))return NWR_BAD_REQUEST;
    for(int y=top;y<=bottom;++y)if(!AddWall(*room,left,y,random,frame)||!AddWall(*room,right,y,random,frame))return NWR_BAD_REQUEST;
    resident[currentList].perimeterReady=true;return NWR_OK;
}
