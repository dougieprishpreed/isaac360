#ifndef GENERIC_NATIVE_MINIMAP_H
#define GENERIC_NATIVE_MINIMAP_H
#include "native_sprite.h"
class NativeWorld;
class NativePlayerState;
struct NormalRunFlow;
struct NativeMinimapDraw {
    const NativeSpriteAsset* asset;const char* animation;int frame,grid;NativePoint position;
};
//8cffb0 renders into two owned textures. Retain its CPU draw lists and exact
//bounds for both platform renderers; headless verification performs no GPU work.
struct NativeMinimap {
    NativeSprite sprite;
    int columns,rows,firstColumn,firstRow,iconLimit,textureWidth,textureHeight;
    float cellWidth,cellHeight,padding,border;
    NativePoint minimum,maximum,currentMarker;
    std::vector<NativeMinimapDraw> draws;
    bool ready;
    NativeMinimap();
};
class NativeMinimaps {
public:
    NativeMinimap maps[2];NativeSprite icons;
    bool ready,discoveryPrepared,entryPrepared;unsigned int refreshes;
    NativeMinimaps():ready(false),discoveryPrepared(false),entryPrepared(false),refreshes(0){}
    bool Prepare();
    void Reset();
    bool EnterInitial(const NormalRunFlow&,NativeWorld&,const NativePlayerState&,unsigned int* next);
private:
    bool BuildInitial(int,const NativeWorld&,unsigned int* next);
    NativeMinimaps(const NativeMinimaps&);
    NativeMinimaps& operator=(const NativeMinimaps&);
};
const NativeSpriteAsset* NativePreparedMinimapSprite(unsigned int);
#endif
