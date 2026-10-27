#include "native_player_motion.h"
#include "native_player_motion_data.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int bits){float n;memcpy(&n,&bits,4);return n;}
bool Finite(float n){unsigned int b;memcpy(&b,&n,4);return (b&0x7fffffffU)<0x7f800000U;}
bool Finite(NativePoint p){return Finite(p.x)&&Finite(p.y);}
float Add(float a,float b){volatile float n=a+b;return n;}
float Sub(float a,float b){volatile float n=a-b;return n;}
float Mul(float a,float b){volatile float n=a*b;return n;}
float Div(float a,float b){volatile float n=a/b;return n;}
float Root(float a){volatile float n=(float)sqrt((double)a);return n;}
float Length2(NativePoint p){return Add(Mul(p.x,p.x),Mul(p.y,p.y));}
float Length(NativePoint p){return Root(Length2(p));}
NativePoint Scale(NativePoint p,float k){NativePoint n={Mul(p.x,k),Mul(p.y,k)};return n;}
NativePoint Plus(NativePoint a,NativePoint b){NativePoint n={Add(a.x,b.x),Add(a.y,b.y)};return n;}
NativePoint Minus(NativePoint a,NativePoint b){NativePoint n={Sub(a.x,b.x),Sub(a.y,b.y)};return n;}
bool Nonzero(NativePoint p){return p.x!=0||p.y!=0;}
NativePoint Unit(NativePoint p){float n=Length(p);return n>0?Scale(p,Div(1,n)):p;}
int Direction(NativePoint p,bool zeroDown){if(fabsf(p.x)>fabsf(p.y))return p.x<0?0:2;return p.y<0?1:(p.y>0||zeroDown?3:-1);}
NativePoint Cardinal(NativePoint p){NativePoint n={0,0};if(fabsf(p.x)>fabsf(p.y))n.x=p.x<0?-1.0f:1.0f;else n.y=p.y<0?-1.0f:1.0f;return n;}
float WrapAngle(float a){if(fabsf(a)>F(kMotion360))a=(float)fmod((double)a,(double)F(kMotion360));
    if(a>F(kMotion180))a=Sub(a,F(kMotion360));else if(a<-F(kMotion180))a=Add(a,F(kMotion360));return a;}
float ApproachAngle(float a,float b){a=WrapAngle(a);b=WrapAngle(b);if(fabsf(Sub(b,a))>F(kMotion180)){if(b>a)a=Add(a,F(kMotion360));else b=Add(b,F(kMotion360));}
    return WrapAngle(Add(Mul(Sub(b,a),F(kMotion030)),a));}
