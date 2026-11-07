#include "native_room_effects.h"
#include "native_room_state.h"
#include "native_player_state.h"
#include "native_player_manager.h"
#include "native_room_effect_data.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int n){float f;memcpy(&f,&n,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Div(float a,float b){volatile float f=a/b;return f;}
float GlobalFloat(NativeMt& r){volatile float f=(float)r.Next();return Mul(f,F(kEffectGlobalFloatScale));}
unsigned int GlobalDraws(int before,int after){
    if(before<0||before>624||after<0||after>624)return 0xffffffffU;
    return after>=before?(unsigned int)(after-before):(unsigned int)(624-before+after);
}
NativePoint Times(NativePoint p,float f){p.x=Mul(p.x,f);p.y=Mul(p.y,f);return p;}
NativePoint Difference(NativePoint a,NativePoint b){a.x=Sub(a.x,b.x);a.y=Sub(a.y,b.y);return a;}
float Squared(NativePoint p){return Add(Mul(p.x,p.x),Mul(p.y,p.y));}
NativePoint Unit(NativePoint p){volatile float n=(float)sqrt((double)Squared(p));float d=n==0?F(kEffectEpsilon):n;p.x=Div(p.x,d);p.y=Div(p.y,d);return p;}
NativePoint Direction(float degrees){float a=Mul(degrees,F(kEffectRadians));NativePoint p={(float)cos((double)a),(float)sin((double)a)};return p;}
float Angle(float f){if(fabsf(f)>360)f=(float)fmod((double)f,360.0);if(f>180)return Sub(f,360);if(f<-180)return Add(f,360);return f;}
float Turn(float from,float to,float rate){from=Angle(from);to=Angle(to);if(fabsf(Sub(to,from))>180){if(to>from)from=Add(from,360);else to=Add(to,360);}return Angle(Add(from,Mul(Sub(to,from),rate)));}
//5df7b0: wall depth/perimeter coordinates, with exactly the native scalar
//SSE interpolation order. This modifies the wrapped/clamped UV owner too.
NativePoint WallPoint(NativePoint& uv,const NativeRoomSpace& s){
    uv.x=uv.x>-10?uv.x:-10;if(uv.x>40)uv.x=40;
    float h=Sub(s.bottom,s.top),w=Sub(s.right,s.left),hh=Add(h,h),ww=Add(w,w);
    float perimeter=Add(ww,hh);uv.y=(float)fmod((double)uv.y,(double)perimeter);if(uv.y<0)uv.y=Add(uv.y,perimeter);
    float x0,x1,y0,y1,x2,x3,y2,y3,along=uv.y,span;
    if(h>along){
        span=h;x0=Sub(s.left,40);x1=s.left;y0=Sub(s.top,40);y1=s.top;
        x2=x0;x3=s.left;y2=Add(s.bottom,40);y3=s.bottom;
    }else if(Add(w,h)>along){
        along=Sub(along,h);span=w;x0=Sub(s.left,40);x1=s.left;y0=Add(s.bottom,40);y1=s.bottom;
        x2=Add(s.right,40);x3=s.right;y2=y0;y3=s.bottom;
    }else if(Add(hh,w)>along){
        along=Sub(along,Add(w,h));span=h;x0=Add(s.right,40);x1=s.right;y0=Add(s.bottom,40);y1=s.bottom;
        x2=x0;x3=s.right;y2=Sub(s.top,40);y3=s.top;
    }else{
        along=Sub(along,Add(hh,w));span=w;x0=Add(s.right,40);x1=s.right;y0=Sub(s.top,40);y1=s.top;
        x2=Sub(s.left,40);x3=s.left;y2=y0;y3=s.top;
    }
    float depth=Div(uv.x,40),fraction=Div(along,span);
    float ax=Add(Mul(Sub(x1,x0),depth),x0),ay=Add(Mul(Sub(y1,y0),depth),y0);
    float bx=Add(Mul(Sub(x3,x2),depth),x2),by=Add(Mul(Sub(y3,y2),depth),y2);
    NativePoint p={Add(Mul(Sub(bx,ax),fraction),ax),Add(Mul(Sub(by,ay),fraction),ay)};return p;
}
bool EmptyCombatQueries(const NativePlayerManager& manager){
    if(manager.members.size()!=1||manager.worldTypes.size()!=manager.worldMembers.size())return false;
    for(unsigned int i=0;i<manager.worldTypes.size();++i)if(manager.worldTypes[i]!=1&&manager.worldTypes[i]!=1000)return false;
    return true;
}
}
const NativeSpriteAsset* NativePreparedRoomEffectSprite(unsigned int i){return i<sizeof(kRoomEffectSprites)/sizeof(kRoomEffectSprites[0])?kRoomEffectSprites[i]:0;}
const NativeEntityBodyProperties* NativePreparedEffectBody(int v,int sub){
    for(unsigned int i=0;i<sizeof(kRoomEffectBodies)/sizeof(kRoomEffectBodies[0]);++i)
        if(kRoomEffectBodies[i].variant==v&&kRoomEffectBodies[i].subtype==sub)return kRoomEffectBodies+i;
    return 0;
}
NativeRoomEffect::NativeRoomEffect():ready(false),anchored(false),nextAddress(0),updates(0),native398(0),native3a4(-1),native3a8(1),renderDepth(0),native39c(0),native3a0(0),native3b8(0),angle(0),native3c0(1),restoreInitGlobalDraws(0),restorePostGlobalDraws(0){native284.x=native284.y=0;}
bool NativeRoomEffect::Init(int variant,unsigned int seed,const NativePoint& position,int configStage,int backdrop,NativeMt& global){
    nextAddress=0x5aa8d0;
    const NativeEntityBodyProperties* p=NativePreparedEffectBody(variant,0);
    if(ready||!p||configStage!=1||(backdrop!=1&&backdrop!=2))return false;
    NativePoint velocity={0,0};
    if(body.BeginFresh(1000,variant,0,seed,0,1,&position,&velocity)!=NEB_OK)return false;
    if(sprite.Load(p->animationPath)!=NSPR_OK||sprite.Play(sprite.asset->defaultAnimation,true)!=NSPR_OK)return false;
    //5aacad/5aae22: all three reached variants set8, state0 and grid class0.
    body.flagsLow|=8;body.state=0;body.gridCollisionClass=0;body.entityCollisionClass=0;body.maxHitPoints=0;
    if(variant==21)body.state=1;
    if(variant==68){
        renderDepth=5000;
        body.renderDepth=5000;
        native284.y=Mul(GlobalFloat(global),F(kEffectWallCoordinate));
        native284.x=Mul(GlobalFloat(global),F(kEffectGridSize));
    }
    //5aceda/5ad043, then5ad465: backdrop45's recoloring is inapplicable.
    body.collisionDamage=1;body.entityCollisionClass=4;body.native16e=true;
    if(sprite.RefreshSheets()!=NSPR_OK)return false;
    //7eb1f0's original unmodified callback registry has no subscribers.
    nextAddress=0;ready=true;return true;
}
bool NativeRoomEffect::RestoreSaved(int variant,int subtype,const NativePoint& position,int configStage,int backdrop,
    const unsigned int fields[0x60],NativeMt& global){
    nextAddress=0x79d0b0;
    const NativeEntityBodyProperties* p=NativePreparedEffectBody(variant,subtype);
    if(ready||!p||!fields||subtype!=0||(variant!=33&&variant!=68)||configStage!=1||(backdrop!=1&&backdrop!=2))return false;
    unsigned int initial=fields[0x34],current=fields[0x38];
    //The default record sentinels are not stores into body.native18/1c.
    if(!initial||!current||fields[0x18]!=0xffffffffU||fields[0x1c]!=0xffffffffU)return false;
    //79dbc6..de passes saved+34 to6bb0c0, whose virtual+4 constructor is
    //005aa8d0. Keep that factory/Init work, including variant68's two draws;
    //restore does not erase Init merely because common fields are serialized.
    const int beforeInit=global.index;
    if(!Init(variant,initial,position,configStage,backdrop,global))return false;
    restoreInitGlobalDraws=GlobalDraws(beforeInit,global.index);
    if(restoreInitGlobalDraws==0xffffffffU)return false;
    nextAddress=0x79d0b0;
    //79d150 reads +38 and MOVSXes only word+20 into complete+27c. The
    //66f544/674ef3 frame-count owner is separate and is not reconstructed
    //from that serialized word.
    body.native27c=(fields[0x20]&0x8000U)?(int)(fields[0x20]|0xffff0000U):(int)fields[0x20];
    body.position.x=position.x;body.position.y=position.y;body.previousPosition=position;
    body.initialSeed=initial;body.random.state=current;
    nextAddress=0;ready=true;return true;
}
bool NativeRoomEffect::PostRestore(const NativeRoomState& room,const NativeEntityBody& player,
    const NativePlayerState& state,const NativePlayerManager& manager,const NativePlayerEffects& roomEffects,
    NativeMt& global,unsigned int frame){
    if(!ready||!global.Valid())return false;
    const int beforePost=global.index;restorePostGlobalDraws=0;
    if(body.variant==33){
        //79d359 and79d37e draw Y then X;79d3ab/3c3 map them through the
        //actual room bounds loaded from game+18190+14..20.
        float y=GlobalFloat(global),x=GlobalFloat(global);
        body.position.x=Add(Mul(Sub(room.space.right,room.space.left),x),room.space.left);
        body.position.y=Add(Mul(Sub(room.space.bottom,room.space.top),y),room.space.top);
        body.previousPosition=body.position;
    }else if(body.variant==68){
        //79d2f3..2fc invokes vtable+0xc, the reached005b0d50 update path.
        if(!Update(room,player,state,manager,roomEffects,global,frame))return false;
    }else return false;
    restorePostGlobalDraws=GlobalDraws(beforePost,global.index);
    return restorePostGlobalDraws!=0xffffffffU;
}
bool NativeRoomEffect::Update(const NativeRoomState& room,const NativeEntityBody& player,const NativePlayerState& state,const NativePlayerManager& manager,const NativePlayerEffects& roomEffects,NativeMt& global,unsigned int frame){
    nextAddress=0x5b0d50;
    bool held;
    if(!ready||body.removed||anchored||body.native26c||!player.ready||player.type!=1||!EmptyCombatQueries(manager)||
       !state.HasCollectible(232,&held)||held||roomEffects.playerOwner||roomEffects.Count(1,677)!=0)return false;
    //797f00: no item232, SeedEffect77 or room effect677; ordinary mode,
    //challenge0 and room transition1210!=2 leave the initialized factor1.
    body.native2ec=1;
    if(!body.UpdateUnmodified(sprite,0,&nextAddress))return false;
    if(body.variant==21){
        body.appliedFriction=Mul(body.appliedFriction,F(kEffectBugFriction));
        if(native398>0){
            body.AddVelocity(Times(Direction(angle),Add(Mul(GlobalFloat(global),0.5f),0.5f)));--native398;
        }else if(global.Next()%30==0)native398=(int)(global.Next()%120)+15;
        else if(global.Next()%5==0)body.AddVelocity(Times(Direction(angle),Mul(GlobalFloat(global),0.5f)));
        NativePoint away=Difference(body.position,player.position);float distanceSquared=Squared(away);
        if(10000>distanceSquared&&global.Next()%3==0){
            body.AddVelocity(Times(Unit(away),Mul(Sub(1,Div(distanceSquared,10000)),2)));
            native398=(int)(global.Next()%100)+30;
        }
        angle=Angle(Sub(Add(Mul(GlobalFloat(global),F(kEffectTurnScale)),angle),F(kEffectTurnOffset)));
        NativePoint clamped;if(!room.space.Clamp(body.position,-2,-2,-2,-2,&clamped))return false;body.position=clamped;
        int collision;if(!room.collision.AtPosition(room.geometry,body.position,room.space.zeroCollision,&collision))return false;
        if(collision){
            int cell=room.geometry.PositionIndex(body.position.x,body.position.y);NativePoint center;
            if(!room.geometry.Position(cell,&center.x,&center.y))return false;
            NativePoint delta=Difference(center,body.position);
            if(fabsf(delta.x)>fabsf(delta.y)){body.position.x=delta.x>0?Sub(center.x,F(kEffectBugWallDistance)):Add(center.x,F(kEffectBugWallDistance));body.velocity.x=Mul(body.velocity.x,F(kEffectBugWallVelocity));}
            else {body.position.y=delta.y>0?Sub(center.y,F(kEffectBugWallDistance)):Add(center.y,F(kEffectBugWallDistance));body.velocity.y=Mul(body.velocity.y,F(kEffectBugWallVelocity));}
            //5cb1c2: only a native pit(type7) needs the extra bounce/random
            //response. The reached initial factory has walls/doors only.
            if(room.collision.occupied[cell]&&room.collision.types[cell]==7){nextAddress=0x5cb1c8;return false;}
        }
        native284=body.position;
        if(100>distanceSquared&&Squared(player.velocity)>1&&player.gridCollisionClass!=0&&player.gridCollisionClass!=1&&player.gridCollisionClass!=2&&player.gridCollisionClass!=3)body.removed=true;
        //5cb3bc: no registered type2 actors in the radius1 spatial query.
    }else if(body.variant==33){
        NativePoint delta=Difference(player.position,body.position),direction=Unit(delta);
        volatile float distance=(float)sqrt((double)Squared(delta));
        body.appliedFriction=Mul(body.appliedFriction,F(kEffectFlyFriction));
        if(global.Next()%5==0)body.AddVelocity(Times(direction,Mul(GlobalFloat(global),F(kEffectHalf))));
        //6721d0(period5,entityToken): no pause at initialization. This is a
        //frame/token cadence, not a collision test.
        if((frame+body.entityToken)%5==0){
            float force=Mul(GlobalFloat(global),F(kEffectFlyRandomForce));
            float angleRadians=Mul(GlobalFloat(global),F(kEffectPi));
            //647cc0 draws one angle, multiplies by PI, then doubles it.
            angleRadians=Add(angleRadians,angleRadians);
            NativePoint randomDirection={(float)cos((double)angleRadians),(float)sin((double)angleRadians)};
            body.AddVelocity(Times(randomDirection,force));
        }
        if(F(kEffectFlyDistance)>distance){
            float amount=Mul(Sub(1,Div(Mul(distance,distance),F(kEffectFlySquaredDistance))),F(kEffectFlyForce));
            body.AddVelocity(Times(direction,amount));
        }
        //5cba42..5cbf65: type2 spatial query and type8/33 identity queries
        //have no matches in this actual player/effects registry.
        sprite.flipX=Sub(body.position.x,native284.x)<0;
        NativePoint clamped;if(!room.space.Clamp(body.position,2,2,2,2,&clamped))return false;body.position=clamped;native284=body.position;
    }else if(body.variant==68){
        body.velocity.x=body.velocity.y=0;NativePoint old=native284;
        angle=Angle(Sub(Add(Mul(GlobalFloat(global),F(kEffectTurnScale)),angle),F(kEffectTurnOffset)));
        if(native398>0){
            NativePoint step=Times(Direction(angle),Add(GlobalFloat(global),1));native284.x=Add(native284.x,step.x);native284.y=Add(native284.y,step.y);--native398;
        }else if(global.Next()%30==0)native398=(int)(global.Next()%120)+15;
        else if(global.Next()%5==0){NativePoint step=Times(Direction(angle),GlobalFloat(global));native284.x=Add(native284.x,step.x);native284.y=Add(native284.y,step.y);}
        float previousDepth=native284.x;body.position=WallPoint(native284,room.space);
        if(previousDepth!=native284.x){float turn=previousDepth>0?(angle>0?90.0f:-90.0f):(angle>0?-90.0f:90.0f);angle=Turn(angle,Add(angle,turn),F(kEffectWallTurnRate));}
        int collision;if(!room.collision.AtPosition(room.geometry,body.position,room.space.zeroCollision,&collision))return false;
        if(collision==5){
            float turn=native284.y>old.y?(angle>90?90.0f:-90.0f):(-90>angle?90.0f:-90.0f);
            angle=Turn(angle,Add(angle,turn),F(kEffectWallTurnRate));native284=old;body.position=WallPoint(native284,room.space);
        }
        if(!room.collision.AtPosition(room.geometry,body.position,room.space.zeroCollision,&collision))return false;
        if(collision==5)native284.y=Add(native284.y,2);
        //5cf973: no registered projectiles in the type2 spatial query.
    }else {nextAddress=0x5cacbc;return false;}
    if(native3a4>0&&!--native3a4)body.removed=true;
    body.native264=body.position;
    //7eb410: original post-effect-update callback list is empty.
    ++updates;nextAddress=0;return true;
}
NativeRoomEffects::~NativeRoomEffects(){for(unsigned int i=0;i<entities.size();++i)delete entities[i];}
bool NativeRoomEffects::Spawn(int variant,unsigned int seed,const NativePoint& position,int stage,int backdrop,NativeMt& global,NativePlayerManager& manager,unsigned int* next){
    NativeRoomEffect* effect=new NativeRoomEffect;
    if(!effect->Init(variant,seed,position,stage,backdrop,global)){if(next)*next=effect->nextAddress;delete effect;return false;}
    if(!manager.RegisterEntity(effect->body)){delete effect;return false;}
    entities.push_back(effect);return true;
}
bool NativeRoomEffects::RestoreSaved(const unsigned int fields[0x60],NativeRoomState& room,
    const NativeEntityBody& player,const NativePlayerState& state,int configStage,int backdrop,
    NativePlayerManager& manager,NativeMt& global,unsigned int frame,unsigned int* next){
    if(!fields){if(next)*next=0x79daf0;return false;}
    int type=(int)fields[0x00],variant=(int)fields[0x04],subtype=(int)fields[0x08];
    if(type!=1000||(variant!=33&&variant!=68)||subtype!=0){if(next)*next=0x79daf0;return false;}
    NativePoint position;memcpy(&position.x,&fields[0x28],4);memcpy(&position.y,&fields[0x2c],4);
    NativeRoomEffect* effect=new NativeRoomEffect;
    if(!effect->RestoreSaved(variant,subtype,position,configStage,backdrop,fields,global)){
        if(next)*next=effect->nextAddress;delete effect;return false;}
    if(!manager.RegisterEntity(effect->body)){delete effect;if(next)*next=0x79dc45;return false;}
    if(!effect->PostRestore(room,player,state,manager,roomEffects,global,frame)){
        if(next)*next=effect->nextAddress;delete effect;return false;}
    entities.push_back(effect);return true;
}
