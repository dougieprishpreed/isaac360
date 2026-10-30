#ifndef GENERIC_NATIVE_ROOM_EFFECTS_H
#define GENERIC_NATIVE_ROOM_EFFECTS_H
#include "native_entity_body.h"
#include "native_sprite.h"
#include "native_player_effects.h"
class NativePlayerState;
class NativePlayerManager;
struct NativeRoomState;
struct NativeRoomEffect {
    NativeEntityBody body;
    NativeSprite sprite;
    bool ready,anchored;unsigned int nextAddress,updates;
    int native398,native3a4,native3a8,renderDepth;
    float native39c,native3a0,native3b8,angle,native3c0;
    NativePoint native284;
    NativeRoomEffect();
    bool Init(int variant,unsigned int seed,const NativePoint&,int configStage,int backdrop,NativeMt&);
    bool Update(const NativeRoomState&,const NativeEntityBody& player,const NativePlayerState&,const NativePlayerManager&,const NativePlayerEffects& roomEffects,NativeMt&,unsigned int frame);
private:
    NativeRoomEffect(const NativeRoomEffect&);
    NativeRoomEffect& operator=(const NativeRoomEffect&);
};
class NativeRoomEffects {
public:
    std::vector<NativeRoomEffect*> entities;
    NativePlayerEffects roomEffects;
    NativeRoomEffects(){roomEffects.playerOwner=false;}
    ~NativeRoomEffects();
    bool Spawn(int variant,unsigned int seed,const NativePoint&,int configStage,int backdrop,NativeMt&,NativePlayerManager&,unsigned int* next);
private:
    NativeRoomEffects(const NativeRoomEffects&);
    NativeRoomEffects& operator=(const NativeRoomEffects&);
};
const NativeSpriteAsset* NativePreparedRoomEffectSprite(unsigned int index);
#endif
