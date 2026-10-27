#include "native_boss_identity.h"
#include "native_boss_data.h"
#include <string.h>
namespace {
float Add(float a,float b) { volatile float n=a+b; return n; }
float Sub(float a,float b) { volatile float n=a-b; return n; }
float Mul(float a,float b) { volatile float n=a*b; return n; }
float Div(float a,float b) { volatile float n=a/b; return n; }
bool Weight(float n) { return n>=0 && n<=1000000; }
void Mark(unsigned int* bits,unsigned int id,bool value) {
    unsigned int mask=1U<<(id&31); if(value) bits[id>>5]|=mask; else bits[id>>5]&=~mask;
}
//008660e0/00866130, used only for the native per-pool initial permutation.
struct ShuffleRng {
    unsigned int words[624]; int index;
    void Seed(unsigned int seed) { words[0]=seed; for(unsigned int i=1;i<624;++i) words[i]=(words[i-1]^(words[i-1]>>30))*1812433253U+i; index=624; }
    unsigned int Next() {
        if(index>=624) {
            for(int i=0;i<624;++i) {
                unsigned int n=(words[i]&0x80000000U)|(words[(i+1)%624]&0x7fffffffU);
                words[i]=words[(i+397)%624]^(n>>1)^((n&1)?0x9908b0dfU:0);
            }
            index=0;
        }
        unsigned int n=words[index++]; n^=n>>11; n^=(n<<7)&0x9d2c5680U; n^=(n<<15)&0xefc60000U; return n^(n>>18);
    }
};
bool ValidPool(const NativeBossIdentityManager& m,int poolIndex) {
    if(poolIndex<0 || poolIndex>=37 || m.entryCount<0 || m.entryCount>kNativeBossEntries) return false;
    const NativeBossIdentityPoolData& p=m.pools[poolIndex];
    if(p.first<0 || p.count<0 || p.first>m.entryCount || p.count>m.entryCount-p.first || !Weight(p.total)) return false;
    for(int i=p.first;i<p.first+p.count;++i) {
        const NativeBossIdentityEntry& e=m.entries[i];
        if(e.id>=kNativeBossIds || !Weight(e.baseWeight) || !Weight(e.currentWeight)) return false;
    }
    return true;
}
bool Allowed(const NativeBossIdentityEntry& e,const NativeBossIdentityContext& c) {
    return !(e.id==74 && c.challenge==36) && (e.achievement<0 || c.unlocked[e.id]);
}
void Report(NativeBossIdentityReport& r,int poolIndex,unsigned int state) {
    memset(&r,0,sizeof(r)); r.identity=r.entryIndex=-1; r.poolIndex=poolIndex; r.localState=state; r.path=NBI_WEIGHTED;
}
void AddSample(NativeBossIdentityReport& r,const NativeBossIdentityReport& sample) {
    r.entryIndex=sample.entryIndex; r.scanPasses+=sample.scanPasses; r.remaps+=sample.remaps; r.linearVisits+=sample.linearVisits;
}
}
const NativeBossIdentityData* NativePreparedBossIdentityData(int profile) {
    for(unsigned int i=0;i<sizeof(kPreparedBossData)/sizeof(kPreparedBossData[0]);++i) if(kPreparedBossData[i].profile==profile) return &kPreparedBossData[i];
    return 0;
}
bool NativeBossIdentityManager::Initialize(unsigned int seed,const NativeBossIdentityData& data) {
    if(!seed || !data.pools || !data.entries || data.count<0 || data.count>kNativeBossEntries) return false;
    NativeBossIdentityManager next; memset(&next,0,sizeof(next)); next.profile=data.profile; next.entryCount=data.count;
    memcpy(next.pools,data.pools,sizeof(next.pools)); memcpy(next.entries,data.entries,data.count*sizeof(NativeBossIdentityEntry));
    for(int p=0;p<37;++p) if(!ValidPool(next,p)) return false;
    //0041ded0 advances3/13/7 once per config stage; each owner uses2/21/9.
    for(int p=0;p<37;++p) {
        seed=NativeShift(seed,3,13,7); next.owners[p].Set(seed,17);
        const NativeBossIdentityPoolData& pool=next.pools[p]; ShuffleRng mt; mt.Seed(seed);
        for(int i=pool.count-1;i>0;--i) {
            unsigned int target=mt.Next()%(i+1); NativeBossIdentityEntry saved=next.entries[pool.first+i];
            next.entries[pool.first+i]=next.entries[pool.first+target]; next.entries[pool.first+target]=saved;
        }
    }
    *this=next; return true;
}
bool NativeBossIdentityManager::Blocked(unsigned int id) const { return id>=104 || ((persistent[id>>5]|temporary[id>>5])&(1U<<(id&31)))!=0; }
bool NativeBossIdentityManager::SetBlocked(unsigned int id,bool persistentSet,bool value) {
    if(id>=104) return false; Mark(persistentSet?persistent:temporary,id,value); return true;
}
void NativeBossIdentityManager::ClearTemporary() {
    for(unsigned int id=0;id<104;++id) Mark(temporary,id,false);
}
void NativeBossIdentityManager::CommitTemporary() {
    for(unsigned int id=0;id<104;++id) { Mark(persistent,id,Blocked(id)); Mark(temporary,id,false); }
}
NativeBossIdentityResult NativeBossIdentityManager::Sample(int poolIndex,float sample,const NativeBossIdentityContext& c,NativeBossIdentityReport* output) {
    if(!output || !(sample>=0 && sample<=1000000) || !ValidPool(*this,poolIndex)) return NBI_BAD_POOL;
    NativeBossIdentityReport r; Report(r,poolIndex,0); const NativeBossIdentityPoolData& p=pools[poolIndex];
    if(!p.count || p.total<=0) { *output=r; return NBI_OK; }
    int selected=-1;
    for(int pass=0;pass<10 && selected<0;++pass) {
        ++r.scanPasses; float sum=0;
        for(int i=p.first;i<p.first+p.count;++i) {
            const NativeBossIdentityEntry& e=entries[i]; float end=Add(sum,e.baseWeight);
            if(sample<end) {
                if(e.currentWeight>0 && sample<Add(sum,e.currentWeight) && Allowed(e,c) && !Blocked(e.id)) { selected=i; break; }
                if(pass==9) {
                    //Saved post-logger continuation walks from the NEXT entry,
                    //wraps, and stops before revisiting the last rejected one.
                    for(int step=1;step<p.count;++step) {
                        int candidate=p.first+(i-p.first+step)%p.count; ++r.linearVisits;
                        const NativeBossIdentityEntry& item=entries[candidate];
                        if(item.currentWeight>0 && Allowed(item,c) && !Blocked(item.id)) { selected=candidate; break; }
                    }
                } else { sample=Mul(Div(Sub(end,sample),e.baseWeight),p.total); ++r.remaps; }
                break;
            }
            sum=end;
        }
    }
    if(selected>=0) { r.entryIndex=selected; r.identity=(int)entries[selected].id; Mark(temporary,entries[selected].id,true); }
    *output=r; return NBI_OK;
}
NativeBossIdentityResult NativeBossIdentityManager::Select(const NativeBossIdentityContext& c,NativeBossIdentityReport* output) {
    int poolIndex;
    if(!output || c.nativeMode<0 || c.nativeMode>3 || !NativeRoomConfigStage(c.stage,c.stageType,-1,c.nativeMode,&poolIndex) || poolIndex<0 || poolIndex>=37) return NBI_BAD_REQUEST;
    if(!ValidPool(*this,poolIndex) || !owners[poolIndex].state || owners[poolIndex].index!=17) return NBI_BAD_POOL;
    NativeBossIdentityReport r; Report(r,poolIndex,owners[poolIndex].state); r.path=NBI_FIXED;
    int fixed=-1;
    if(c.challenge==32) fixed=30;
    else if(c.sequence13 && c.stage<=7) { const int ids[7]={60,43,50,23,51,-1,15}; fixed=ids[c.stage-1]; }
    if(fixed<0) {
        bool alternate=c.stageType==4 || c.stageType==5;
        if(c.stage==6) fixed=alternate?89:6;
        if(c.stage==8) fixed=alternate?88:(c.profile66 || c.profileOverride)?25:8;
        if(c.stage==10) fixed=alternate?40:c.stageType==1?39:24;
        if(c.stage==11) fixed=alternate?40:c.stageType==1?40:54;
        if(c.stage==12) fixed=70;
    }
    if(fixed>=0) { r.identity=fixed; *output=r; return NBI_OK; }
    NativeRng& owner=owners[poolIndex]; unsigned int rolls[7]; for(int i=0;i<7;++i) owner.Next(&rolls[i]);
    NativeRng local=owner; r.ownerDraws=7; r.localState=local.state;
    bool late=(rolls[0]&1)==0 && (c.profile86 || c.profileOverride),alternate=c.stageType==4 || c.stageType==5;
    int special=-1;
    if(rolls[1]%10==0 && (c.profile49 || c.profileOverride) && !alternate && (c.stage==1 || c.stage==3 || c.stage==5 || c.stage==7)) {
        if(rolls[2]%10==0 && !Blocked(22)) { special=22; r.path=NBI_SPECIAL22; }
        else {
            unsigned int id=(c.stage+17)/2; //Stages1/3/5/7 ->9/10/11/12.
            if(!Blocked(id)) {
                Mark(temporary,id,true); special=(int)id; r.path=NBI_SPECIAL_STAGE;
                if(c.stage==7 && late) { Mark(temporary,38,true); special=38; }
            }
        }
    }
    if(special>=0) { Mark(temporary,(unsigned int)special,true); r.identity=special; *output=r; return NBI_OK; }
    const NativeBossIdentityPoolData& p=pools[poolIndex]; unsigned int divisor=c.challenge==34?1:(c.stage==3 || c.stage==4)?50:c.stage==5?25:c.stage==7?40:0;
    if(p.pairedVariant && divisor && rolls[3]%divisor==0) { r.identity=(int)(0U-(unsigned int)p.pairedVariant); r.path=NBI_PAIRED_VARIANT; *output=r; return NBI_OK; }
    if(rolls[4]%10==0 && !alternate && c.runFlag6 && !Blocked(23)) { Mark(temporary,23,true); r.identity=23; r.path=NBI_SPECIAL23; *output=r; return NBI_OK; }
    r.path=NBI_WEIGHTED;
    for(int attempt=0;attempt<10;++attempt) {
        ++r.attempts; float point; local.Float(&point); ++r.localDraws; r.localState=local.state;
        NativeBossIdentityReport sample; Sample(poolIndex,Mul(point,p.total),c,&sample); AddSample(r,sample);
        if(sample.entryIndex>=0) {
            const NativeBossIdentityEntry& e=entries[sample.entryIndex];
            //Native repeats the eligibility check after the helper. Context is
            //a stable snapshot here; no callback/unlock mutation is fabricated.
            if(Allowed(e,c)) { r.identity=e.variant?(int)(0U-(unsigned int)e.variant):(int)e.id; *output=r; return NBI_OK; }
        } else {
            ++r.resets;
            for(int i=p.first;i<p.first+p.count;++i) { Mark(persistent,entries[i].id,false); Mark(temporary,entries[i].id,false); }
        }
    }
    r.identity=1; r.path=NBI_DEFAULT; *output=r; return NBI_OK; //Saved0094b0d0 continuation, not an invented retry policy.
}
NativeBossSequenceResult NativeAdvanceManagedBoss(NativeBossSequence& sequence,NativeBossIdentityManager& manager,
        const NativeBossIdentityContext& context,NativeTopology& topology,NativeRoomPool& roomPool,NativeRng& stream,
        NativeLevelLayout& layout,NativeManagedBossReport* output) {
    int requested=sequence.IdentityStage();
    if(!output || requested<1 || sequence.context.nativeMode!=context.nativeMode || !stream.state || stream.index!=35 || layout.count<0 || layout.count>507) return NBS_BAD_REQUEST;
    NativeManagedBossReport r; memset(&r,0,sizeof(r)); NativeBossIdentityContext c=context; c.stage=requested;
    r.identityResult=manager.Select(c,&r.identity);
    if(r.identityResult!=NBI_OK) return NBS_BAD_REQUEST; //Atomic context/pool validation, no native identity failure fabricated.
    r.sequenceResult=sequence.PlaceIdentity(r.identity.identity,topology,roomPool,stream,layout,&r.placement); *output=r; return r.sequenceResult;
}
