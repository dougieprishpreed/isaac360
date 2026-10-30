#ifndef GENERIC_NATIVE_ROOM_SPACE_H
#define GENERIC_NATIVE_ROOM_SPACE_H
#include "native_room_grid.h"
struct NativePoint {float x,y;};
struct NativeRoomCollision;
//Room+14..20 physical boundaries, distinct from grid storage dimensions.
struct NativeRoomSpace {
    int shape;
    float left,top,right,bottom;
    float regions[8]; //007c9420's two rectangles for L shapes.
    bool zeroCollision; //Original configStage35/type16 exception.
    bool Init(int shape,const NativeRoomGrid&,int configStage,int roomType);
    //0079fda0(false), after real grid construction. Only native types15/16
    //trim fully occupied outer rows/columns; collision class is not the test.
    bool RefreshBounds(const NativeRoomGrid&,const NativeRoomCollision&);
    //007bd810. Margins may differ on each side; original tie direction and
    //scalar float operation order are preserved. No entity/grid collision.
    bool Clamp(const NativePoint&,float leftMargin,float topMargin,float rightMargin,float bottomMargin,NativePoint*) const;
    //007bdb20 wrapping uses the original point and clamp delta, without a
    //second clamp. This is geometry, not entitlement to a wrapping effect.
    bool Wrap(const NativePoint&,float leftMargin,float topMargin,float rightMargin,float bottomMargin,NativePoint*) const;
    bool Contains(const NativePoint&,float margin) const; //007bdca0
};
//Known native grid collision fields. Factories own occupant classes and writes
//to+76c. This container does not synthesize walls/rocks from metadata or claim
//complete actor collision, pathfinding, native grid restoration or movement.
struct NativeRoomCollision {
    int native76c[kNativeRoomGridLimit],classes[kNativeRoomGridLimit],types[kNativeRoomGridLimit];
    unsigned short nativeE6c[kNativeRoomGridLimit];
    bool occupied[kNativeRoomGridLimit];
    void Clear();
    bool Set(int cell,bool occupant,int collisionClass,int nativeValue,int nativeType=0);
    bool At(int cell,bool zeroCollision,int* collisionClass) const; //0079e1f0
    bool AtPosition(const NativeRoomGrid&,const NativePoint&,bool zeroCollision,int*) const;
    bool CostAtPosition(const NativeRoomGrid&,const NativePoint&,int*) const; //007bd4d0
};
#endif
