#include "native_player_stats.h"
#include "native_player_state.h"
#include "native_player_creation.h"
#include "native_player_manager.h"
#include "native_player_stat_constants.h"
#include "native_item_cache.h"
#include <algorithm>
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int b){float f;memcpy(&f,&b,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Div(float a,float b){volatile float f=a/b;return f;}
float Root(float a){volatile float f=(float)sqrt((double)a);return f;}
float Pow(float a,int n){volatile float f=(float)pow((double)a,(double)n);return f;}
enum FreshQueryKind {FQ_HAS_ITEM,FQ_ITEM_COUNT,FQ_HAS_TRINKET,FQ_TRINKET_COUNT,FQ_WEAPON,FQ_FORM,
 FQ_CARD_EFFECT,FQ_ITEM_EFFECT,FQ_NULL_EFFECT,FQ_TRINKET_EFFECT,FQ_ITEM_EFFECT_COUNT,FQ_NULL_EFFECT_COUNT,
 FQ_FAMILIAR,FQ_CALLBACK,FQ_PILL_COUNT,FQ_SEED_EFFECT};
struct FreshQuery {unsigned int pc;FreshQueryKind kind;int id;};
const FreshQuery queries[]={
#include "native_player_fresh_queries.inc"
};
bool Zero(const std::vector<int>& values){for(unsigned int i=0;i<values.size();++i)if(values[i])return false;return true;}
bool Zero(const std::vector<unsigned char>& values){for(unsigned int i=0;i<values.size();++i)if(values[i])return false;return true;}
void White(float* color){for(int i=0;i<11;++i)color[i]=i<4?1.0f:0;}
bool InitialScalar(const NativePlayerCreation& creation,unsigned int offset,unsigned int expected){
 unsigned int value;return creation.constructorFields.Scalar(offset,&value)&&value==expected;
}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(8*i))&255))*16777619U;}
void Scalar(unsigned int& h,float n){unsigned int v;memcpy(&v,&n,4);Fold(h,v);}
}
NativePlayerStats::NativePlayerStats():ready(false),flying(false),dirty(0),fireDelay(0),shotSpeed(0),damage(0),baseDamage(0),tearHeight(0),fallingSpeed(0),fallingAcceleration(0),range(0),moveSpeed(0),luck(0),native17c0(0),native17c4(0){
 memset(weapons,0,sizeof(weapons));memset(tearFlags,0,sizeof(tearFlags));memset(colors,0,sizeof(colors));memset(size,0,sizeof(size));
 cacheBaseReady=false;cacheBaseFireDelay=cacheBaseShotSpeed=cacheBaseRange=cacheBaseMoveSpeed=cacheBaseLuck=0;
 cacheBaseTearHeight=cacheBaseFallingAcceleration=0;
 bonusTears=bonusFireRate=bonusDamage=bonusRange=bonusMoveSpeed=bonusShotSpeed=bonusLuck=0;
}
bool NativePlayerStats::EvaluateFresh(NativePlayerState& state,const NativePlayerCreation& creation,NativePlayerManager& manager,const NativePlayerStartup& startup,NativeEntityBody& body,NativePlayerSprites& sprites,unsigned int* next){
 if(next)*next=0x716840;
 //This is the reached initial empty-inventory branch, not a substitute for
 //later cache evaluation. Its facts come from the owners populated by Init.
 //Any grant/effect/form/pill change must enter its native arithmetic branch.
 if(ready||!state.resourcesReady||body.type!=1||body.variant||!body.native168||!body.native16a||body.removed||
    !creation.prefixPrepared||!creation.weapon.ready||!sprites.bodyReady||!manager.initialized||!startup.ready||
    !Zero(state.collectibleCounts)||!Zero(state.trinketCounts)||!Zero(state.pillEffectCounts)||!state.effects.Entries().empty()||
    !state.collectibleOverrides.empty()||state.temporaryCollectible||state.transformationFlags||!state.pendingCollectibles.empty())return false;
 for(int i=0;i<4;++i)if(state.activeItems[i])return false;
 for(int i=0;i<15;++i)if(state.formCounts[i])return false;
 //Explicit native scalar writes, including each of the three independent
 //multipliers. Do not infer neutral multipliers from zero allocated storage.
 if(!InitialScalar(creation,0x1850,0x3f800000)||!InitialScalar(creation,0x1854,0x3f800000)||!InitialScalar(creation,0x1858,0x3f800000)||
    !InitialScalar(creation,0x1ee0,0)||!InitialScalar(creation,0x1edc,0)||!InitialScalar(creation,0x1ee4,0)||
    !InitialScalar(creation,0x1ee8,0)||!InitialScalar(creation,0x1eec,0)||!InitialScalar(creation,0x1ef0,0))return false;
 NativePlayerStats n;n.dirty=0xffff;
 //716e76/717114 selects the already present ordinary tears weapon. Its
 //identity and initialized owner survive; there is no factory or RNG replay.
 n.weapons[1]=true;
 for(unsigned int i=0;i<sizeof(queries)/sizeof(queries[0]);++i){
    const FreshQuery& q=queries[i];if(next)*next=q.pc;bool has=false;int value=0;
    switch(q.kind){
    case FQ_HAS_ITEM:if(!state.HasCollectible(q.id,&has)||has)return false;break;
    case FQ_ITEM_COUNT:if(q.id<0||(unsigned int)q.id>=state.collectibleCounts.size()||state.collectibleCounts[q.id])return false;break;
    case FQ_HAS_TRINKET:case FQ_TRINKET_COUNT:
        if(!state.TrinketMultiplier(q.id,&value)||value)return false;break;
    case FQ_PILL_COUNT:if(q.id<0||(unsigned int)q.id>=state.pillEffectCounts.size()||state.pillEffectCounts[q.id])return false;break;
    case FQ_WEAPON:if(q.id<0||q.id>=16||n.weapons[q.id])return false;break;
    case FQ_FORM:if(q.id<0||q.id>=15||state.formCounts[q.id]>=3)return false;break;
    case FQ_CARD_EFFECT:if(!state.effects.Entries().empty())return false;break;
    case FQ_ITEM_EFFECT:has=state.effects.Has(1,q.id);if(has)return false;break;
    case FQ_TRINKET_EFFECT:has=state.effects.Has(2,q.id);if(has)return false;break;
    case FQ_NULL_EFFECT:has=state.effects.Has(0,q.id);if(has)return false;break;
    case FQ_ITEM_EFFECT_COUNT:value=state.effects.Count(1,q.id);if(value)return false;break;
    case FQ_NULL_EFFECT_COUNT:value=state.effects.Count(0,q.id);if(value)return false;break;
    case FQ_SEED_EFFECT:break; //Normal numeric-seed New Run has no SeedEffect registrations.
    case FQ_FAMILIAR:if(!manager.ReconcileNoFamiliars(q.id,0,startup))return false;break;
    case FQ_CALLBACK:
        //The absent modifier branches above reduce to these native base
        //expressions. Only their zero operands are folded, not output fixtures.
        switch(q.id){
        case 0x100:break;
        case 2:{
            float term=Root(std::max(0.0f,Add(Mul(0,F(kStatTearsCoefficient)),1)));
            n.fireDelay=std::max(5.0f,Sub(Sub(F(kStatTearsBase),Mul(term,F(kStatTearsScale))),Mul(0,F(kStatTearsScale))));
            float rate=Div(F(kStatThirty),Add(n.fireDelay,1));
            n.fireDelay=Sub(Div(F(kStatThirty),Mul(rate,1)),1);break;}
        case 1:n.damage=Mul(Root(std::max(0.0f,Add(Mul(0,F(kStatDamageCoefficient)),1))),F(kStatDamageBase));
            n.damage=Mul(Add(F(kStatIsaacDamageOffset),1),n.damage);n.baseDamage=n.damage;break;
        case 4:n.shotSpeed=1;break;
        case 8:n.range=F(kStatRangeBase);n.tearHeight=F(kStatHeightBase);n.fallingSpeed=n.fallingAcceleration=0;break;
        case 16:n.moveSpeed=Add(1,F(kStatIsaacSpeedOffset));break;
        case 32:memset(n.tearFlags,0,sizeof(n.tearFlags));break;
        case 64:White(n.colors[0]);White(n.colors[1]);break;
        case 128:n.flying=false;body.gridCollisionClass=5;break;
        case 512:break; //All147 zero desired counts were reconciled above.
        case 1024:n.luck=0;break;
        case 2048:
            for(int j=0;j<3;++j)n.size[j].x=n.size[j].y=1;break;
        case 4096:White(n.colors[2]);break;
        default:return false;
        }
        if(!manager.EmptyCacheCallback(q.id))return false;break;
    }
 }
 //722080 virtual70b4f0 copies the complete body color to the body Sprite.
 for(int i=0;i<11;++i)sprites.sprites[0].color[i]=n.colors[2][i];
 manager.roomCacheDirty=true; //722460 ->67c7c0, room+7778, even before entry.
 n.cacheBaseFireDelay=n.fireDelay;n.cacheBaseShotSpeed=n.shotSpeed;n.cacheBaseRange=n.range;
 n.cacheBaseMoveSpeed=n.moveSpeed;n.cacheBaseLuck=n.luck;n.cacheBaseTearHeight=n.tearHeight;
 n.cacheBaseFallingAcceleration=n.fallingAcceleration;n.cacheBaseReady=true;
 n.FinalizeDerived();
 n.dirty=0;n.ready=true;Swap(n);if(next)*next=0;return true;
}
void NativePlayerStats::FinalizeDerived(){
 //The final clamp/derive block of00716840, shared by the fresh evaluation
 //and every cache recompute.
 moveSpeed=std::max(F(kStatMinSpeed),std::min(F(kStatMaxSpeed),moveSpeed));
 shotSpeed=std::max(shotSpeed,F(kStatMinShotSpeed));
 tearHeight=std::min(tearHeight,-5.0f);range=std::max(range,F(kStatMinRange));
 damage=std::max(damage,F(kStatMinDamage));fireDelay=std::max(fireDelay,F(kStatMinFireDelay));
 //646730: original pow(double,double)->float boundary and scalar operation
 //order. Falling speed is derived from evaluated range/shot speed/height.
 float time=Div(range,Mul(shotSpeed,10));
 volatile float power=(float)pow((double)F(kStatRangeDecay),(double)time);
 float d=std::max(Mul(Sub(power,1),F(kStatRangeDecayScale)),F(kStatRangeDecayMinimum));
 float speed=Add(Mul(fallingAcceleration,10),1);
 fallingSpeed=-Div(Sub(Sub(-5,tearHeight),Mul(speed,Sub(time,d))),d);
}
bool NativePlayerStats::RecomputeCache(const NativePlayerState& state){
 if(!ready||!cacheBaseReady)return false;
 float tears=Div(F(kStatThirty),Add(cacheBaseFireDelay,1));
 tears=Sub(tears,1);
 float fireAdd=bonusFireRate,fireMul=1;
 float damageAdd=0,damageMul=1,sharedDamage15=false;
 float rangeAdd=0,rangeMul=1,speedAdd=0,speedMul=1,shotAdd=0,shotMul=1,luckAdd=0;
 int flight=flying?1:0;
 for(unsigned int id=0;id<state.collectibleCounts.size();++id){
  const int count=state.collectibleCounts[id];if(count<=0)continue;
  const NativeItemCacheEffect e=NativeItemCacheEffectFor((int)id);
  const float n=(float)count;
  tears=Mul(Add(tears,Mul(e.tearsAdd,n)),Pow(e.tearsMul,count));
  fireAdd=Add(fireAdd,Mul(e.fireRateAdd,n));
  fireMul=Mul(fireMul,Pow(e.fireRateMul,count));
  damageAdd=Add(damageAdd,Mul(e.damageAdd,n));
  if(e.damageMul==1.5f)sharedDamage15=true;
  else damageMul=Mul(damageMul,Pow(e.damageMul,count));
  rangeAdd=Add(rangeAdd,Mul(e.rangeAdd,n));
  rangeMul=Mul(rangeMul,Pow(e.rangeMul,count));
  speedAdd=Add(speedAdd,Mul(e.speedAdd,n));
  speedMul=Mul(speedMul,Pow(e.speedMul,count));
  shotAdd=Add(shotAdd,Mul(e.shotSpeedAdd,n));
  shotMul=Mul(shotMul,Pow(e.shotSpeedMul,count));
  luckAdd=Add(luckAdd,Mul(e.luckAdd,n));
  if(e.flying>=0)flight=e.flying?1:0;
 }
 //Cricket's Head, Magic Mushroom and the Blood of the Martyr/Book of Belial
 //pair share one1.5x damage multiplier instead of stacking.
 if(sharedDamage15)damageMul=Mul(damageMul,1.5f);
 //Conditional cache owners. The hit counter is shared by Bloody Lust and
 //Bloody Gust and resets on floor descent; Crown of Light's disable resets on
 //room entry. Adrenaline and Dark Prince's Crown are derived from health.
 const int rage=std::min(state.damageTakenThisFloor,6);
 const bool hasLust=157<state.collectibleCounts.size()&&state.collectibleCounts[157]>0;
 const bool hasGust=695<state.collectibleCounts.size()&&state.collectibleCounts[695]>0;
 if(hasLust){
  float bonus=0;for(int i=0;i<rage;++i)bonus=Add(bonus,Add(0.5f,Mul(0.2f,(float)i)));
  damageAdd=Add(damageAdd,bonus);
 }
 if(hasGust){
  for(int i=0;i<rage;++i){speedAdd=Add(speedAdd,Add(0.07f,Mul(0.04f,(float)i)));
   fireAdd=Add(fireAdd,Add(0.25f,Mul(0.1f,(float)i)));}
 }
 if(493<state.collectibleCounts.size()&&state.collectibleCounts[493]>0){
  const int missing=(state.maxRedHearts-state.redHearts)/2;
  if(missing>0){volatile float gain=(float)pow((double)(2*missing),1.6)*0.1f;damageAdd=Add(damageAdd,gain);}
 }
 if(442<state.collectibleCounts.size()&&state.collectibleCounts[442]>0&&state.redHearts==2){
  fireAdd=Add(fireAdd,2.0f);rangeAdd=Add(rangeAdd,1.5f);shotAdd=Add(shotAdd,0.2f);
 }
 if(415<state.collectibleCounts.size()&&state.collectibleCounts[415]>0&&!state.crownOfLightDisabled&&
    state.maxRedHearts>0&&state.redHearts>=state.maxRedHearts){
  damageMul=Mul(damageMul,2.0f);shotAdd=Add(shotAdd,-0.3f);
 }
 damageAdd=Add(damageAdd,state.bloodOathDamage);speedAdd=Add(speedAdd,state.bloodOathSpeed);
 damageAdd=Add(damageAdd,state.roomDamageBonus);speedAdd=Add(speedAdd,state.roomSpeedBonus);
 damageAdd=Add(damageAdd,state.permDamageBonus);fireAdd=Add(fireAdd,state.permFireRateBonus);
 rangeAdd=Add(rangeAdd,state.permRangeBonus);shotAdd=Add(shotAdd,state.permShotSpeedBonus);
 speedAdd=Add(speedAdd,state.permSpeedBonus);luckAdd=Add(luckAdd,state.permLuckBonus);
 if(407<state.collectibleCounts.size()&&state.collectibleCounts[407]>0&&state.purityActive){
  switch(state.purityAura){
  case 1:damageAdd=Add(damageAdd,4.0f);break; //Red
  case 2:fireAdd=Add(fireAdd,2.0f);break;      //Blue
  case 3:speedAdd=Add(speedAdd,0.5f);break;    //Yellow
  case 4:rangeAdd=Add(rangeAdd,3.0f);break;    //Orange
  default:break;
  }
 }
 //Temporary pill/card/active layer is added after the collectible cache.
 tears=Add(tears,bonusTears);fireAdd=Add(fireAdd,bonusFireRate);
 damage=Mul(Add(Add(baseDamage,damageAdd),bonusDamage),damageMul);
 range=Mul(Add(Add(cacheBaseRange,rangeAdd),bonusRange),rangeMul);
 moveSpeed=Mul(Add(Add(cacheBaseMoveSpeed,speedAdd),bonusMoveSpeed),speedMul);
 shotSpeed=Mul(Add(Add(cacheBaseShotSpeed,shotAdd),bonusShotSpeed),shotMul);
 luck=Add(Add(cacheBaseLuck,luckAdd),bonusLuck);
 //Tears -> delay, then direct fire-rate additions/multipliers.
 if(tears<-0.9f)tears=-0.9f;
 float delay=Sub(Div(F(kStatThirty),Add(tears,1)),1);
 float rate=Add(Div(F(kStatThirty),Add(delay,1)),fireAdd);
 rate=Mul(rate,fireMul);
 fireDelay=Sub(Div(F(kStatThirty),rate),1);
 flying=flight==1;
 FinalizeDerived();dirty=0xffffU;return true;
}
void NativePlayerStats::Swap(NativePlayerStats& b){
 std::swap(ready,b.ready);std::swap(flying,b.flying);std::swap(dirty,b.dirty);
 for(int i=0;i<16;++i)std::swap(weapons[i],b.weapons[i]);for(int i=0;i<4;++i)std::swap(tearFlags[i],b.tearFlags[i]);
 std::swap(fireDelay,b.fireDelay);std::swap(shotSpeed,b.shotSpeed);std::swap(damage,b.damage);std::swap(baseDamage,b.baseDamage);
 std::swap(tearHeight,b.tearHeight);std::swap(fallingSpeed,b.fallingSpeed);std::swap(fallingAcceleration,b.fallingAcceleration);
 std::swap(range,b.range);std::swap(moveSpeed,b.moveSpeed);std::swap(luck,b.luck);std::swap(native17c0,b.native17c0);std::swap(native17c4,b.native17c4);
 std::swap(cacheBaseReady,b.cacheBaseReady);std::swap(cacheBaseFireDelay,b.cacheBaseFireDelay);
 std::swap(cacheBaseShotSpeed,b.cacheBaseShotSpeed);std::swap(cacheBaseRange,b.cacheBaseRange);
 std::swap(cacheBaseMoveSpeed,b.cacheBaseMoveSpeed);std::swap(cacheBaseLuck,b.cacheBaseLuck);
 std::swap(cacheBaseTearHeight,b.cacheBaseTearHeight);std::swap(cacheBaseFallingAcceleration,b.cacheBaseFallingAcceleration);
 std::swap(bonusTears,b.bonusTears);std::swap(bonusFireRate,b.bonusFireRate);std::swap(bonusDamage,b.bonusDamage);
 std::swap(bonusRange,b.bonusRange);std::swap(bonusMoveSpeed,b.bonusMoveSpeed);std::swap(bonusShotSpeed,b.bonusShotSpeed);
 std::swap(bonusLuck,b.bonusLuck);
 for(int i=0;i<3;++i){std::swap(size[i],b.size[i]);for(int j=0;j<11;++j)std::swap(colors[i][j],b.colors[i][j]);}
}
unsigned int NativePlayerStats::StateHash() const{
 unsigned int h=2166136261U;Fold(h,ready);if(!ready)return h;Fold(h,flying);Fold(h,dirty);
 for(int i=0;i<16;++i)Fold(h,weapons[i]);for(int i=0;i<4;++i)Fold(h,tearFlags[i]);
 Scalar(h,fireDelay);Scalar(h,shotSpeed);Scalar(h,damage);Scalar(h,baseDamage);Scalar(h,tearHeight);Scalar(h,fallingSpeed);
 Scalar(h,fallingAcceleration);Scalar(h,range);Scalar(h,moveSpeed);Scalar(h,luck);Fold(h,native17c0);Fold(h,native17c4);
 Fold(h,cacheBaseReady);Scalar(h,cacheBaseFireDelay);Scalar(h,cacheBaseShotSpeed);Scalar(h,cacheBaseRange);
 Scalar(h,cacheBaseMoveSpeed);Scalar(h,cacheBaseLuck);Scalar(h,cacheBaseTearHeight);Scalar(h,cacheBaseFallingAcceleration);
 Scalar(h,bonusTears);Scalar(h,bonusFireRate);Scalar(h,bonusDamage);Scalar(h,bonusRange);
 Scalar(h,bonusMoveSpeed);Scalar(h,bonusShotSpeed);Scalar(h,bonusLuck);
 for(int i=0;i<3;++i){Scalar(h,size[i].x);Scalar(h,size[i].y);for(int j=0;j<11;++j)Scalar(h,colors[i][j]);}return h;
}
