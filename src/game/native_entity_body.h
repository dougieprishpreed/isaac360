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
const NativeEntityBodyProperties* NativePreparedEffectBody(int variant,int subtype);
const NativeEntityBodyProperties* NativePreparedEnemyBody(int type,int variant);
class NativeSprite;
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
    bool native168,native169,native16a,native16b,removed,native16e;
    unsigned int flagsLow,flagsHigh;
    unsigned int native18,native1c; //66f7a3/aa: separate deferred-status flags.
    int native34c,native350; //66f875/7f: pit/fall lifecycle.
    bool gridContactKnown,gridContact; //188 is first defined by collision processing.
    NativePoint contactVelocity,contactNormal; //18c/190,194/198; read only after contact.
    int type,variant,subtype,frameCount,initialFrame,state,gridCollisionClass,entityCollisionClass,renderDepth;
    unsigned int initialSeed,entityToken,native34,native38,parentToken,childToken; //30c/324 nullable stable identities.
    NativeRng random; //0066f449..80: index30 (4/3/17), no initial advance.
    NativePoint position,velocity,previousPosition,previousVelocity;
    float shadowScale,friction,mass,collisionDamage,hitPoints,maxHitPoints;
    float native2a0,native2ec,native2fc,appliedFriction,previousTime,time,native304,native308;
    int statusTimers[20],native26c,native27c,native340,native360; //360..36c base-Init vector/timers.
    NativePoint native264;
    bool boundStatus,damagePending;
    std::vector<unsigned int> tintRecords,damageQueue;
    NativeEntityCollisionShape collision;
    NativeEntityBody();
    NativeEntityBodyResult BeginFresh(int type,int variant,int subtype,unsigned int seed,unsigned int gameFrame,int stage,const NativePoint* position=0,const NativePoint* velocity=0);
    void PreUpdate(); //674060.
    bool UpdateUnmodified(NativeSprite&,float waterAmount,unsigned int* nextAddress); //6740a0, actual empty status owners.
    void AddVelocity(NativePoint acceleration,bool ignoreTime=false); //424130.
    void Swap(NativeEntityBody&);
    unsigned int StateHash() const; //Fieldwise diagnostic, not serialization.
private:
    NativeEntityBody(const NativeEntityBody&);
    NativeEntityBody& operator=(const NativeEntityBody&);
};
#endif