NativePlayerVelocityResult Fail(NativePlayerInputResult r,unsigned int pc,int effect,NativePlayerVelocityIssue* issue){
    if(issue){issue->nextAddress=pc;issue->missingEffect=effect;issue->sourceResult=r;}
    return r==NPI_BAD_VALUE?NPV_BAD_VALUE:r==NPI_SOURCE_MISSING?NPV_SOURCE_MISSING:NPV_BAD_REQUEST;
}
NativePlayerVelocityResult Done(NativePlayerVelocityState* out,const NativePlayerVelocityState& n,unsigned int pc,NativePlayerVelocityIssue* issue){
    if(issue){issue->nextAddress=pc;issue->missingEffect=-1;issue->sourceResult=NPI_OK;}*out=n;
    return pc==0x72c3c4?NPV_RETURNED:NPV_NEED_UPDATE_TAIL;
}
}
NativePlayerVelocityResult NativePlayerVelocityCore(const NativePlayerVelocityContext& c,NativeActionSource& actions,NativeMovementEffects& effects,NativePlayerVelocityState* output,NativePlayerVelocityIssue* issue){
    if(!output)return Fail(NPI_BAD_REQUEST,0x72ae20,-1,issue);
    if(!Finite(c.native1d78)||!Finite(c.native2a0))return Fail(NPI_BAD_VALUE,0x72ae4f,-1,issue);
    NativePlayerVelocityState n=*output;
    if(c.native1d78<0||c.native2a0<0)return Done(output,n,0x72c3c4,issue);
    if(c.targetPresent){if(!Finite(c.position)||!Finite(c.targetPosition))return Fail(NPI_BAD_VALUE,0x72ae73,-1,issue);
        n.velocity=Minus(c.targetPosition,c.position);if(!Finite(n.velocity))return Fail(NPI_BAD_VALUE,0x72ae83,-1,issue);return Done(output,n,0x72c3c4,issue);}
    if(c.input.playerType<0||c.input.stage<0||c.input.stageType<0||c.input.stageType>5||c.input.dimension<0||c.input.dimension>2)return Fail(NPI_BAD_REQUEST,0x72aea8,-1,issue);
    if(!Finite(n.velocity)||!Finite(n.movement)||!Finite(n.lastMovement)||!Finite(n.angle)||!Finite(c.moveSpeed)||!Finite(c.native2ec))return Fail(NPI_BAD_VALUE,0x72aea8,-1,issue);
    n.previousVelocity=n.velocity;NativePoint input={0,0};NativePlayerInputResult r;
    const bool horizontalOnly=(c.flagsLow&0x8000)!=0&&!c.native14a4;
    const bool special=c.input.playerType==35&&!c.native1f80;
    if(c.input.controlsEnabled&&c.input.controlsCooldown<=0){
        r=NativeMovementInput(c.input,actions,&input);if(r!=NPI_OK)return Fail(r,0x72af02,-1,issue);
        if(horizontalOnly)input.y=0;if(Length(input)>1)input=Unit(input);
    }
    if(special||c.native1e24!=0||c.native1ef4){input.x=0;input.y=0;}
    const int type=c.input.playerType;
    if(((type==19||type==20||type==29||type==38)&&c.input.twinPresent)||type==26){
        const NativePlayerInputContext* owner=&c.input;
        if(c.input.twinPresent&&c.input.twinNative1540<c.input.native1540){
            if(!c.twinInputKnown)return Fail(NPI_SOURCE_MISSING,0x72b012,-1,issue);
            if(c.twinInput.native1540!=c.input.twinNative1540||c.twinInput.stage!=c.input.stage||c.twinInput.stageType!=c.input.stageType||c.twinInput.dimension!=c.input.dimension)return Fail(NPI_BAD_REQUEST,0x72b012,-1,issue);
            owner=&c.twinInput;
        }
        NativePoint paired;r=NativeMovementInput(*owner,actions,&paired);if(r!=NPI_OK)return Fail(r,0x72b016,-1,issue);
        if(Nonzero(paired))n.native16d8=0;
    }
    bool effect63=false;r=effects.Has(63,c.input.playerToken,&effect63);if(r!=NPI_OK)return Fail(r,0x72b054,63,issue);
    NativePoint work=input;
    if(effect63){
        if(Length(input)>F(kMotion025)){
            if(n.native1dc4<=0){NativePoint u=Unit(input);n.velocity=Plus(n.velocity,Scale(Plus(u,u),c.native2ec));n.native1dc4=5;}
            else n.native1dc4=n.native1dc4>2?n.native1dc4-1:1;
        }else n.native1dc4=(int)((unsigned int)n.native1dc4-1U);
        work.x=0;work.y=0;
    }else n.native1dc4=0;
    if(c.native1798>0)work=Plus(work,Scale(Minus(Cardinal(n.movement),work),F(kMotion060)));
    n.movement=work;if(Nonzero(work))n.lastMovement=work;
    NativePoint scaled=Scale(work,Add(Mul(c.moveSpeed,F(kMotion025)),F(kMotion075)));
    n.direction=Direction(scaled,false);if(c.native1e24==1)n.direction=Direction(n.velocity,true);
    if(n.direction!=-1){volatile float radians=(float)atan2((double)scaled.y,(double)scaled.x);
        n.angle=ApproachAngle(n.angle,Mul(radians,F(kMotionDegrees)));}
    volatile float rawFactor=(float)c.native1580;float factor=Div(Sub(rawFactor,0),F(kMotion60));
    NativePoint previous=n.velocity;float damping;
    if(special){
        damping=(c.flagsLow&0x4000)?1:((c.flagsHigh&0x04000000)?F(kMotion097):Add(Mul(factor,F(kMotion051)),F(kMotion045)));
    }else{
        if(c.flagsLow&0x80)factor=0;
        if(Nonzero(scaled)&&!horizontalOnly){
            float dot=Add(Mul(n.velocity.x,scaled.x),Mul(n.velocity.y,scaled.y));NativePoint projection=Scale(scaled,dot);
            float friction=Root(Add(Mul(factor,F(kMotion020Below)),F(kMotion080)));
            n.velocity=Plus(Scale(Minus(n.velocity,projection),friction),projection);
        }
        previous=n.velocity;float dot=Add(Mul(n.velocity.x,scaled.x),Mul(n.velocity.y,scaled.y));
        float amount=Mul(Sub(Mul(factor,F(kMotion070)),F(kMotion020)),dot);if(amount<=0)amount=0;
        amount=Mul(Add(Div(amount,c.moveSpeed),F(kMotion3)),F(kMotion040));
        float base=1;
        if(!(c.flagsHigh&0x04000000)){
            //The cache is queried again here in the original; do not fold it
            //into the earlier query or silently manufacture an absent effect.
            r=effects.Has(63,c.input.playerToken,&effect63);if(r!=NPI_OK)return Fail(r,0x72b4e0,63,issue);
            base=effect63||c.native1e24>1||special?Add(Mul(factor,F(kMotion051)),F(kMotion045)):Add(Mul(factor,F(kMotion0185)),F(kMotion0775));
        }
        damping=Root(base);
        float acceleration=Add(Mul(Sub(Mul(amount,F(kMotion025)),amount),factor),amount);
        n.velocity=Plus(n.velocity,Scale(Scale(scaled,F(kMotion050)),acceleration));
        float cap=Mul(c.moveSpeed,F(kMotion441));
        if(factor>0&&Length2(n.velocity)>Mul(cap,cap)){
            float length=Length(n.velocity);if(length>0)n.velocity=Scale(n.velocity,Div(Length(previous),length));
        }
    }
    n.velocity.x=Mul(n.velocity.x,damping);n.velocity.y=horizontalOnly?Add(n.velocity.y,F(kMotion0225)):Mul(damping,n.velocity.y);
    bool effect50=false;r=effects.Has(50,c.input.playerToken,&effect50);if(r!=NPI_OK)return Fail(r,0x72b661,50,issue);
    if(effect50){n.velocity=Plus(n.velocity,Scale(previous,F(kMotion005)));
        if(c.native188){if(!Finite(c.native18c)||!Finite(c.native194))return Fail(NPI_BAD_VALUE,0x72b6b2,-1,issue);
            float dot=Add(Mul(c.native18c.x,c.native194.x),Mul(c.native18c.y,c.native194.y));
            NativePoint correction=Scale(Scale(c.native194,dot),F(kMotion080));n.velocity=Minus(c.native18c,Plus(correction,correction));}
    }
    if(!Finite(n.velocity)||!Finite(n.movement)||!Finite(n.lastMovement)||!Finite(n.angle))return Fail(NPI_BAD_VALUE,0x72b71c,-1,issue);
    return Done(output,n,0x72b71c,issue);
}
