#include "native_entity_body.h"
#include "native_sprite.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int n){float f;memcpy(&f,&n,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Div(float a,float b){volatile float f=a/b;return f;}
}
void NativeEntityBody::PreUpdate(){previousPosition=position;previousVelocity=velocity;}
void NativeEntityBody::AddVelocity(NativePoint acceleration,bool ignoreTime){
    if(appliedFriction==0)return;
    float dt=ignoreTime?1:native2ec;
    velocity.x=Add(velocity.x,Div(Mul(dt,acceleration.x),appliedFriction));
    velocity.y=Add(velocity.y,Div(Mul(dt,acceleration.y),appliedFriction));
}
bool NativeEntityBody::UpdateUnmodified(NativeSprite& sprite,float water,unsigned int* next){
    if(next)*next=0x6740a0;
    if(!ready||!next||(type!=1&&type!=1000))return false;
    //The status owners are initialized by66f695..7dd, rather than inferred
    //from item IDs. Nonempty owners require their actual native branches.
    if((flagsHigh&(0x8000U|0x80000U))||!tintRecords.empty()||boundStatus||damagePending||!damageQueue.empty()||water>0)return false;
    for(int i=0;i<20;++i)if(statusTimers[i])return false;
    if(!(flagsLow&0x4000U)){
        previousTime=time;time=Add(time,native2ec);
        if(!(((flagsLow&0x20U)||native340>0)&&!removed)&&!(flagsHigh&1U)){
            float old=sprite.speed;sprite.speed=Mul(old,native2ec);
            NativeSpriteResult r=sprite.Update();sprite.speed=old;
            if(r!=NSPR_OK){*next=0x67415e;return false;}
        }
    }
    //67440b..49c: damping, reset accumulated damping, then integration.
    float damping=appliedFriction;
    if(native2ec!=1)damping=Div(damping,Sub(Add(damping,native2ec),Mul(damping,native2ec)));
    velocity.x=Mul(damping,velocity.x);velocity.y=Mul(damping,velocity.y);appliedFriction=friction;
    position.x=Add(position.x,Mul(native2ec,velocity.x));position.y=Add(position.y,Mul(native2ec,velocity.y));
    //Both304/308 are1 after Init; the source interpolation leaves1 exactly.
    //A changed target belongs to a reached effect, not this empty-owner path.
    if(native304!=1||native308!=1){*next=0x674c6e;return false;}
    if(native340>0)--native340;
    frameCount=0; //674ef3 resets the per-tick damage aggregate, not elapsed time.
    *next=0;return true;
}
