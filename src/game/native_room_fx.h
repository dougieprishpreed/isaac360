#ifndef GENERIC_NATIVE_ROOM_FX_H
#define GENERIC_NATIVE_ROOM_FX_H
#include "native_sprite.h"
#include "native_rng.h"
struct NormalRunFlow;
struct NativeRoomFxLayer {
    const NativeSpriteSheet* texture;
    NativePoint position,velocity;
    bool flipX,flipY;
    int layer;
    float parallax;
};
struct NativeRoomFxRay {const NativeSpriteSheet* texture;float opacity,speed,phase;};
struct NativeRoomFxRayGroup {float minimum,maximum,topParallax,bottomParallax,length,spacing,perspective;int first,count;};
struct NativeRoomFxEdge {NativePoint position;float rotation;int variant;};
struct NativeRoomFxParams {
    float modifier[6];bool native18;
    float native1c[4],native2c[4],shadowAlpha,native40[4],native50[3],native5c[12];
    void Reset(); //8d42f0's explicit stores, excluding padding.
};
//CPU state from8d55c0. Texture identities and original mutable Sprite owners
//are retained without requiring a host GPU context. This is not a render backend.
class NativeRoomFx {
public:
    bool begun,ready;unsigned int nextAddress,initialSeed;
    int definition,stage,stageType,selectedRay;
    NativeRng random;
    NativeRoomFxLayer lighting;
    const NativeSpriteSheet* shadingTexture;
    NativeRoomFxRay rays[8];NativeRoomFxRayGroup groups[2];NativeRoomFxEdge edges[10];
    NativeSprite veins[2];
    float shadowAlpha,colorModifier[6];
    NativeRoomFxParams parameters;
    NativeRoomFx();
    bool PrepareInitial(const NormalRunFlow&,NativeMt&);
    void Swap(NativeRoomFx&);
private:
    NativeRoomFx(const NativeRoomFx&);
    NativeRoomFx& operator=(const NativeRoomFx&);
};
#endif
