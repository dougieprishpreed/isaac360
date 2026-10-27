#ifndef GENERIC_NATIVE_ENTITY_BODY_H
#define GENERIC_NATIVE_ENTITY_BODY_H
#include "native_rng.h"
#include "native_room_space.h"
#include <vector>
//Physical properties only. Asset preloading and complete entity
//creation are separate native services, not supplied by this record.
struct NativeEntityBodyProperties {
    int type,variant,subtype,gridPoints;
    float shadowScale,friction,radius,xMultiplier,yMultiplier;
    float collisionDamage,mass,baseHP,stageHP;
    const char* animationPath; //Inherited entity property+74; consumed by Sprite.
};
const NativeEntityBodyProperties* NativePreparedPlayerBody(int variant);
struct NativeEntityCollisionShape {
    float radius,xMultiplier,yMultiplier;
    std::vector<NativePoint> points; //00671de0: even indices, then odd indices.
    NativeEntityCollisionShape():radius(0),xMultiplier(0),yMultiplier(0){}
    //Paired -1 multipliers and point count -1 resolve through the properties.
    //The native unchanged-size return does not compare the point count.
    bool Resize(float,float,float,int,const NativeEntityBodyProperties&);
    void Swap(NativeEntityCollisionShape&);
};
enum NativeEntityBodyResult { NEB_OK,NEB_BAD_REQUEST,NEB_UNSUPPORTED_PROPERTIES };
//Owned physical subset of0066e910/0066f440/0066f8f0. No sprite, player health,
//inventory, registry insertion, gameplay placement or complete Init claim.
class NativeEntityBody {
public:
    bool ready;
    int type,variant,subtype,frameCount,initialFrame,state,gridCollisionClass,entityCollisionClass;
    unsigned int initialSeed;
    NativeRng random; //0066f449..80: index30 (4/3/17), no initial advance.
    NativePoint position,velocity,previousPosition,previousVelocity;
    float shadowScale,friction,mass,collisionDamage,hitPoints,maxHitPoints;
    float native2a0,native2ec,native2fc;
    NativeEntityCollisionShape collision;
    NativeEntityBody();
    NativeEntityBodyResult BeginFresh(int type,int variant,int subtype,unsigned int seed,unsigned int gameFrame,int stage);
    void Swap(NativeEntityBody&);
    unsigned int StateHash() const; //Fieldwise diagnostic, not serialization.
private:
    NativeEntityBody(const NativeEntityBody&);
    NativeEntityBody& operator=(const NativeEntityBody&);
};
#endif
