#include "native_item_pools.h"
#include "native_item_pool_data.h"
#include <algorithm>
#include <string.h>
namespace {
float FloatWord(unsigned int word){float value;memcpy(&value,&word,4);return value;}
bool Unlocked(const NativeProfile& profile,int id){
    //6ecac0 and6ebBdb use the direct negative/zero/profile predicate. The
    //normal caller has neither daily nor custom-seed achievement overrides.
    return id<0||id==0||(id<kNativeAwardCount&&profile.Award(id));
}
const NativePoolItemDef* Collectible(int id){
    for(unsigned int i=0;i<sizeof(kPoolCollectibles)/sizeof(kPoolCollectibles[0]);++i)
        if(kPoolCollectibles[i].id==id)return kPoolCollectibles+i;
    return 0;
}
int PillClass(int id){
    for(unsigned int i=0;i<sizeof(kPoolPills)/sizeof(kPoolPills[0]);++i)
        if(kPoolPills[i].id==id)return kPoolPills[i].classification;
    return -1;
}
bool Shuffle(std::vector<int>& entries,NativeRng& random){
    for(unsigned int n=(unsigned int)entries.size();n>1;--n){unsigned int j;
        if(!random.Int(n,&j))return false;std::swap(entries[n-1],entries[j]);}
    return true;
}
}
NativeItemPools::NativeItemPools():initialized(false),seed(0),counter(0),treasureMultiplier(1),devilMultiplier(1),availableTrinkets(0){
    random.Default();backup.Default();memset(pillEffects,0,sizeof(pillEffects));
    memset(pillIdentified,0,sizeof(pillIdentified));memset(pocketFlags,0,sizeof(pocketFlags));
}
bool NativeItemPools::InitializeOrdinary(unsigned int initial,const NativeRunStart& start,const NativeProfile& profile,NativeSoundState& sounds){
    if(initial==0||initialized||!NativeRunStartValid(start)||(start.mode!=0&&start.mode!=1))return false;
    seed=initial;counter=0;unsigned int value;
    if(!random.Set(seed,3)||!random.Next(&value)||!backup.Set(value,2))return false;
    seen.assign(733,0);removed.assign(733,0);trinkets.resize(190);
    treasureMultiplier=devilMultiplier=1;
    //6ec160 initializes all31 owners before reading XML. XML order can differ
    //from the fixed table (notably greedCurse); use the native table ordinals.
    for(int i=0;i<31;++i){NativeItemPool& pool=pools[i];
        pool.entries.clear();pool.totalWeight=0;pool.counter=0;
        if(!random.Next(&value)||!pool.selection.Set(value,35)||
            !random.Next(&value)||!pool.upgrade.Set(value,35))return false;
    }
    for(int i=0;i<31;++i){NativeItemPool& pool=pools[i];const NativePoolDef& source=kPoolDefinitions[i];
        for(unsigned int j=0;j<source.count;++j){const NativePoolEntryDef& def=source.entries[j];
            NativeItemPoolEntry entry;entry.id=def.id;entry.weight=entry.initialWeight=FloatWord(def.weight);
            entry.decreaseBy=FloatWord(def.decrease);entry.removeOn=FloatWord(def.remove);
            const NativePoolItemDef* item=Collectible(entry.id);
            entry.unlocked=item&&Unlocked(profile,item->achievement);entry.special=item&&item->special;
            pool.entries.push_back(entry);
            //6ecb86 adds the original weight in XML insertion order, before
            //shuffle. Locked entries remain in the pool and in its total.
            volatile float total=pool.totalWeight+entry.weight;pool.totalWeight=total;
        }
    }
    //6ec050 has its own MT, seeded from but never advancing the index3 owner.
    //It is shared by all31 shuffles; the game-global MT is not involved.
    NativeMt local;local.Seed(random.state);
    for(int i=0;i<31;++i){std::vector<NativeItemPoolEntry>& entries=pools[i].entries;
        for(unsigned int n=(unsigned int)entries.size();n>1;--n)std::swap(entries[n-1],entries[local.Next()%n]);
    }
    availableTrinkets=0;
    for(int id=0;id<190;++id){NativePoolTrinket& t=trinkets[id];t.id=id;t.enabled=t.unlocked=false;
        for(unsigned int j=0;j<sizeof(kPoolTrinkets)/sizeof(kPoolTrinkets[0]);++j){const NativePoolItemDef& def=kPoolTrinkets[j];
            if(def.id!=id||id==145)continue;
            //6e77ed bypasses872d80 for ALL negative achievements. Type2 in
            //challenge0 bypasses the collectible quality/tag gates; before
            //904960 the manager's type14/type33/type10 queries are empty.
            t.enabled=true;t.unlocked=!def.hidden&&Unlocked(profile,def.achievement);
            if(t.unlocked)++availableTrinkets;break;
        }
    }
    if(availableTrinkets==0)return false;
    memset(pillIdentified,0,sizeof(pillIdentified));memset(pocketFlags,0,sizeof(pocketFlags));
    soundResets.clear();std::vector<int> colors,eligible;
    for(int id=1;id<14;++id)colors.push_back(id);
    if(!Shuffle(colors,random))return false;colors.push_back(14);
    for(unsigned int i=0;i<sizeof(kPoolPills)/sizeof(kPoolPills[0]);++i){const NativePillDef& pill=kPoolPills[i];
        if(!Unlocked(profile,pill.achievement))continue;
        eligible.push_back(pill.id);NativePocketSoundReset change={0,pill.id,pill.announcer};soundResets.push_back(change);
        //+3c is a sound identity.6ebc7f logs level0 if it was not preloaded,
        //then clears+199 regardless. Do not invent a loaded sound/animation.
        sounds.native199[pill.announcer]=false;
    }
    if(!Shuffle(eligible,random)||eligible.size()<colors.size())return false;
    static const signed char desired[]={3,3,3,3,2,1,0,-1,-1,3,2,1,-1,-1};
    for(unsigned int i=0;i<colors.size();++i){unsigned int chosen=(unsigned int)eligible.size();
        if(desired[i]>=0)for(unsigned int j=0;j<eligible.size();++j)
            if(PillClass(eligible[j])==desired[i]){chosen=j;break;}
        //A successful class scan consumes no draw (6ebffe). Fallback always
        //draws, including a singleton remaining list (6ebe06..6ebe70).
        if(chosen==eligible.size()&&!random.Int((unsigned int)eligible.size(),&chosen))return false;
        pillEffects[colors[i]]=eligible[chosen];eligible.erase(eligible.begin()+chosen);
    }
    pillEffects[0]=-1;
    for(unsigned int i=0;i<sizeof(kPoolCards)/sizeof(kPoolCards[0]);++i){const NativePoolCardDef& card=kPoolCards[i];
        NativePocketSoundReset change={1,card.id,card.announcer};soundResets.push_back(change);sounds.native199[card.announcer]=false;}
    initialized=true;return true;
}
void NativeItemPools::Swap(NativeItemPools& other){
    std::swap(initialized,other.initialized);std::swap(seed,other.seed);std::swap(counter,other.counter);
    std::swap(random,other.random);std::swap(backup,other.backup);
    for(int i=0;i<31;++i)std::swap(pools[i],other.pools[i]);
    std::swap(treasureMultiplier,other.treasureMultiplier);std::swap(devilMultiplier,other.devilMultiplier);
    seen.swap(other.seen);removed.swap(other.removed);trinkets.swap(other.trinkets);
    std::swap(availableTrinkets,other.availableTrinkets);soundResets.swap(other.soundResets);
    for(int i=0;i<15;++i){std::swap(pillEffects[i],other.pillEffects[i]);std::swap(pillIdentified[i],other.pillIdentified[i]);}
    for(int i=0;i<62;++i)std::swap(pocketFlags[i],other.pocketFlags[i]);
}
bool NativeItemPools::Capture(NativeItemPoolSnapshot& output) const{
    if(!initialized)return false;
    //6ecf40..6ed207: retain order and the mutable+8 weight. The original
    //does not capture removed+68c, per-entry eligibility, or sound flags.
    for(int i=0;i<31;++i){std::vector<float>& weights=output.weights[i];weights.clear();
        for(unsigned int j=0;j<pools[i].entries.size();++j)weights.push_back(pools[i].entries[j].weight);
        output.poolCounters[i]=pools[i].counter;
    }
    output.seed=seed;output.counter=counter;output.random=random.state;output.backup=backup.state;
    output.treasureMultiplier=treasureMultiplier;output.devilMultiplier=devilMultiplier;output.seen=seen;
    output.trinketEnabled.resize(trinkets.size());
    for(unsigned int i=0;i<trinkets.size();++i)output.trinketEnabled[i]=trinkets[i].enabled?1:0;
    for(int i=0;i<15;++i){output.pillEffects[i]=pillEffects[i];output.pillIdentified[i]=pillIdentified[i];}
    memcpy(output.pocketFlags,pocketFlags,sizeof(pocketFlags));return true;
}
