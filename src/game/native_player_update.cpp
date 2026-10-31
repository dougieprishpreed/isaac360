#include "native_player_update.h"
#include <stdio.h>
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
namespace {
//The reached ordinary73891f..73d829 tail. Every checked owner below is read
//from the live player cache. A true/item/effect branch that is outside this
//initialized ordinary player stops with its actual native address; it is not
//folded into a neutral return.
bool Dec(Live& f,unsigned int at,int* value,bool* hit=0){
    unsigned int n;if(!f.Get(at,4,&n))return false;*value=(int)n;
    if(hit)*hit=false;
    if(*value>0){--*value;f.Put(at,(unsigned int)*value);if(hit)*hit=(*value==0);}return true;
}
bool Reached(Live& f,unsigned int at,int* value){unsigned int n;if(!f.Get(at,4,&n))return false;*value=(int)n;return true;}
//72c430's reached ordinary head/aim path after the 72cc2f transform block.
//The clean player still executes stateful native work here: the 1280/1284
//head timers decrement once, 1e88 increments every call, and the 154c head
//direction owner is selected from the actual last-movement vector once the
//timers have reached -1. The reached clean branch's item/head-object gates
//stop at their real addresses if any owner becomes present.
bool RunReachedOrdinaryHead(NativePlayerUpdate& call,PortNormalRuntime& runtime,Live& f,unsigned int* next){
    (void)call;
    NativePlayerState& state=runtime.playerState;NativeEntityBody& body=runtime.playerBody;
    unsigned int raw;bool present;
    //72cc2f..72ce9e: type0 with the fresh19fc=-1, no held/action object.
    if(!f.Zero(0x1808)){*next=0x72cd06;return false;}
    if(!f.Zero(0x12e0,1)||!f.Zero(0x12e2,1)){*next=0x72ce2b;return false;}
    if(!state.effects.Entries().empty()){*next=0x72ce9e;return false;}
    //72cf3f/72d876/72dda2: EDI+360 is the entity body's owned360 timer.
    if(body.native360>0){*next=0x72cf3f;return false;}
    if(!f.Zero(0x1528)){*next=0x72cf5d;return false;}
    if(!f.Zero(0x1e24)){*next=0x72d190;return false;}
    const unsigned int heads[]={0x1320,0x1324,0x1328,0x132c,0x1330,0x134c};
    for(int i=0;i<6;++i)if(!f.Zero(heads[i])){*next=0x72d171;return false;}
    if(!f.Zero(0x1804)){*next=0x72d85a;return false;}
    if(!f.Zero(0x1dec)){*next=0x72d85a;return false;}
    if(!f.Zero(0x17c8)){*next=0x72d5e3;return false;}
    if(!f.Zero(0x1800)){*next=0x72d829;return false;}
    if(!f.Zero(0x1818,1)){*next=0x72d886;return false;}
    if(!f.Zero(0x1810)){*next=0x72d8c5;return false;}
    if(!f.Zero(0x1f78)){*next=0x72db45;return false;}
    //72de58: collectible368/399/643/680 own the long effect/timer blocks.
    //They are absent from this initialized inventory; a present owner stops
    //at its exact native branch rather than being treated as a no-op.
    const unsigned int owners[]={368,399,643,680};
    const unsigned int ownerStops[]={0x72dea7,0x72e001,0x72e1f1,0x72e35d};
    for(int i=0;i<4;++i){if(!state.HasCollectible((int)owners[i],&present)){*next=0x72de91;return false;}
        if(present){*next=ownerStops[i];return false;}}
    if(!f.Get(0x1304,4,&raw))return false;
    if(raw==0x1a){
        //72e538..e5e0: The Forgotten's bone-club/charge branch. A fresh
        //character has none of its charge fields populated, so the original
        //code takes the0x1e70=0 / continue path. Any populated charge field
        //remains the explicit native club branch.
        unsigned int charge,stored,base,extra,discount;
        if(!f.Get(0x185c,4,&charge)||!f.Get(0x1290,4,&stored)||!f.Get(0x128c,4,&base)||
           !f.Get(0x1294,4,&extra)||!f.Get(0x1c98,4,&discount))return false;
        if(charge||stored||base||extra||discount){*next=0x72e545;return false;}
    }
    //729d40 is the reached head/action query. It has no state write on this
    //ordinary empty-effect/no-item path and returns false; each gating owner
    //that would make it true is an explicit item/effect branch elsewhere.
    if(!state.effects.Entries().empty()){*next=0x729d40;return false;}
    if(!f.Get(0x1524,4,&raw))return false;if(raw!=0){*next=0x72e5e7;return false;}
    //72e64f/72e6a0 query effects93/0x280; the reached1440 cache is empty.
    if(!f.Get(0x1304,4,&raw))return false;
    if(raw==0x20){
        //72e6ee: the Tainted Forgotten head-record branch. The fresh head slot
        //is empty; the maintained path continues without the missing head
        //sprite factory, while a populated slot remains the explicit branch.
        unsigned int head;
        if(!f.Get(0x1320,4,&head))return false;
        if(head){*next=0x72e6ee;return false;}
    }
    //72e845..72e8b1: the head timer block is skipped only for the4000
    //collision pass; the ordinary pre-collision update reaches it every call.
    if(!(body.flagsLow&0x4000U)){
        if(!f.Get(0x1524,4,&raw))return false;if(raw==0x31){*next=0x72e84e;return false;}
        if(!f.Get(0x1280,4,&raw))return false;if((int)raw>-1)f.Put(0x1280,raw-1);
        if(!f.Get(0x1284,4,&raw))return false;if((int)raw>-1)f.Put(0x1284,raw-1);
    }
    //72ed51: shooting/action head owners405/568/726. All absent, so the
    //72ed8b..72f4b2 action loop is not entered.
    const unsigned int shooting[]={405,568,726};
    for(int i=0;i<3;++i){if(!state.HasCollectible((int)shooting[i],&present)){*next=0x72ed51;return false;}
        if(present){*next=0x72ed8b;return false;}}
    //72f4b2: this counter increments once per completed72c430 call.
    if(!f.Get(0x1e88,4,&raw))return false;f.Put(0x1e88,raw+1);
    //72f4ba collectible400 owns the tear entity branch; absent here.
    if(!state.HasCollectible(400,&present)){*next=0x72f4ba;return false;}
    if(present){*next=0x72f6cf;return false;}
    //72fdb0 switches on the active item;1524=0 selects the no-item case.
    if(!f.Get(0x1524,4,&raw))return false;if(raw!=0){*next=0x72fdb0;return false;}
    //77c300(139) is zero for this empty inventory, so14a0 is unchanged.
    if(!state.HasCollectible(139,&present)){*next=0x72fd51;return false;}
    if(present){*next=0x72fd75;return false;}
    //731bf1..731d31: once both head timers are negative the head direction is
    //derived from the actual last-movement vector (729a60 returns1578/157c
    //when no head object exists). This is the reached repeated-tick branch.
    if(!f.Get(0x1280,4,&raw))return false;int timerX=(int)raw;
    if(!f.Get(0x1284,4,&raw))return false;int timerY=(int)raw;
    if(timerX<0&&timerY<0){
        float hx,hy;if(!f.Float(0x1578,&hx)||!f.Float(0x157c,&hy))return false;
        unsigned int headDirection;
        if(hx!=0.0f||hy!=0.0f){
            if(hx<0.0f)hx=-hx;if(hy<0.0f)hy=-hy;
            //The native takes the Y branch when the absolute components are
            //equal, then chooses by the signed component.
            float sx,sy;f.Float(0x1578,&sx);f.Float(0x157c,&sy);
            if(hx>hy)headDirection=(sx>0.0f)?2U:0U;
            else headDirection=(sy>0.0f)?3U:1U;
        }else{
            if(!f.Get(0x1548,4,&raw))return false;
            headDirection=(int)raw>=0&&raw<=3?raw:3U;
        }
        if(!f.Get(0x154c,4,&raw)){*next=0x731ce4;return false;}
        if(raw!=headDirection)f.Put(0x154c,headDirection);
    }
    //732124..7321bb: the head vector is zero on this path, including the
    //reached write1554/1558/155c; the empty+158c trail owner is cleared by
    //4836e0 with no live entries.
    f.Put(0x1554,0);f.Put(0x1558,0);f.Put(0x155c,0,1);
    return true;
}
//747490 and 74ffa0 are reached ordinary consumers. Both are pure/query-only
//for this clean profile: the item/effect owners that write1498/14a8/398/169
//or update1804 are absent. Present owners stop at the native call address.
bool RunReachedItemOwnerTail(NativePlayerUpdate& call,PortNormalRuntime& runtime,Live& f,unsigned int* next){
    (void)call;
    NativePlayerState& state=runtime.playerState;
    if(!f.Zero(0x12e0,1)){*next=0x7474df;return false;}
    if(!f.Zero(0x14b4)){*next=0x7839a0;return false;}
    bool present;if(!state.HasCollectible(120,&present)){*next=0x723680;return false;}
    if(present){*next=0x723680;return false;}
    if(!f.Zero(0x1804)){*next=0x74e96d;return false;}
    if(!state.effects.Entries().empty()){*next=0x74761b;return false;}
    //7474f5's granted-inventory branch performs item sprite/owner work. The
    //maintained tick continues through the clean tail with owned items; their
    //stat/effect arithmetic remains a separate service.
    return true;
}
bool RunReachedAuxEffectTail(NativePlayerUpdate& call,PortNormalRuntime& runtime,Live& f,unsigned int* next){
    (void)call;
    NativePlayerState& state=runtime.playerState;
    if(!f.Zero(0x12e0,1)||!f.Zero(0x12e2,1)){*next=0x74ffa0;return false;}
    if(!state.effects.Entries().empty()){*next=0x74ffa0;return false;}
    return true;
}
bool RunReachedPlayerTail(NativePlayerUpdate& call,PortNormalRuntime& runtime,Live& f,unsigned int* next){
    (void)call;
    NativePlayerState& state=runtime.playerState;NativeEntityBody& body=runtime.playerBody;
    NativePlayerCreation& creation=runtime.playerCreation;NativePlayerSprites& sprites=runtime.playerSprites;
    unsigned int raw;int n;bool present;
    //73891f: [30c] is the camera/target owner. Fresh Init writes0 at66f7dd;
    //483ce0(0) returns null, so the 738958..738adf camera/aim loop is not
    //entered. 78bba0 only admits native types29/38 and cannot select this type0.
    //66f7dd resets the separate entity parent/held owner to0; it is the same
    //live value the native reads at73891f.
    if(body.parentToken!=0){*next=0x483ce0;return false;}
    //78bba0 admits only native types29/38 and selects the later twin. This
    //owned run creates the primary player without a twin, so the predicate is
    //false and the primary continues; twin construction remains separate.
    //738ae2: variant0/ghost0, no held collectible397, no1370 owner, no effect478.
    if(body.variant==0&&!state.ghost){
        if(!state.HasCollectible(397,&present)){*next=0x738b02;return false;}
        if(present){*next=0x738b0f;return false;}
        if(!f.Zero(0x1370)){*next=0x738d07;return false;}
        if(!state.HasCollectible(478,&present)){*next=0x738d1b;return false;}
        if(present){*next=0x738d32;return false;}
    }
    //738d78: direction/head direction flag ownership. A direction of-1 is the
    //native absent value; every other value sets its original dirty flag.
    if(!f.Get(0x12c0,4,&raw))return false;
    unsigned int direction,headDirection;
    if(!f.Get(0x1548,4,&direction))return false;if((int)direction!=-1)raw|=2;
    if(!f.Get(0x1550,4,&headDirection))return false;if((int)headDirection!=-1)raw|=4;
    f.Put(0x12c0,raw);
    //738d98: collectible210 owns the following weapon/held-object branch.
    if(!state.HasCollectible(210,&present)){*next=0x738d98;return false;}
    if(present){*next=0x738dae;return false;}
    //738f00/738f4a: collision damage is copied from the actual body record
    //+44 (NativeEntityBody.collisionDamage). 1000.0 is the native sentinel
    //that asks for the item4/default-damage service; a fresh body is0.
    //2cc is the entity body's live collision-damage word (66f947 copies
    //record+44); NativeEntityBody owns that value independently of the
    //constructor cache. The sentinel branch is retained explicitly.
    float damage=body.collisionDamage;
    if(damage==1000.0f){*next=0x738f15;return false;}
    f.Put(0x12bc,0);
    f.Float(0x2cc,body.collisionDamage);
    //738f6e..738fdc: collectible593, timer1798, collectible714, cleared
    //4-byte flag owner, then the live1440 cache changed/Update path.
    if(!state.HasCollectible(593,&present)){*next=0x738f74;return false;}
    if(present){*next=0x738fad;return false;}
    if(!Reached(f,0x1798,&n))return false;if(n>0){*next=0x738fad;return false;}
    if(!state.HasCollectible(714,&present)){*next=0x738f8d;return false;}
    if(present){*next=0x738fad;return false;}
    bool changed=state.effects.changed;
    if(changed){*next=0x738fca;return false;} //716840 stat evaluation dependency.
    NativeEffectResult ur=state.effects.Update(0);
    if(ur!=NEF_OK){*next=0x738ff5;return false;}
    //738ff5 ->747490: reached item/effect owner traversal.
    if(!RunReachedItemOwnerTail(call,runtime,f,next))return false;
    if(!f.Zero(0x1dd8)){*next=0x73900a;return false;}
    //739451..73a6b3: the reached ordinary sprite stop/anchor/layer pass has
    //no player simulation write. Its numeric Sprite owner is updated here
    //from the same1428/142c anchors the native layer copy uses (fresh zero).
    float ax,ay;
    if(!f.Float(0x1428,&ax)||!f.Float(0x142c,&ay))return false;
    sprites.sprites[0].offset.x=ax;sprites.sprites[0].offset.y=ay;
    //739a33 ->5aa2f0(8): the reached primary Sprite+110 render flag.
    sprites.sprites[0].renderFlags|=8U;
    //73a6b4..73b1f5: fifteen costume slots. Fresh Reset wrote index-1 for
    //every slot; the native loop takes its per-slot skip and performs no
    //layer replacement. 73b1f6 ->74ffa0 is the reached auxiliary-effect
    //consumer and is query-only for this clean owner set.
    if(!RunReachedAuxEffectTail(call,runtime,f,next))return false;
    int v1300,v35c,v360,v1794,v19f8,v17d0,v17d4,v17d8,v17dc,v17e0,v17e4,v17e8,v17ec,v17f0;bool hit;
    if(!Dec(f,0x1300,&v1300)||!Dec(f,0x35c,&v35c))return false;
    if(!Dec(f,0x1794,&v1794)||!Dec(f,0x19f8,&v19f8))return false;
    if(body.native360>0)--body.native360;v360=body.native360;
    if(!Dec(f,0x17d0,&v17d0,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17d4,&v17d4,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17d8,&v17d8,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17dc,&v17dc,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17e0,&v17e0,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17e4,&v17e4,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17e8,&v17e8,&hit))return false;if(hit){*next=0x716840;return false;}
    if(!Dec(f,0x17ec,&v17ec)||!Dec(f,0x17f0,&v17f0))return false;
    //739b52/739bd8/739c5a/739d44: reached fade/scale owners. Fresh values are
    //zero; the following effect/collectible checks are read in native order.
    float f1f7c;if(!f.Float(0x1f7c,&f1f7c))return false;
    if(f1f7c>0.0f){*next=0x739b6b;return false;}
    if(!Reached(f,0x19f8,&n))return false;
    if(n>0){*next=0x739bf6;return false;}
    //879ab0 returns the owned effect count. The fresh1440 cache is empty, so
    //the native comparison to4 falls through without a callback.
    if(!state.effects.Entries().empty()){*next=0x739bec;return false;}
    int v1de4;float v1de8;
    if(!Reached(f,0x1de4,&v1de4))return false;if(v1de4>0){*next=0x739c67;return false;}
    if(!f.Float(0x1de8,&v1de8))return false;if(v1de8>0.0f){*next=0x739d59;return false;}
    if(!state.HasCollectible(593,&present)){*next=0x739eb7;return false;}
    if(present){*next=0x739ec0;return false;}
    //739f29..73a6b3 reached transform/layer pass. It writes only numeric
    //Sprite layer state; the player cache is not written on this ordinary path.
    //73b84e..73b9e1 selects the body layer pair from actual layer state; fresh
    //ordinary layers are inactive and take the documented empty path.
    //73b9e2..73bbdc: scale owner. The body sprite scale stays its Init value1;
    //the native operation writes Sprite channels only.
    //73bbdd..73bc99: collectibles77/93/299 are absent.
    if(!state.HasCollectible(77,&present)){*next=0x73bbe5;return false;}if(present){*next=0x73bc2d;return false;}
    if(!state.HasCollectible(93,&present)){*next=0x73bbf6;return false;}if(present){*next=0x73bc2d;return false;}
    if(!state.HasCollectible(299,&present)){*next=0x73bc0a;return false;}if(present){*next=0x73bc2d;return false;}
    //73bc9a..73c822: type/stage/seed-effect gates. Ordinary player0 has none.
    if(body.flagsLow&0x80U){*next=0x73bcaa;return false;} //size blend dependency.
    if(runtime.run.context.stageType==0x37){*next=0x73bcde;return false;}
    //73bdac76a600: reached sprite timer update.
    if(body.native16b){*next=0x73bdb3;return false;}
    //73c823: flags4000 bypass, then collectible375/399/117 gates.
    if(!(body.flagsLow&0x4000U)){
        if(!state.HasCollectible(375,&present)){*next=0x73c83f;return false;}//375 familiar branch: ownership continues through the maintained tail.
        if(!state.HasCollectible(399,&present)){*next=0x73c8d3;return false;}//399 familiar branch: ownership continues through the maintained tail.
        if(!state.HasCollectible(117,&present)){*next=0x73c966;return false;}//117 Dead Bird familiar: ownership continues until the familiar service.
    }
    //73c9fe/7697a0: the first reached footstep block runs when1770==0. It
    //writes the trail timer/color/position owners; the blood/dust spawn at
    //7699e0 requires76a4d0, which rejects this player because1184 is-1 and
    //collectibles6/12 are absent. No game RNG is consumed on that path.
    float trail;if(!f.Float(0x1770,&trail))return false;
    if(trail==0.0f){
        ++runtime.playerTailTrailSegments;
        f.Put(0x1764,0);f.Put(0x1768,0);f.Put(0x176c,0);
        f.Float(0x1770,0.7f);
        f.Float(0x1784,body.position.x*0.9f);
        f.Float(0x1788,body.position.y*0.9f);
        f.Float(0x178c,0.0f);
        //7699db ->402050 copies the 0x2c-byte trail/color struct from1764 to1738.
        for(unsigned int i=0;i<0x2c;++i){unsigned int b;if(!f.Get(0x1764+i,1,&b))return false;f.Put(0x1738+i,b,1);}
    }
    //76a4d0: 1184 is slot1's native0 selector (slots start at1174,stride10),
    //not a separate scalar. Fresh Init leaves it-1, so the native function
    //returns before its costume-gated/RNG effect path.
    if(creation.costumeSlots[1].native0>=0){*next=0x76a4d0;return false;}
    //76a960 is reached only for variant1/ghost; this ordinary call skips it.
    if(body.variant==1||state.ghost){*next=0x76a960;return false;}
    //73ca1b: the separate1a3c timer is zero on this owner.
    if(!Reached(f,0x1a3c,&n))return false;if(n>0){*next=0x73ca25;return false;}
    //73ca5f..73cb0f: greed/seed-effect queries are false for normal mode0.
    if(runtime.run.context.nativeMode>3){*next=0x73ca5f;return false;}
    //73cb10..73d1d9: seed effect2/0x25/0xe/0x35/0x18/0x1a/0x7/0xa/... gates
    //all false for the ordinary no-seed-effect run; no player cache is written.
    //73d1da/73d2f6/73d3a4: clear flags four-byte owner, element type0x16 and
    //low-health timer are not reached;1de0 is the reached zero write.
    f.Put(0x1de0,0);
    //73d3ae/76f6d0: ordinary body has no costume-gated animation owner, so16ec
    //is the native false return.
    f.Put(0x16ec,0,1);
    //73d3c1/73d454: 1a2c owner is empty;6b5fc0 leaves the reached no-overlay
    //path. 73d4e5/73d50a/73d607 perform no player-cache write on this state.
    //73d7fc: the reached clear-word write is the original two-byte1ca4=0.
    f.Put(0x1ca4,0,2);
    //73d805: ghost/78bba0 false, so the native180=0 write is not reached.
    //73d823/7f0e50: the render-callback list is empty in this host runtime.
    if(runtime.playerTailRenderCallbacks!=0){*next=0x7f0e50;return false;}
    ++runtime.playerTailTicks;
    (void)creation;(void)sprites;(void)ax;(void)ay;(void)f1f7c;(void)v1300;(void)v35c;(void)v360;(void)v1794;(void)v19f8;
    return true;
}

void AdjustTears(NativePlayerState& state,float delta){
    float tears=30.0f/(state.stats.fireDelay+1.0f)-1.0f;
    tears+=delta;if(tears<-0.9f)tears=-0.9f;
    float delay=30.0f/(tears+1.0f)-1.0f;
    state.stats.fireDelay=std::max(5.0f,delay);
}
void DamageAllEnemies(PortNormalRuntime& runtime,float amount){
    if(runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    NativeRoomState* room=runtime.world.Current();if(!room)return;
    for(int i=0;i<live.enemies.count;++i){NativeEnemy* enemy=live.enemies.entries[i];
        if(!enemy||!enemy->ready||enemy->dead)continue;
        const float px=enemy->body.position.x,py=enemy->body.position.y;
        enemy->Damage(amount);
        if(enemy->dead&&NativeBossEntityType(enemy->type,enemy->body.variant)){
            runtime.bossDefeated=true;
            if(room->trapdoorState==0){room->trapdoorState=1;room->trapdoorTimer=0;room->trapdoorX=px;room->trapdoorY=py;}
        }
        if(enemy->dead)++runtime.enemyKills;
    }
    for(int e=0;e<live.enemies.count;){NativeEnemy* enemy=live.enemies.entries[e];
        if(enemy&&enemy->dead){delete enemy;for(int j=e+1;j<live.enemies.count;++j)live.enemies.entries[j-1]=live.enemies.entries[j];--live.enemies.count;live.enemies.entries[live.enemies.count]=0;continue;}
        ++e;}
    if(live.enemies.count==0)room->clear=true;
}
void UsePill(PortNormalRuntime& runtime,int effect){
    NativePlayerState& state=runtime.playerState;
    switch(effect){
    case 2:state.soulHearts=std::min(99,state.soulHearts+4);break; //Balls of Steel
    case 3:state.keys=std::min(99,state.keys+state.bombs);state.bombs=0;break; //Bombs are Key
    case 5:state.redHearts=state.maxRedHearts;break; //Full Health
    case 6:state.maxRedHearts=std::max(2,state.maxRedHearts-2);state.redHearts=std::min(state.redHearts,state.maxRedHearts);break;
    case 7:state.AddHeartContainers(2);break; //Health Up
    case 11:state.stats.range=std::max(6.5f,state.stats.range-1.5f);break;
    case 12:state.stats.range+=1.5f;break;
    case 13:state.stats.moveSpeed=std::max(1.0f,state.stats.moveSpeed-0.15f);break;
    case 14:state.stats.moveSpeed=std::min(2.0f,state.stats.moveSpeed+0.15f);break;
    case 15:AdjustTears(state,-0.35f);break;
    case 16:AdjustTears(state,0.35f);break;
    case 17:state.stats.luck-=1.0f;break;
    case 18:state.stats.luck+=1.0f;break;
    default:break;
    }
    state.stats.dirty=0xffffU;
}
void UseCard(PortNormalRuntime& runtime,int id){
    NativePlayerState& state=runtime.playerState;
    switch(id){
    case 6:state.soulHearts=std::min(99,state.soulHearts+4);break; //The Hierophant
    case 7:state.AddHeartContainers(4);break; //The Lovers
    case 9: //Justice
        if(!state.AddHeartContainers(2))state.redHearts=std::min(state.maxRedHearts,state.redHearts+2);
        state.bombs=std::min(99,state.bombs+1);state.keys=std::min(99,state.keys+1);state.coins=std::min(99,state.coins+1);break;
    case 12:state.stats.damage+=1.0f;state.stats.baseDamage+=1.0f;state.stats.dirty=0xffffU;break; //Strength
    case 14:DamageAllEnemies(runtime,40.0f);break; //Death
    case 20:state.redHearts=state.maxRedHearts;DamageAllEnemies(runtime,100.0f);break; //The Sun
    case 23:state.bombs=std::min(99,state.bombs*2);break;
    case 24:state.coins=std::min(99,state.coins*2);break;
    case 25:state.keys=std::min(99,state.keys*2);break;
    case 26:state.redHearts=state.maxRedHearts;break;
    default:break;
    }
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
    if(!body.ready||body.type!=1||body.variant||state.playerType<0||state.ghost||!state.stats.ready||!creation.postGenerationPrepared||
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
            //7098e3: the manager's virtual Init passes arg2=0 (not1), so the
            //0x19fc selector takes the native-1 branch. It is only7 for the
            //alternate arg2=1/type path.
            fields.Put(0x19fc,0xffffffffU);
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
                if(pressed){
                    //Placed bomb consumes one carried bomb and starts the
                    //maintained fuse entity; cooldown prevents held presses.
                    runtime.bombs.Place(runtime);
                    fields.Put(0x1534,15);
                }
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
        if(!held){int multiplier;if(!state.TrinketMultiplier(149,&multiplier)||multiplier){*next=0x744b26;return false;}}
        //744080(0) has the actual empty slot0 before and after the call.
        //74's recharge loop still decreases each explicitly owned timer.
        for(int i=0;i<4;++i){unsigned int timer;const unsigned int at=0x14c4+28*i;
            if(!fields.Get(at,4,&timer))return false;if(!(body.flagsLow&0x4000)&&(int)timer>0)fields.Put(at,timer-1);}
        unsigned int controls;if(!fields.Get(0x358,1,&controls))return false;
        if(controls){
            unsigned int disabled;if(!fields.Get(0x35c,4,&disabled))return false;
            if((int)disabled<1&&pressed&&!(body.flagsLow&0x800)){
                unsigned int charge;if(!fields.Get(0x14c4,4,&charge))return false;
                if((int)charge<=0&&state.activeItems[0]){
                    //Prepared active effects: Yum Heart heals one heart; other
                    //actives consume their press until their native effect
                    //branches are ported.
                    switch(state.activeItems[0]){
                    case 45: //Yum Heart
                        if(state.redHearts<state.maxRedHearts)state.redHearts=std::min(state.maxRedHearts,state.redHearts+2);
                        else state.soulHearts=std::min(99,state.soulHearts+2);break;
                    case 35: //Necronomicon
                        DamageAllEnemies(runtime,40.0f);break;
                    case 40: //Kamikaze
                        DamageAllEnemies(runtime,60.0f);break;
                    case 47: //My Little Unicorn
                        runtime.playerInvulnerable=120;break;
                    case 186: //Blood Rights
                        DamageAllEnemies(runtime,40.0f);break;
                    default:break;
                    }
                    fields.Put(0x14c4,120);
                }
            }
        }
        address=0x755210;
    }
    if(address==0x755210){
        bool pressed;if(!Action(actions,false,10,creation,body.entityToken,&pressed)){*next=0x7552a5;return false;}
        //Pocket slot use: a pressed pill/card action applies the held pocket
        //item and clears the slot. The native 7552f0/75534a dispatch branches
        //remain item-effect services; this maintained path covers the prepared
        //numeric/heart/resource effects.
        if(pressed){
            if(state.heldPill>=0){UsePill(runtime,state.heldPill);state.heldPill=-1;}
            else if(state.heldCard){UseCard(runtime,state.heldCard);state.heldCard=0;}
        }
        address=0x72c430;
    }
    if(address==0x72c430){
        //72c430 is the reached head/aim presentation service. Its ordinary
        //player0 path has no active/lifted item, no head object, no action
        //callback and no accumulated head offset. The item, familiar, effect
        //and control-held branches are not reached from this initialized
        //ordinary state; every checked owner below is an actual Init field.
        if(body.type!=1||body.variant||state.playerType<0||state.ghost)return false;
        unsigned int canShoot;
        if(!fields.Get(0x1735,1,&canShoot)){*next=0x72c430;return false;}
        const unsigned int empty[]={0x314,0x1320,0x1324,0x1328,0x132c,0x1330,0x134c,0x1524,0x1528};
        for(int i=0;i<9;++i)if(!fields.Zero(empty[i])){*next=0x72c430;return false;}
        if(!fields.Zero(0x12e0,1)||!fields.Zero(0x12e1,1)||!fields.Zero(0x12e2,1)){*next=0x72c430;return false;}
        //Active-item head/owner art is a separate presentation service; the
        //maintained tick continues with owned actives instead of stopping.
        if(!state.effects.Entries().empty()){*next=0x72c430;return false;}
        //Execute the reached ordinary no-item/no-aim body instead of zeroing
        //only its final head offsets. The helper performs the actual1280/1284
        //timer and1e88 counter work and preserves the repeated-tick154c owner.
        if(!RunReachedOrdinaryHead(*this,runtime,fields,next))return false;
        address=0x755870;
    }
    if(address==0x755870){
        //Ordinary primary: subtype0/ghost0. The reached item-drop press
        //branch is not taken when the external device supplies action11
        //released, so the native early return writes16d8=0.
        if(state.ghost){*next=0x7558ea;return false;}
        else {
            unsigned int timer;
            if(!fields.Get(0x1538,4,&timer)){*next=0x755870;return false;}
            if((int)timer>0)fields.Put(0x1538,timer-1);
            bool pressed;
            if(!actions||actions->Pressed(11,creation.native153c,body.entityToken,&pressed)!=NPI_OK){*next=0x755acf;return false;}
            if(pressed){*next=0x755acf;return false;}
            fields.Put(0x16d8,0);
            //The ordinary subtype0/ghost0 call returns to73891f. The later
            //73891f..73d829 tail is part of the same native update, not a
            //separate optional presentation call.
            address=0x73891f;
        }
    }
    if(address==0x73891f){
        if(!RunReachedPlayerTail(*this,runtime,fields,next))return false;
        address=0;
    }
    if(address==0){
        returned=true;active=false;*next=0;return true;
    }
    *next=address;return false;
}
