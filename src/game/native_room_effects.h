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
    unsigned int restoreInitGlobalDraws,restorePostGlobalDraws;
    NativePoint native284;
    NativeRoomEffect();
    bool Init(int variant,unsigned int seed,const NativePoint&,int configStage,int backdrop,NativeMt&);
    //79dab0 ->6bb0c0 factory/Init ->79d0b0 common dispatch.
    bool RestoreSaved(int variant,int subtype,const NativePoint&,int configStage,int backdrop,
        const unsigned int fields[0x60],NativeMt&);
    //79d33a variant33 bounds draws and79d2fc virtual+0xc variant68 update.
    bool PostRestore(const NativeRoomState&,const NativeEntityBody&,const NativePlayerState&,
        const NativePlayerManager&,const NativePlayerEffects&,NativeMt&,unsigned int frame);
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
    bool RestoreSaved(const unsigned int fields[0x60],NativeRoomState&,const NativeEntityBody&,const NativePlayerState&,
        int configStage,int backdrop,NativePlayerManager&,NativeMt&,unsigned int frame,unsigned int* next);
private:
    NativeRoomEffects(const NativeRoomEffects&);
    NativeRoomEffects& operator=(const NativeRoomEffects&);
};
const NativeSpriteAsset* NativePreparedRoomEffectSprite(unsigned int index);
#endif
