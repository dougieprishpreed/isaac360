#include "native_floor.h"
#include <string.h>
namespace {
bool AlternateBranch(const NativeFloorContext& c) {
    if(c.nativeMode==2 || c.nativeMode==3 || (c.stage<=6 && (c.gameFlags&0x10000)) || (c.stageType!=4 && c.stageType!=5)) return false;
    return c.stage==2 || c.stage==4 || ((c.stage==1 || c.stage==3) && (c.effectiveCurses&2));
}
bool ReserveCell(const NativeFloorContext& c) {
    if(c.nativeMode==2 || c.nativeMode==3 || (c.stage<=6 && (c.gameFlags&0x10000)) || c.stageType==4 || c.stageType==5) return false;
    return (c.stage==6 || (c.stage==5 && (c.effectiveCurses&2))) && !c.challenge && !c.runCounter && c.reservationProfileGate;
}
unsigned int Draw(NativeRng& rng,int& draws) { unsigned int value=0; rng.Next(&value); ++draws; return value; }
NativeFloorResult Histogram(NativeFloorPlan& p,int stage,int low,int high,bool append) {
    NativeRoomQuery q={stage,1,13,0,0xffffffffU,low,high,0,-1}; int n,hist[13];
    if(NativeRoomHistogram(q,&n,hist,p.catalog)!=NR_OK) return NF_MISSING_STAGE;
    if(!append) { p.poolCount=0; memset(p.shapeHistogram,0,sizeof(p.shapeHistogram)); }
    p.poolCount+=n; for(int i=0;i<13;++i) p.shapeHistogram[i]+=hist[i];
    return NF_OK;
}
NativeFloorResult CombinedHistogram(NativeFloorPlan& p,int low,int high) {
    // Native stage12 concatenates stage-owned pools1..17 except13, retaining
    // aliased source entries. A widening pass APPENDS, rather than replacing.
    for(int stage=1;stage<18;++stage) if(stage!=13) {
        NativeFloorResult result=Histogram(p,stage,low,high,true); if(result!=NF_OK) return result;
    }
    return NF_OK;
}
}
NativeFloorResult NativePrepareFloor(const NativeLevelSeeds& streams,const NativeFloorContext& c,NativeFloorPlan* output) {
    if(!output || c.stage<1 || c.stage>13 || c.stageType<0 || c.stageType>5 || c.nativeMode<0 || c.nativeMode>3 ||
        c.challenge<0 || c.catalog!=NC_NORMAL || !streams.layout.state || streams.layout.index!=35 || !streams.secondary.state || streams.secondary.index!=2 ||
        (c.catalog==NC_NORMAL&&(!streams.setup.state||streams.setup.index!=35))) return NF_BAD_REQUEST;
    // The alternate-content loader's second stage-owned pool is not exported
    // yet. Never substitute the ordinary catalogue for that mode.
    if(c.nativeMode==2 || c.nativeMode==3) return NF_UNSUPPORTED_MODE;
    NativeFloorPlan p; memset(&p,0,sizeof(p)); p.streams=streams;p.catalog=c.catalog;
    if(!NativeRoomConfigStage(c.stage,c.stageType,-1,c.nativeMode,&p.configStage)) return NF_BAD_REQUEST;
    unsigned int roll=Draw(p.streams.layout,p.layoutDraws);
    p.target=c.stage*10/3+5+(roll&1); if(p.target>20) p.target=20;
    bool alternate=AlternateBranch(c);
    if((c.stageType==4 || c.stageType==5) && alternate) p.target-=3;
    if(c.effectiveCurses&2) { volatile double scaled=(double)p.target*1.8; p.target=(int)scaled; if(p.target>45) p.target=45; }
    else if(c.effectiveCurses&4) p.target+=4;
    if(c.challenge==21) p.target=40+Draw(p.streams.layout,p.layoutDraws)%5;
    if(c.stage==12) p.target=50+Draw(p.streams.layout,p.layoutDraws)%5;
    //006fa5e8 passes the local curse-evaluation clone to006f6340. It is NOT
    //the level's1/5/19 owner. Even normal difficulty consumes this setup draw.
    roll=c.catalog==NC_NORMAL?Draw(p.streams.setup,p.setupDraws):Draw(p.streams.secondary,p.secondaryDraws);
    if(c.nativeMode==1) p.target+=2+(roll&1);
    if(c.effectiveCurses&0x80) { p.target=p.target*3/5; if(p.target<4) p.target=4; }
    p.minimumDeadEnds=(c.stage!=1)+((c.effectiveCurses&2)?6:5)+(c.stage==12?2:0)+(c.held599?1:0)+(alternate?1:0);
    unsigned int topologySeed=Draw(p.streams.layout,p.layoutDraws); p.topologyStream.Set(topologySeed,35);
    bool hard=c.nativeMode==1;
    if((c.effectiveCurses&2) || c.stage>=9) { p.minDifficulty=hard?5:1; p.maxDifficulty=hard?15:10; }
    else if(hard) { p.minDifficulty=(c.stage&1)?5:10; p.maxDifficulty=(c.stage&1)?10:15; }
    else { p.minDifficulty=(c.stage&1)?1:5; p.maxDifficulty=(c.stage&1)?5:10; }
    NativeFloorResult result;
    if(c.stage==12) { p.minDifficulty=5; p.maxDifficulty=15; result=CombinedHistogram(p,5,15); }
    else result=Histogram(p,p.configStage,p.minDifficulty,p.maxDifficulty,false);
    if(result!=NF_OK) return result;
    if(p.poolCount<20) {
        p.widenedPool=true; p.minDifficulty=1; p.maxDifficulty=15;
        result=c.stage==12?CombinedHistogram(p,1,15):Histogram(p,p.configStage,1,15,false);
        if(result!=NF_OK) return result;
    }
    if(!p.poolCount) return NF_EMPTY_POOL;
    for(int i=0;i<13;++i) if(p.shapeHistogram[i]) p.options.shapes|=1U<<i;
    if(c.effectiveCurses&0x80) p.options.shapes&=0xfffffff1U;
    p.options.reserveAboveStart=c.stage==11;
    p.options.loopA=(c.effectiveCurses&2)!=0 || c.challenge==21; p.options.loopB=c.stage==12;
    if(ReserveCell(c)) p.options.reserved[5*13+6]=1;
    *output=p; return NF_OK;
}
NativeFloorResult NativeFloorGenerator::Begin(const NativeLevelSeeds& streams,const NativeFloorContext& context) {
    NativeFloorPlan next; NativeFloorResult result=NativePrepareFloor(streams,context,&next); if(result!=NF_OK) return result;
    plan=next; phase=NF_GENERATING; attempts=contentFailures=sizeGrowths=0; target=plan.target; return NF_OK;
}
NativeFloorResult NativeFloorGenerator::Step(NativeTopology* topology) {
    if(!topology) return NF_BAD_REQUEST;
    if(phase!=NF_GENERATING) return NF_BAD_PHASE;
    //The original caller has a diagnostic at the retry threshold and a
    //continuation. A host step budget belongs to its supervisor; it must not
    //turn resumable generation into a native failure or reset any owner.
    ++attempts;
    NativeTopologyResult result=topology->GenerateWrapped(plan.topologyStream,plan.options,NativeTopologyStart(6,6),target,plan.minimumDeadEnds);
    plan.topologyStream=topology->rng;
    if(result!=NT_OK && result!=NT_TOO_FEW_DEAD_ENDS) { phase=NF_FAILED; return NF_INVALID_TOPOLOGY; }
    if(result==NT_TOO_FEW_DEAD_ENDS) {
        if(attempts>=10 && attempts%5==0 && target<64) { ++target; ++sizeGrowths; }
        return NF_RETRY;
    }
    phase=NF_WAIT_CONTENT; return NF_CONTENT_READY;
}
bool NativeFloorGenerator::RejectContent(const NativeTopology* topology) {
    if(phase!=NF_WAIT_CONTENT || (topology && (!topology->rng.state || topology->rng.index!=35))) return false;
    if(topology) plan.topologyStream=topology->rng;
    ++contentFailures; phase=NF_GENERATING; return true;
}
bool NativeFloorGenerator::AcceptContent(const NativeTopology* topology) {
    if(phase!=NF_WAIT_CONTENT || (topology && (!topology->rng.state || topology->rng.index!=35))) return false;
    if(topology) plan.topologyStream=topology->rng;
    phase=NF_COMPLETE; return true;
}
