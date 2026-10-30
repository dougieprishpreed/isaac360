#include "native_entity_body.h"
#include "native_entity_body_data.h"
#include "native_sprite.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int b){float v;memcpy(&v,&b,4);return v;}
bool Finite(float n){unsigned int b;memcpy(&b,&n,4);return (b&0x7fffffffU)<0x7f800000U;}
float Mul(float a,float b){volatile float v=a*b;return v;}
float Add(float a,float b){volatile float v=a+b;return v;}
float Div(float a,float b){volatile float v=a/b;return v;}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
void Scalar(unsigned int& h,float n){unsigned int b;memcpy(&b,&n,4);Fold(h,b);}
//Inherited entities2.xml type10 physical records. AI, death FX and rendering
//remain separate; these are the actual geometry/HP/damage fields.
const NativeEntityBodyProperties kEnemyBodies[]={
    {10,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,10.0f,0.0f,"gfx/characters/010.000_Frowning Gaper.anm2"},
    {10,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,10.0f,0.0f,"gfx/characters/010.001_Gaper.anm2"},
    {13,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,0.0f,3.0f,3.0f,0.0f,"gfx/characters/013.000_Fly.anm2"},
    {14,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,0.0f,4.0f,8.0f,0.0f,"gfx/characters/014.000_Pooter.anm2"},
    {208,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/characters/207.000_Fatty.anm2"},
    {226,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,4.0f,"gfx/226.000_Skinny.anm2"},
    {226,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,16.0f,5.0f,"gfx/226.001_Rotty.anm2"}
};
}
const NativeEntityBodyProperties* NativePreparedEnemyBody(int type,int variant){
    for(unsigned int i=0;i<sizeof(kEnemyBodies)/sizeof(kEnemyBodies[0]);++i)
        if(kEnemyBodies[i].type==type&&kEnemyBodies[i].variant==variant)return &kEnemyBodies[i];
    return 0;
}
const NativeEntityBodyProperties* NativePreparedPlayerBody(int variant){
    for(unsigned int i=0;i<sizeof(kNativePlayerBodies)/sizeof(kNativePlayerBodies[0]);++i)
        if(kNativePlayerBodies[i].variant==variant)return &kNativePlayerBodies[i];
    return 0;
}
bool NativeEntityCollisionShape::Resize(float r,float x,float y,int count,const NativeEntityBodyProperties& props){
    if(x==-1&&y==-1){x=props.xMultiplier;y=props.yMultiplier;}if(count==-1)count=props.gridPoints;
    if(!Finite(r)||!Finite(x)||!Finite(y))return false;
    if(radius==r&&xMultiplier==x&&yMultiplier==y)return true;
    std::vector<NativePoint> next;
    //Keep separate SSE float operations before the double CRT calls. Native
    //precision across different CRTs/PowerPC remains a target-validation limit.
    for(int parity=0;parity<2;++parity)for(int i=parity;i<count;){
        float angle=Div(Mul(Add((float)i,(float)i),F(kNativeBodyPi)),(float)count);
        volatile float c=(float)cos((double)angle),s=(float)sin((double)angle);
        NativePoint p={Mul(c,Mul(r,x)),Mul(s,Mul(r,y))};
        if(!Finite(p.x)||!Finite(p.y))return false;next.push_back(p);
        if(i>count-3)break;i+=2;
    }
    radius=r;xMultiplier=x;yMultiplier=y;points.swap(next);return true;
}
void NativeEntityCollisionShape::Swap(NativeEntityCollisionShape& other){
    float r=radius,x=xMultiplier,y=yMultiplier;radius=other.radius;xMultiplier=other.xMultiplier;yMultiplier=other.yMultiplier;
    other.radius=r;other.xMultiplier=x;other.yMultiplier=y;points.swap(other.points);
}
NativeEntityBody::NativeEntityBody():ready(false),native168(false),native169(false),native16a(false),native16b(false),removed(false),native16e(false),flagsLow(0),flagsHigh(0),type(0),variant(0),subtype(0),frameCount(0),initialFrame(0),state(0),gridCollisionClass(0),entityCollisionClass(0),renderDepth(0),initialSeed(0),entityToken(0),native34(0),native38(0),parentToken(0),childToken(0),
    shadowScale(0),friction(0),mass(0),collisionDamage(0),hitPoints(0),maxHitPoints(0),native2a0(0),native2ec(0),native2fc(0),appliedFriction(0),previousTime(0),time(0),native304(0),native308(0),native26c(0),native27c(0),native340(0),native360(0),boundStatus(false),damagePending(false){
    position.x=position.y=velocity.x=velocity.y=previousPosition.x=previousPosition.y=previousVelocity.x=previousVelocity.y=0;random.Default();
    native264.x=native264.y=0;memset(statusTimers,0,sizeof(statusTimers));
    native18=native1c=0;native34c=native350=0;
    gridContactKnown=gridContact=false;contactVelocity.x=contactVelocity.y=contactNormal.x=contactNormal.y=0;
}
NativeEntityBodyResult NativeEntityBody::BeginFresh(int t,int v,int sub,unsigned int seed,unsigned int frame,int stage,const NativePoint* pos,const NativePoint* vel){
    if(t<0||v<0||sub<0||!seed||stage<0)return NEB_BAD_REQUEST;
    const NativeEntityBodyProperties* p=t==1&&sub==0?NativePreparedPlayerBody(v):
        (t==10||t==13||t==14||t==208||t==226)?NativePreparedEnemyBody(t,v):t==1000?NativePreparedEffectBody(v,sub):0;
    if(!p)return NEB_UNSUPPORTED_PROPERTIES;
    NativeEntityBody next;next.type=t;next.variant=v;next.subtype=sub;next.initialSeed=seed;next.random.Set(seed,30);
    next.frameCount=0; //66f544: native+24 reset.
    next.native27c=-1; //66f566: separate damage aggregate, not the frame count.
    next.initialFrame=(int)frame;next.shadowScale=p->shadowScale;next.friction=p->friction;
    next.mass=p->mass;next.collisionDamage=p->collisionDamage;next.hitPoints=p->baseHP;
    if(p->stageHP>F(kNativeBodyStageThreshold)){float s=(float)stage;if(s>F(kNativeBodyStageCap))s=F(kNativeBodyStageCap);
        next.hitPoints=Add(Mul(Mul(stage<5?F(kNativeBodyEarlyHP):F(kNativeBodyLateHP),s),p->stageHP),p->baseHP);}
    next.maxHitPoints=next.hitPoints;
    if(pos)next.position=*pos;if(vel)next.velocity=*vel;
    next.previousPosition=next.position;next.previousVelocity=next.velocity;
    if(!next.collision.Resize(p->radius,p->xMultiplier,p->yMultiplier,p->gridPoints,*p))return NEB_BAD_REQUEST;
    //Both bound player records omit a gridcollision override. Configured
    //boundary samples select5; an empty point vector selects3 at0066fb2c.
    next.gridCollisionClass=next.collision.points.empty()?3:5;
    //66f5ec is a FOUR-byte write at169: both169 and16a become true.
    next.native169=next.native16a=true;next.state=2;next.entityCollisionClass=4;
    next.native2ec=1;next.native2fc=F(0x3f666666U);next.appliedFriction=next.friction;
    next.previousTime=F(0xb727c5acU);next.time=0;next.native304=next.native308=1;
    next.ready=true;Swap(next);return NEB_OK;
}
void NativeEntityBody::Swap(NativeEntityBody& other){
    //The scalar prefix contains no pointers, vtable or padding-dependent
    //serialization. Swap named members; the shape owns its vector separately.
#define EXCHANGE(member,type) {type n=member;member=other.member;other.member=n;}
    EXCHANGE(ready,bool);EXCHANGE(type,int);EXCHANGE(variant,int);EXCHANGE(subtype,int);EXCHANGE(frameCount,int);EXCHANGE(initialFrame,int);EXCHANGE(state,int);
    EXCHANGE(native168,bool);EXCHANGE(native169,bool);EXCHANGE(native16a,bool);EXCHANGE(native16b,bool);EXCHANGE(removed,bool);EXCHANGE(native16e,bool);EXCHANGE(flagsLow,unsigned int);EXCHANGE(flagsHigh,unsigned int);
    EXCHANGE(native18,unsigned int);EXCHANGE(native1c,unsigned int);EXCHANGE(native34c,int);EXCHANGE(native350,int);
    EXCHANGE(gridContactKnown,bool);EXCHANGE(gridContact,bool);EXCHANGE(contactVelocity,NativePoint);EXCHANGE(contactNormal,NativePoint);
    EXCHANGE(gridCollisionClass,int);EXCHANGE(entityCollisionClass,int);EXCHANGE(renderDepth,int);EXCHANGE(initialSeed,unsigned int);EXCHANGE(random,NativeRng);
    EXCHANGE(position,NativePoint);EXCHANGE(velocity,NativePoint);EXCHANGE(previousPosition,NativePoint);EXCHANGE(previousVelocity,NativePoint);
    EXCHANGE(shadowScale,float);EXCHANGE(friction,float);EXCHANGE(mass,float);EXCHANGE(collisionDamage,float);EXCHANGE(hitPoints,float);EXCHANGE(maxHitPoints,float);
    EXCHANGE(native2a0,float);EXCHANGE(native2ec,float);EXCHANGE(native2fc,float);
    EXCHANGE(entityToken,unsigned int);EXCHANGE(appliedFriction,float);EXCHANGE(previousTime,float);EXCHANGE(time,float);EXCHANGE(native304,float);EXCHANGE(native308,float);
    EXCHANGE(native34,unsigned int);EXCHANGE(native38,unsigned int);EXCHANGE(parentToken,unsigned int);EXCHANGE(childToken,unsigned int);
    EXCHANGE(native26c,int);EXCHANGE(native27c,int);EXCHANGE(native340,int);EXCHANGE(native360,int);EXCHANGE(native264,NativePoint);EXCHANGE(boundStatus,bool);EXCHANGE(damagePending,bool);
    for(int i=0;i<20;++i){EXCHANGE(statusTimers[i],int);}
#undef EXCHANGE
    tintRecords.swap(other.tintRecords);damageQueue.swap(other.damageQueue);
    collision.Swap(other.collision);
}
unsigned int NativeEntityBody::StateHash() const{
    unsigned int h=2166136261U;Fold(h,ready?1:0);Fold(h,type);Fold(h,variant);Fold(h,subtype);Fold(h,frameCount);Fold(h,initialFrame);Fold(h,state);
    Fold(h,native168);Fold(h,native169);Fold(h,native16a);Fold(h,native16b);Fold(h,removed);Fold(h,native16e);Fold(h,flagsLow);Fold(h,flagsHigh);
    Fold(h,native18);Fold(h,native1c);Fold(h,native34c);Fold(h,native350);
    Fold(h,gridContactKnown);if(gridContactKnown){Fold(h,gridContact);Scalar(h,contactNormal.x);Scalar(h,contactNormal.y);
        if(gridContact){Scalar(h,contactVelocity.x);Scalar(h,contactVelocity.y);}}
    Fold(h,gridCollisionClass);Fold(h,entityCollisionClass);Fold(h,initialSeed);Fold(h,random.state);Fold(h,random.index);
    Scalar(h,position.x);Scalar(h,position.y);Scalar(h,velocity.x);Scalar(h,velocity.y);Scalar(h,previousPosition.x);Scalar(h,previousPosition.y);Scalar(h,previousVelocity.x);Scalar(h,previousVelocity.y);
    Scalar(h,shadowScale);Scalar(h,friction);Scalar(h,mass);Scalar(h,collisionDamage);Scalar(h,hitPoints);Scalar(h,maxHitPoints);Scalar(h,native2a0);Scalar(h,native2ec);Scalar(h,native2fc);
    Scalar(h,collision.radius);Scalar(h,collision.xMultiplier);Scalar(h,collision.yMultiplier);Fold(h,(unsigned int)collision.points.size());
    Fold(h,renderDepth);Fold(h,entityToken);Scalar(h,appliedFriction);Scalar(h,previousTime);Scalar(h,time);Scalar(h,native304);Scalar(h,native308);
    Fold(h,native34);Fold(h,native38);Fold(h,parentToken);Fold(h,childToken);
    Fold(h,native26c);Fold(h,native27c);Fold(h,native340);Fold(h,native360);Scalar(h,native264.x);Scalar(h,native264.y);Fold(h,boundStatus);Fold(h,damagePending);
    for(int i=0;i<20;++i)Fold(h,statusTimers[i]);Fold(h,(unsigned int)tintRecords.size());Fold(h,(unsigned int)damageQueue.size());
    for(unsigned int i=0;i<collision.points.size();++i){Scalar(h,collision.points[i].x);Scalar(h,collision.points[i].y);}return h;
}
