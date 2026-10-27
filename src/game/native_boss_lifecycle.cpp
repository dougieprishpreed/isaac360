#include "native_boss_lifecycle.h"
#include <string.h>
namespace {
bool Valid(const NativeBossIdentityManager& m) {
    if(m.entryCount<0 || m.entryCount>kNativeBossEntries) return false;
    for(int i=0;i<37;++i) {
        const NativeBossIdentityPoolData& p=m.pools[i];
        if(!m.owners[i].state || m.owners[i].index!=17 || p.first<0 || p.count<0 || p.first>m.entryCount || p.count>m.entryCount-p.first) return false;
    }
    return true;
}
bool Valid(const NativeBossIdentitySnapshot& s) { for(int i=0;i<37;++i) if(!s.ownerSeeds[i]) return false; return true; }
}
bool NativeCaptureBossState(const NativeBossIdentityManager& m,NativeBossIdentitySnapshot* output) {
    if(!output || !Valid(m)) return false;
    NativeBossIdentitySnapshot state;
    for(int i=0;i<37;++i) state.ownerSeeds[i]=m.owners[i].state;
    memcpy(state.persistent,m.persistent,sizeof(state.persistent)); state.persistent[3]&=255;
    *output=state; return true; //Canonical unused tail bits, not native ABI bytes.
}
bool NativeApplyBossState(NativeBossIdentityManager& m,const NativeBossIdentitySnapshot& state) {
    if(!Valid(m) || !Valid(state)) return false;
    for(int i=0;i<37;++i) m.owners[i].Set(state.ownerSeeds[i],17);
    for(int i=0;i<3;++i) m.persistent[i]=state.persistent[i];
    m.persistent[3]=(m.persistent[3]&0xffffff00U)|(state.persistent[3]&255);
    m.ClearTemporary(); return true;
}
bool NativeInitializeRunBosses(const NativeSeeds& seeds,const NativeBossIdentityData& data,NativeBossIdentityManager* output) {
    if(!output || !seeds.Valid()) return false;
    NativeBossIdentityManager next; if(!next.Initialize(seeds.start,data)) return false;
    *output=next; return true;
}
bool NativeRestoreRunBosses(const NativeSeeds& seeds,const NativeBossIdentityData& data,const NativeBossIdentitySnapshot& state,NativeBossIdentityManager* output) {
    if(!output || !Valid(state)) return false;
    NativeBossIdentityManager next;
    if(!NativeInitializeRunBosses(seeds,data,&next) || !NativeApplyBossState(next,state)) return false;
    *output=next; return true;
}
NativeFloorResult NativeStepFloorWithBosses(NativeFloorGenerator& floor,NativeBossIdentityManager& manager,NativeTopology* topology) {
    if(!topology || !Valid(manager)) return NF_BAD_REQUEST;
    if(floor.phase!=NF_GENERATING) return NF_BAD_PHASE;
    manager.ClearTemporary(); return floor.Step(topology);
}
bool NativeAcceptFloorWithBosses(NativeFloorGenerator& floor,NativeBossIdentityManager& manager,const NativeTopology* topology) {
    if(!Valid(manager) || !floor.AcceptContent(topology)) return false;
    manager.CommitTemporary(); return true;
}
