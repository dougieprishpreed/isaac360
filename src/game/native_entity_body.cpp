#include "native_entity_body.h"
#include "native_entity_body_data.h"
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
NativeEntityBody::NativeEntityBody():ready(false),type(0),variant(0),subtype(0),frameCount(0),initialFrame(0),state(0),gridCollisionClass(0),entityCollisionClass(0),initialSeed(0),
    shadowScale(0),friction(0),mass(0),collisionDamage(0),hitPoints(0),maxHitPoints(0),native2a0(0),native2ec(0),native2fc(0){
    position.x=position.y=velocity.x=velocity.y=previousPosition.x=previousPosition.y=previousVelocity.x=previousVelocity.y=0;random.Default();
}
NativeEntityBodyResult NativeEntityBody::BeginFresh(int t,int v,int sub,unsigned int seed,unsigned int frame,int stage){
    if(t<0||v<0||sub<0||!seed||stage<0)return NEB_BAD_REQUEST;
    const NativeEntityBodyProperties* p=t==1&&sub==0?NativePreparedPlayerBody(v):0;
    if(!p)return NEB_UNSUPPORTED_PROPERTIES;
    NativeEntityBody next;next.type=t;next.variant=v;next.subtype=sub;next.initialSeed=seed;next.random.Set(seed,30);
    next.initialFrame=(int)frame;next.shadowScale=p->shadowScale;next.friction=p->friction;
    next.mass=p->mass;next.collisionDamage=p->collisionDamage;next.hitPoints=p->baseHP;
    if(p->stageHP>F(kNativeBodyStageThreshold)){float s=(float)stage;if(s>F(kNativeBodyStageCap))s=F(kNativeBodyStageCap);
        next.hitPoints=Add(Mul(Mul(stage<5?F(kNativeBodyEarlyHP):F(kNativeBodyLateHP),s),p->stageHP),p->baseHP);}
    next.maxHitPoints=next.hitPoints;
    if(!next.collision.Resize(p->radius,p->xMultiplier,p->yMultiplier,p->gridPoints,*p))return NEB_BAD_REQUEST;
    //Both bound player records omit a gridcollision override. Configured
    //boundary samples select5; an empty point vector selects3 at0066fb2c.
    next.gridCollisionClass=next.collision.points.empty()?3:5;
    next.native2ec=1;next.native2fc=F(0x3f666666U);
    next.ready=true;Swap(next);return NEB_OK;
}
void NativeEntityBody::Swap(NativeEntityBody& other){
    //The scalar prefix contains no pointers, vtable or padding-dependent
    //serialization. Swap named members; the shape owns its vector separately.
#define EXCHANGE(member,type) {type n=member;member=other.member;other.member=n;}
    EXCHANGE(ready,bool);EXCHANGE(type,int);EXCHANGE(variant,int);EXCHANGE(subtype,int);EXCHANGE(frameCount,int);EXCHANGE(initialFrame,int);EXCHANGE(state,int);
    EXCHANGE(gridCollisionClass,int);EXCHANGE(entityCollisionClass,int);EXCHANGE(initialSeed,unsigned int);EXCHANGE(random,NativeRng);
    EXCHANGE(position,NativePoint);EXCHANGE(velocity,NativePoint);EXCHANGE(previousPosition,NativePoint);EXCHANGE(previousVelocity,NativePoint);
    EXCHANGE(shadowScale,float);EXCHANGE(friction,float);EXCHANGE(mass,float);EXCHANGE(collisionDamage,float);EXCHANGE(hitPoints,float);EXCHANGE(maxHitPoints,float);
    EXCHANGE(native2a0,float);EXCHANGE(native2ec,float);EXCHANGE(native2fc,float);
#undef EXCHANGE
    collision.Swap(other.collision);
}
unsigned int NativeEntityBody::StateHash() const{
    unsigned int h=2166136261U;Fold(h,ready?1:0);Fold(h,type);Fold(h,variant);Fold(h,subtype);Fold(h,frameCount);Fold(h,initialFrame);Fold(h,state);
    Fold(h,gridCollisionClass);Fold(h,entityCollisionClass);Fold(h,initialSeed);Fold(h,random.state);Fold(h,random.index);
    Scalar(h,position.x);Scalar(h,position.y);Scalar(h,velocity.x);Scalar(h,velocity.y);Scalar(h,previousPosition.x);Scalar(h,previousPosition.y);Scalar(h,previousVelocity.x);Scalar(h,previousVelocity.y);
    Scalar(h,shadowScale);Scalar(h,friction);Scalar(h,mass);Scalar(h,collisionDamage);Scalar(h,hitPoints);Scalar(h,maxHitPoints);Scalar(h,native2a0);Scalar(h,native2ec);Scalar(h,native2fc);
    Scalar(h,collision.radius);Scalar(h,collision.xMultiplier);Scalar(h,collision.yMultiplier);Fold(h,(unsigned int)collision.points.size());
    for(unsigned int i=0;i<collision.points.size();++i){Scalar(h,collision.points[i].x);Scalar(h,collision.points[i].y);}return h;
}
