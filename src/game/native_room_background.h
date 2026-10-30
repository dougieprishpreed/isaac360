#ifndef GENERIC_NATIVE_ROOM_BACKGROUND_H
#define GENERIC_NATIVE_ROOM_BACKGROUND_H
#include "native_sprite.h"
#include "native_rng.h"
struct NativeRoomState;
struct NativeRoomBackgroundDef {
    int id,walls,wallVariants,floors,floorVariants;
    NativeSpriteSheet texture,lFloor,nFloor;
};
struct NativeBackgroundQuad {
    NativePoint source[4],destination[4]; //TL,TR,BL,BR; original pixel coordinates.
};
//CPU ownership of the ordinary backdrop render-to-texture operation. The host
//retains its draw commands; the graphics adapter uploads/submits these commands.
//There is no GPU dependency in player/room collision state.
class NativeRoomBackground {
public:
    bool ready;unsigned int nextAddress,spawnSeed,draws;
    int floorVariant,wallVariant;
    float left,top,right,bottom;
    NativeRng random;
    const NativeRoomBackgroundDef* definition;
    NativeSprite controls,buttons;
    bool controlsRequested; //87ed2c, only the actual first room of stage1.
    std::vector<NativeBackgroundQuad> floor,wall;
    NativeRoomBackground();
    bool Prepare(const NativeRoomState&,int backdrop,unsigned int spawn);
private:
    unsigned int Int(unsigned int);
    unsigned int WallVariation();
    void Quad(std::vector<NativeBackgroundQuad>&,float sx,float sy,float sw,float sh,
              float x,float y,float w,float h,bool flipX=false,bool flipY=false);
    NativeRoomBackground(const NativeRoomBackground&);
    NativeRoomBackground& operator=(const NativeRoomBackground&);
};
const NativeSpriteAsset* NativePreparedBackgroundSprite(unsigned int);
#endif
