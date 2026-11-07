#include "native_player_collision.h"
#include "game_session.h"
#include <math.h>
namespace {
float Add(float a,float b){volatile float n=a+b;return n;}
float Sub(float a,float b){volatile float n=a-b;return n;}
float Mul(float a,float b){volatile float n=a*b;return n;}
float Div(float a,float b){volatile float n=a/b;return n;}
float Root(float a){volatile float n=(float)sqrt((double)a);return n;}
float Length(NativePoint p){return Root(Add(Mul(p.x,p.x),Mul(p.y,p.y)));}
//67a7e0 wraps79e1f0 with special type14 shape/door predicates. The actual
//room has walls and closed normal/curse doors; other reached grid branches
//remain explicit dependencies rather than an assumed collision class.
bool At(const PortNormalRuntime& runtime,NativePoint p,int* value,unsigned int* next){
    const NativeRoomState& room=*runtime.world.Current();
    if(!room.collision.AtPosition(room.geometry,p,room.space.zeroCollision,value))return false;
    const int cell=room.geometry.PositionIndex(p.x,p.y);
    if(cell>=0&&room.collision.occupied[cell]){
        if(*value==3&&room.collision.types[cell]==14){*next=0x67a883;return false;}
        if(*value==4&&room.collision.types[cell]==16){
            const NativeDoorSet* doors=runtime.world.resident[runtime.world.currentList].doors;
            if(!doors)return false;
            for(int slot=0;slot<8;++slot)if((doors->attached&(1U<<slot))&&doors->actors[slot].grid==cell){
                if(doors->actors[slot].flags&16U){*next=0x67aa1c;return false;}break;
            }
        }
    }
    //67aa54: class5 is passable to this genuine ordinary primary; ghost,
    //variant1 and secondary twin predicates were excluded by the caller.
    return true;
}
}
bool NativePlayerGridCollision(PortNormalRuntime& runtime,bool positionOnly,unsigned int* next){
    if(!next)return false;*next=0x6712a0;
    NativeEntityBody& body=runtime.playerBody;const NativePlayerState& player=runtime.playerState;
    NativeRoomState* room=runtime.world.Current();
    if(!body.ready||body.type!=1||body.variant||player.playerType<0||player.ghost||!room)return false;
    if(!positionOnly){body.gridContact=false;body.gridContactKnown=true;}
    if(!body.gridCollisionClass||body.collision.points.empty()||body.childToken){*next=0;return true;}
    //The actual post-generation cache owns class5. No flying/other-class
    //arithmetic is approximated by the grounded response.
    if(body.gridCollisionClass!=5||room->space.zeroCollision){*next=0x6713c3;return false;}
    NativePoint clamped;if(!room->space.Clamp(body.position,0,0,0,0,&clamped))return false;
    int collision;
    if(!At(runtime,body.position,&collision,next))return false;
    if(room->geometry.PositionIndex(body.position.x,body.position.y)<0||collision==4){
        if(!positionOnly)body.gridContact=body.position.x!=clamped.x||body.position.y!=clamped.y;
        if(room->type!=16)body.position=clamped;
    }
    NativePoint normal={0,0};
    for(unsigned int sample=0;sample<body.collision.points.size();++sample){
        const NativePoint& offset=body.collision.points[sample];
        NativePoint p={Add(body.position.x,offset.x),Add(offset.y,body.position.y)};
        int cell=room->geometry.PositionIndex(p.x,p.y);if(cell<0)continue;
        if(!At(runtime,p,&collision,next))return false;
        if(!positionOnly){
            //The native call queries this predicate for each valid sample,
            //including empty cells; preserve its position in the loop.
            bool effect=player.effects.Has(1,625);
            if(effect&&room->collision.occupied[cell]){*next=0x67166b;return false;}
            //7734f0 is the grid-destruction transaction. Only a crush effect
            //(Leo302, Thunder Thighs314, Mega Mush625) may reach it; an
            //ordinary ground or flying player treats an intact class2/3 grid
            //as solid. Leaving the crush call explicit keeps every no-crush
            //run deterministic and auditable.
            if(collision==2||collision==3){
                const bool crush=effect||player.effects.Has(1,302)||player.effects.Has(1,314);
                if(crush){*next=0x671869;return false;}
                normal.x=Add(normal.x,offset.x);normal.y=Add(normal.y,offset.y);
                continue;
            }
            if(collision==4&&room->collision.occupied[cell]&&room->collision.types[cell]==16){
                unsigned int use;if(!runtime.playerCreation.constructorFields.Scalar(0x1524,&use))return false;
                if(use==147){*next=0x6718e6;return false;}
            }
        }
        if(collision!=0&&collision!=5){normal.x=Add(normal.x,offset.x);normal.y=Add(normal.y,offset.y);}
    }
    float square=Add(Mul(normal.x,normal.x),Mul(normal.y,normal.y));
    if(square>=0.001f){
        float length=Root(square);if(length>0){float inverse=Div(1,length);normal.x=Mul(inverse,normal.x);normal.y=Mul(inverse,normal.y);}
        if(!positionOnly)body.contactNormal=normal;
        float impact=Sub(Mul(-normal.x,body.velocity.x),Mul(normal.y,body.velocity.y));
        if(impact<0&&!positionOnly){
            body.gridContact=true;body.contactVelocity=body.velocity;
            impact=Sub(Mul(impact,0.9f),0.1f);
            body.velocity.y=Add(Mul(impact,body.contactNormal.y),body.velocity.y);
            body.velocity.x=Add(Mul(impact,body.contactNormal.x),body.velocity.x);
        }
        const float speed=Length(body.velocity);
        NativePoint ellipse={Mul(normal.x,body.collision.xMultiplier),Mul(normal.y,body.collision.yMultiplier)};
        float high=Mul(Length(ellipse),body.collision.radius),radius=high;
        float low=Sub(high,speed>2?speed:2);
        for(int iteration=0;iteration<12;++iteration){
            const float middle=Add(Mul(Sub(high,low),0.5f),low);
            NativePoint p={Add(Mul(normal.x,middle),body.position.x),Add(Mul(normal.y,middle),body.position.y)};
            if(!At(runtime,p,&collision,next))return false;
            if(collision==0||collision==5)low=middle;else high=middle;
        }
        body.position.x=Add(Mul(normal.x,Sub(low,radius)),body.position.x);
        body.position.y=Add(Mul(normal.y,Sub(low,radius)),body.position.y);
    }else if(!positionOnly){body.contactNormal.x=body.contactNormal.y=0;}
    *next=0;return true;
}
