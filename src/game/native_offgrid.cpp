#include "native_offgrid.h"
#include <string.h>
namespace {
NativeOffgridResult Prepare(NativeRoomPool& pool,NativeRng& owner,NativeLevelLayout& layout,int cell,int stage,int type,int low,int high,int subtype,bool lookup,
        NativeOffgridReport& report) {
    NativeOffgridEntry& e=report.entries[report.count++];e.gridIndex=cell;e.type=type;e.stage=stage;e.subtype=subtype;
    e.ownerIndex=owner.index;e.ownerEntry=owner.state;e.definition=-1;
    NativePlacedRoom* room=layout.Special(cell);room->placementSeed=owner.state;
    if(!NativeAdvanceRoomSeeds(owner,&room->seeds))return NOG_BAD_DATA;
    owner.Next(&e.selectionSeed);e.ownerFinal=owner.state;
    if(lookup)e.selectionResult=NativeFindRoomVariant(stage,type,0,&e.definition,pool.catalog);
    else {NativeRoomQuery q={stage,type,13,0,0xffffffffU,low,high,0,subtype};int draws=0;
        e.selectionResult=pool.SelectOrdinary(e.selectionSeed,q,true,&e.definition,&draws);report.selectorDraws+=draws;}
    //These results are assigned directly; native code permits null here.
    room->definition=e.definition;
    if(owner.index==35)report.layoutDraws+=4;else report.localDraws+=4;
    return e.selectionResult==NR_OK||e.selectionResult==NR_NO_MATCH?NOG_OK:NOG_BAD_DATA;
}
}
NativeOffgridResult NativePrepareOffgridRooms(NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeOffgridContext& c,NativeOffgridReport* out) {
    if(!out||!stream.state||stream.index!=35||layout.count<1||layout.count>507)return NOG_BAD_REQUEST;
    int expectedConfig;
    if(!NativeRoomConfigStage(c.stage,c.stageType,-1,c.nativeMode,&expectedConfig))return NOG_UNSUPPORTED_CONTEXT;
    if(c.stage<1||c.stage>8||c.stageType<0||c.stageType>2||c.nativeMode<0||c.nativeMode>1||c.configStage!=expectedConfig)return NOG_UNSUPPORTED_CONTEXT;
    NativeOffgridReport r;memset(&r,0,sizeof(r));r.entrySeed=stream.state;
    //006f2e47/006f2ec4: branch bodies require stage>4/>3. Do not omit draws.
    stream.Float(r.bossRolls);stream.Float(r.bossRolls+1);r.layoutDraws=2;
    const int cells[]={-2,-6,-4,-5,-7,-8},types[]={3,22,16,17,5,1},stages[]={0,0,0,0,0,13},subtypes[]={-1,-1,-1,-1,55,1};
    for(int i=0;i<6;++i) {
        NativeOffgridResult result=Prepare(pool,stream,layout,cells[i],stages[i],types[i],i==5?0:1,i==5?0:10,subtypes[i],i==3&&c.held550,r);
        if(result!=NOG_OK){r.finalSeed=stream.state;*out=r;return result;}
    }
    //006f31fd..006f329a: layout draw seeds index19, two local Int draws
    //select the shop subtype, then three descriptor seeds and its selector.
    unsigned int seed,n;stream.Next(&seed);++r.layoutDraws;NativeRng local;local.Set(seed,19);
    local.Int(1+(c.profile153?1:0),&n);int subtype=n;
    local.Int(1+(c.profile152?1:0)+(c.profile154?1:0),&n);subtype+=n;r.localDraws+=2;
    NativeOffgridResult result=Prepare(pool,local,layout,-13,0,2,1,10,subtype,false,r);
    if(result!=NOG_OK){r.finalSeed=stream.state;*out=r;return result;}
    //006f32b2..006f3337: a separate index20 owner, not the shop clone.
    stream.Next(&seed);++r.layoutDraws;local.Set(seed,20);
    result=Prepare(pool,local,layout,-18,0,15,1,10,1,false,r);
    //00700510 is false for stage types0..2; no late alternate boss descriptor.
    r.finalSeed=stream.state;*out=r;return result;
}
