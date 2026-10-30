#include "native_player_update.h"
#include "game_session.h"
#include "native_room_effects.h"
#include "native_room_update.h"
#include <algorithm>
#include <string.h>
namespace {
//Read only bytes actually written by the constructor/Init or this call.
//An absent source byte is a dependency, never an implicit neutral value.
class Live {
    NativePlayerUpdate& call;NativePlayerCreationCache& fields;unsigned int* next;
    Live& operator=(const Live&);
public:
    Live(NativePlayerUpdate& c,NativePlayerCreationCache& f,unsigned int* issue):call(c),fields(f),next(issue){}
    bool Get(unsigned int at,unsigned int count,unsigned int* value){
        unsigned int n=0;for(unsigned int i=0;i<count;++i){unsigned char b;
            if(!fields.Byte(at+i,&b)){call.missingOffset=at+i;*next=call.address;return false;}n|=(unsigned int)b<<(8*i);}
        *value=n;return true;
    }
    bool Zero(unsigned int at,unsigned int count=4){unsigned int n;return Get(at,count,&n)&&n==0;}
    bool Float(unsigned int at,float* value){unsigned int n;if(!Get(at,4,&n))return false;memcpy(value,&n,4);return true;}
    void Put(unsigned int at,unsigned int n,unsigned int count=4){fields.AssignBytes(at,n,count);}
    void Float(unsigned int at,float value){unsigned int n;memcpy(&n,&value,4);Put(at,n);}
};
class MovementEffects:public NativeMovementEffects {
    const NativePlayerState& state;const NativeEntityBody& body;
    MovementEffects& operator=(const MovementEffects&);
public:
    MovementEffects(const NativePlayerState& s,const NativeEntityBody& b):state(s),body(b){}
    NativePlayerInputResult Has(int id,unsigned int owner,bool* value){
        if(owner!=body.entityToken||!value)return NPI_BAD_REQUEST;
        //72b054/4e0/661 call879800 (null effects63/50). The later
        //93/77/298/299/58/313 predicates call879770 (collectible effects).
        *value=state.effects.Has(id==63||id==50?0:1,id);
        if(id==50&&*value&&!body.gridContactKnown)return NPI_SOURCE_MISSING;
        return NPI_OK;
    }
    NativePlayerInputResult Collectible(int id,unsigned int owner,bool* value){return owner==body.entityToken&&state.HasCollectible(id,value)?NPI_OK:NPI_SOURCE_MISSING;}
};
bool Absent(const NativePlayerState& state,int id){bool value;return state.HasCollectible(id,&value)&&!value;}
bool Action(NativeActionSource* source,bool held,int id,const NativePlayerCreation& creation,unsigned int token,bool* value){
    if(!source)return false;return (held?source->Held(id,creation.native153c,token,value):source->Pressed(id,creation.native153c,token,value))==NPI_OK;
}
}
void NativePlayerUpdate::Begin(){
    address=0x736d70;missingOffset=0;active=false;returned=false;
}
bool NativePlayerUpdate::Resume(PortNormalRuntime& runtime,NativeActionSource* actions,unsigned int* next){
    if(!next)return false;*next=address;missingOffset=0;
    NativeEntityBody& body=runtime.playerBody;NativePlayerState& state=runtime.playerState;
    NativePlayerCreation& creation=runtime.playerCreation;NativePlayerSprites& sprites=runtime.playerSprites;
    NativeWorldResident& room=runtime.world.resident[runtime.world.currentList];
    Live fields(*this,creation.constructorFields,next);
    unsigned int transitionA,transitionB;
    if(!body.ready||body.type!=1||body.variant||state.playerType||state.ghost||!state.stats.ready||!creation.postGenerationPrepared||
       !runtime.world.Current()||!room.effects||!room.update||runtime.run.context.challenge)return false;
    if(!runtime.history.fields.Get(0x1b470,4,&transitionA)||!runtime.history.fields.Get(0x1b7bc,4,&transitionB)||transitionA||transitionB){*next=0x7372e9;return false;}
    //This is the ordinary registered primary. The actual type0 selection
    //excludes29/38's twin lifecycle, type35's vertical motion and variant2.
    if(address==0x736d70){
        active=true;
        //736fc5..7370f6: the reached first live tick after post-generation
        //Init. The genuine 1734 owner is already true, so this is not a
        //second76c990 call. The native block still clears the four head
        //selectors, enables the primary controls byte and clears the
        //action/costume callbacks before the update services below.
        if(!firstTick){
            if(!state.native1734||!creation.postGenerationPrepared)return false;
            fields.Put(0x1304,state.playerType); //7098a0; ordinary player0.
            fields.Put(0x19fc,0); //7098d2: player0's ESI value0, not a fixture.
            fields.Put(0x1735,state.canShoot?1U:0U,1);
            fields.Put(0x314,0); //66ec42/66f7f1/708cc8 clear the held-object owner.
            for(int i=0;i<4;++i)fields.Put(0x1334+4*i,0);
            fields.Put(0x14a8,0xffffU);
            firstTick=true;
        }
        if(body.parentToken||creation.native1fac){*next=0x736ddf;return false;}
        if(!fields.Zero(0x1d88)){*next=0x736e53;return false;}
        if(!(body.flagsLow&0x4000)){
            unsigned int effect,previous;
            if(!fields.Get(0x1ca4,2,&effect)||!fields.Get(0x1ca6,2,&previous))return false;
            if(effect!=previous){*next=0x7372d1;return false;}
            //41d260/5e1950 return distinct transition-state owners. Both
            //are checked above;1eb4 was cleared by the reached Init.
            if(body.native168&&!fields.Zero(0x1eb4,1)){*next=0x737321;return false;}
            unsigned int reflected;if(!fields.Get(0x1f14,1,&reflected))return false;
            const NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
            if(!descriptor||((descriptor->flags>>13)&1)!=reflected){*next=0x7373ad;return false;}
        }
        fields.Put(0x1ce8,0,1);
        if(!body.native16a||body.removed){*next=0x7375b7;return false;}
        if(!fields.Zero(0x1264,1)){*next=0x7375ef;return false;}
        const unsigned int linked[]={0x137c,0x1380,0x1d80,0x1d84,0x1d88};
        for(int i=0;i<5;++i)if(!fields.Zero(linked[i])){*next=0x73760b;return false;}
        //70b540 ->797f00: entitytype1 bypasses item232 and SeedEffect77;
        //the owned room effect677 is absent.7a037a initialized7120 to0.
        if(room.effects->roomEffects.Has(1,677)||room.update->native7120){*next=0x70b54e;return false;}
        body.native2ec=1;
        float z;if(!fields.Float(0x1d78,&z))return false;
        if((body.flagsHigh&0x02000000U)==0&&(body.native2a0<0||z<0)){*next=0x7376cc;return false;}
        if(body.flagsHigh&0x02000000U)fields.Float(0x1d78,0.0f);
        if(!(body.flagsLow&0x4000)){
            if(!Absent(state,632)){*next=0x73792b;return false;}
            //Native18/1c is the player-local flag owner, distinct from the
            //entity160/164 flags. Its8000 branch owns the effect removals.
            if((body.native18&0x8000U)||!fields.Zero(0x1d9c)){*next=0x73796d;return false;}
            body.flagsLow&=~0x40U;
        }
        address=0x6740a0;
    }
    if(address==0x6740a0){
        if(!body.UpdateUnmodified(sprites.sprites[0],0,next))return false;
        address=0x737e76;
    }
    if(address==0x737e76){
        if(!(body.flagsLow&0x4000)){
            if(sprites.sprites[1].Update()!=NSPR_OK){*next=0x737e92;return false;}
            //b276f0 is the body animation's addressed event. An event is
            //not discarded merely because its emitter is not yet ported.
            if(sprites.sprites[0].mainState.events||sprites.sprites[0].overlayState.events){*next=0x737e9f;return false;}
        }
        address=0x737f6f;
    }
    if(address==0x737f6f){
        bool connected;
        if(!actions||actions->Connected(creation.native153c,&connected)!=NPI_OK){*next=0x737f7a;return false;}
        if(!connected){*next=0x737fa7;return false;}
        //Both action/costume sprites were cleared by75808d/773ba0. These
        //flags are inspected from their real Init bytes, not assumed false.
        if(!fields.Zero(0x12e0,1)||!fields.Zero(0x12e1,1)){*next=0x738016;return false;}
        if(!(body.flagsLow&0x4000)&&!Absent(state,641)){*next=0x7381fe;return false;}
        if(!fields.Zero(0x12e2,1)){*next=0x738534;return false;}
        //76a6b0's pit/JumpOut state0 returns immediately.784f20 has no
        //leap owner/timer and consequently performs no writes on this path.
        if(body.native34c){*next=0x76a6b0;return false;}
        if(!(body.flagsLow&0x4000)&&(!fields.Zero(0x1e24)||!fields.Zero(0x1e28))){*next=0x784f20;return false;}
        if(!(body.flagsLow&0x4000)&&!fields.Zero(0x12fe,1)){*next=0x7385e5;return false;}
        if(!fields.Zero(0x14d0)){*next=0x73861c;return false;}
        if(!Absent(state,59)){*next=0x7386c6;return false;}
        //Normal numeric seed has no SeedEffect52 registration.
        unsigned int ice;if(!fields.Get(0x1580,4,&ice))return false;
        fields.Put(0x1580,(body.flagsLow&0x08000000U)?60:((int)ice>0?ice-1:0));
        fields.Put(0x12c0,0);
        if(!fields.Zero(0x1350)){*next=0x73887d;return false;}
        address=0x72ae20;
    }
    if(address==0x72ae20){
        NativePlayerVelocityContext c;memset(&c,0,sizeof(c));
        c.input.playerType=state.playerType;c.input.controller=creation.native153c;c.input.playerToken=body.entityToken;
        c.input.native1540=creation.native1540;c.input.stage=runtime.run.context.stage;c.input.stageType=runtime.run.context.stageType;
        c.input.dimension=runtime.world.layout.currentDimension;
        //This entry is dimension0. The reflected dimension1 predicate
        //does not read level flags; do not substitute game run flags.
        if(c.input.dimension!=0){*next=0x72ae20;return false;}
        unsigned int n;float f;
#define WORD(at,target) if(!fields.Get(at,4,&n))return false;target=(int)n
#define REAL(at,target) if(!fields.Float(at,&f))return false;target=f
#define BYTE(at,target) if(!fields.Get(at,1,&n))return false;target=n!=0
        BYTE(0x358,c.input.controlsEnabled);
        //72aef0 reads35c only if358 is true. New Run6b4be5 disabled
        //controls; do not demand or define an unqueried cooldown byte.
        if(c.input.controlsEnabled){WORD(0x35c,c.input.controlsCooldown);}
        c.flagsLow=body.flagsLow;c.flagsHigh=body.flagsHigh;c.native2a0=body.native2a0;c.native2ec=body.native2ec;
        c.position=body.position;c.moveSpeed=state.stats.moveSpeed;
        REAL(0x1d78,c.native1d78);WORD(0x1580,c.native1580);WORD(0x1798,c.native1798);WORD(0x1e24,c.native1e24);
        c.native14a4=state.stats.flying;BYTE(0x1ef4,c.native1ef4);c.native1f80=state.ghost;
        if(!fields.Zero(0x13e8)){*next=0x72ae73;return false;}
        //Do not query effect50 here:72b661 must query it after both63
        //queries. Its adapter rejects an unwritten188 only if50 is present.
        if(body.gridContactKnown){c.native188=body.gridContact;if(body.gridContact){c.native18c=body.contactVelocity;c.native194=body.contactNormal;}}
        NativePlayerVelocityState velocity;velocity.velocity=body.velocity;
        //72aec4 overwrites1584/1588 before any read on this path.
        velocity.previousVelocity=body.velocity;
        REAL(0x1568,velocity.movement.x);REAL(0x156c,velocity.movement.y);
        REAL(0x1578,velocity.lastMovement.x);REAL(0x157c,velocity.lastMovement.y);
        WORD(0x1548,velocity.direction);REAL(0x19f4,velocity.angle);
        //Type0 never reads/writes16d8 in72ae20. Its initial native bytes
        //are unwritten until755870; this unused component member must not
        //be exported back into the player's storage.
        velocity.native16d8=0;WORD(0x1dc4,velocity.native1dc4);
        WORD(0x1ddc,velocity.native1ddc);WORD(0x1d94,velocity.native1d94);
#undef WORD
#undef REAL
#undef BYTE
        if(!actions){*next=0x72ae20;return false;}
        MovementEffects effects(state,body);NativePlayerVelocityIssue issue;
        NativePlayerVelocityResult r=NativePlayerVelocityCore(c,*actions,effects,&velocity,&issue);
        if(r!=NPV_RETURNED){*next=issue.nextAddress;return false;}
        body.velocity=velocity.velocity;
        fields.Float(0x1584,velocity.previousVelocity.x);fields.Float(0x1588,velocity.previousVelocity.y);
        fields.Float(0x1568,velocity.movement.x);fields.Float(0x156c,velocity.movement.y);
        fields.Float(0x1578,velocity.lastMovement.x);fields.Float(0x157c,velocity.lastMovement.y);
        fields.Put(0x1548,velocity.direction);fields.Float(0x19f4,velocity.angle);
        fields.Put(0x1dc4,velocity.native1dc4);
        fields.Put(0x1ddc,velocity.native1ddc);fields.Put(0x1d94,velocity.native1d94);
        address=0x733ef0;
    }
    if(address==0x733ef0){
        //733ef0 is Bomb, not the shooting service.4edc30 reads the same
        //controls byte/cooldown as the live movement owner.
        unsigned int cooldown,controls;
        if(!fields.Get(0x1534,4,&cooldown)||!fields.Get(0x358,1,&controls))return false;
        if((int)cooldown>0){if(!(body.flagsLow&0x4000))fields.Put(0x1534,cooldown-1);}
        else if(controls){
            unsigned int disabled;if(!fields.Get(0x35c,4,&disabled))return false;
            if((int)disabled<1&&!(body.flagsLow&0x800)){
                bool pressed;if(!Action(actions,false,8,creation,body.entityToken,&pressed)){*next=0x733fda;return false;}
                if(pressed){*next=0x733ef0;return false;}
            }
        }
        address=0x7449b0;
    }
    //The active-item and pill/card services follow movement and Bomb even
    //when controls are disabled. They may query device edges on that path.
    if(address==0x7449b0){
        bool held,pressed;
        if(!Action(actions,true,9,creation,body.entityToken,&held)||!Action(actions,false,9,creation,body.entityToken,&pressed)){*next=0x744a80;return false;}
        if(!fields.Zero(0x1528)||!fields.Zero(0x1524)||!fields.Zero(0x1e24)){*next=0x744ae9;return false;}
        for(int i=0;i<4;++i)if(state.activeItems[i]){*next=0x744b0e;return false;}
        if(!held){int multiplier;if(!state.TrinketMultiplier(149,&multiplier)||multiplier){*next=0x744b26;return false;}}
        //744080(0) has the actual empty slot0 before and after the call.
        //74's recharge loop still decreases each explicitly owned timer.
        for(int i=0;i<4;++i){unsigned int timer;const unsigned int at=0x14c4+28*i;
            if(!fields.Get(at,4,&timer))return false;if(!(body.flagsLow&0x4000)&&(int)timer>0)fields.Put(at,timer-1);}
        unsigned int controls;if(!fields.Get(0x358,1,&controls))return false;
        if(controls){unsigned int disabled;if(!fields.Get(0x35c,4,&disabled))return false;
            if((int)disabled<1&&pressed&&!(body.flagsLow&0x800)){*next=0x7452ec;return false;}}
        address=0x755210;
    }
    if(address==0x755210){
        bool pressed;if(!Action(actions,false,10,creation,body.entityToken,&pressed)){*next=0x7552a5;return false;}
        unsigned int kind,controls;
        if(!fields.Get(0x16bc,4,&kind)||!fields.Get(0x358,1,&controls))return false;
        unsigned int item=0;if(kind==2&&!fields.Get(0x16b8,4,&item))return false;
        if(kind==2&&(int)item>0){*next=0x7552f0;return false;}
        if(controls){unsigned int cooldown;if(!fields.Get(0x35c,4,&cooldown))return false;
            if((int)cooldown<1&&pressed&&!(body.flagsLow&0x800)){*next=0x75534a;return false;}}
        address=0x72c430;
    }
    if(address==0x72c430){
        //72c430 is the reached head/aim presentation service. Its ordinary
        //player0 path has no active/lifted item, no head object, no action
        //callback and no accumulated head offset. The item, familiar, effect
        //and control-held branches are not reached from this initialized
        //ordinary state; every checked owner below is an actual Init field.
        if(body.type!=1||body.variant||body.subtype||state.playerType||state.ghost||!state.canShoot)return false;
        unsigned int canShoot;
        if(!fields.Get(0x1735,1,&canShoot)){*next=0x72c430;return false;}
        if(canShoot!=1){*next=0x72c430;return false;}
        const unsigned int empty[]={0x314,0x1320,0x1324,0x1328,0x132c,0x1330,0x134c,0x1524,0x1528};
        for(int i=0;i<9;++i)if(!fields.Zero(empty[i])){*next=0x72c430;return false;}
        if(!fields.Zero(0x12e0,1)||!fields.Zero(0x12e1,1)||!fields.Zero(0x12e2,1)){*next=0x72c430;return false;}
        for(int i=0;i<4;++i)if(state.activeItems[i]){*next=0x72c430;return false;}
        if(!state.effects.Entries().empty()){*next=0x72c430;return false;}
        //The no-item/no-aim path leaves the local head vector at its global
        //zero owner.1554/1558 are the native head offsets,155c the flag.
        fields.Put(0x1554,0);fields.Put(0x1558,0);fields.Put(0x155c,0,1);
        address=0x755870;
    }
    if(address==0x755870){
        //Ordinary primary: subtype0/ghost0. The reached item-drop press
        //branch is not taken when the external device supplies action11
        //released, so the native early return writes16d8=0.
        if(body.subtype||state.ghost){address=0;}
        else {
            unsigned int timer;
            if(!fields.Get(0x1538,4,&timer)){*next=0x755870;return false;}
            if((int)timer>0)fields.Put(0x1538,timer-1);
            bool pressed;
            if(!actions||actions->Pressed(11,creation.native153c,body.entityToken,&pressed)!=NPI_OK){*next=0x755acf;return false;}
            if(pressed){*next=0x755acf;return false;}
            fields.Put(0x16d8,0);
            address=0;
        }
    }
    if(address==0){
        returned=true;active=false;*next=0;return true;
    }
    *next=address;return false;
}
