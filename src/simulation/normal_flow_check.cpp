// Focused normal-flow checks. No target/window/input dependency.
#if defined(_XBOX)
#include <xtl.h>
#endif
#include "../game/game_session.h"
#include "../game/native_scene.h"
#include "../game/native_loader.h"
#include "../content/resource_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <vector>
namespace {
int failures=0,checks=0,rawReads=0,constructed=0,constructionGaps=0;
FILE* events=0;std::map<int,unsigned int> expectedHashes;std::set<int> readDefinitions;
std::set<std::vector<unsigned int> > constructionContexts;
int contextualConstructions=0,missingDependencies=0;
struct ReadOnlyRecord {std::vector<unsigned char> bytes;unsigned int hash;};
std::map<int,ReadOnlyRecord> recordCache;unsigned int cachedRecordBytes=0;
#if defined(_XBOX)
const char* indexPath="game:\\game\\file_index.bin";
const char* objectRoot="game:\\game\\";
#else
const char* indexPath="runtime/game/file_index.bin";
const char* objectRoot="runtime/game/";
#endif
void Check(bool ok,const char* name,int value=0) {
    ++checks;if(!ok){++failures;fprintf(events,"FAIL\t%s\t%d\n",name,value);}
}
unsigned int Bits(float n) {unsigned int bits;memcpy(&bits,&n,4);return bits;}
float FloatBits(unsigned int bits){float n;memcpy(&n,&bits,4);return n;}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(8*i))&255))*16777619U;}
unsigned int MarkerBits(const NativeRoomMarkers& m){return (m.waterPits?1U:0)|(m.noWaterPits?2U:0)|(m.water?4U:0)|(m.noWater?8U:0)|(m.native45?16U:0)|(m.native37?32U:0);}
unsigned int PlanHash(const NativeSpawnPlan& p) {
    unsigned int h=2166136261U;Fold(h,p.count);Fold(h,p.preliminaryState);Fold(h,p.finalState);Fold(h,p.markers.flags);Fold(h,MarkerBits(p.markers));Fold(h,p.markers.directionSet?p.markers.direction:0xffffffffU);
    for(int i=0;i<p.count;++i){const NativeSpawnChoice& c=p.choices[i];Fold(h,c.present?1:0);if(!c.present)continue;
        Fold(h,c.grid);Fold(h,c.seed);Fold(h,c.option);Fold(h,c.entry.type);Fold(h,c.entry.variant);Fold(h,c.entry.subtype);Fold(h,c.entry.weightBits);}
    return h;
}
unsigned int U32(FILE* f){unsigned char b[4]={0};Check(fread(b,1,4,f)==4,"room-spawn-packet-u32");return (unsigned int)b[0]|((unsigned int)b[1]<<8)|((unsigned int)b[2]<<16)|((unsigned int)b[3]<<24);}
unsigned int Advance(unsigned int n,int draws) {while(draws--)n=NativeShift(n,5,9,7);return n;}
FILE* Input(const char* root,const char* name) {char p[768];sprintf(p,"%s/%s",root,name);if(strchr(root,'\\'))for(char* c=p;*c;++c)if(*c=='/')*c='\\';FILE* f=fopen(p,"rb");Check(f!=0,"input");return f;}
void RoomEntry(const char* root) {
    FILE* f=Input(root,"room_markers.tsv");if(!f)return;unsigned int t,v,s,flags,bits;int direction,n=0;
    while(fscanf(f,"%u%u%u%u%u%d",&t,&v,&s,&flags,&bits,&direction)==6){NativeRoomMarkers m={0};RoomRecordOption e={t,v,s,0x3f800000};NativeApplyRoomMarker(e,&m);
        Check(m.flags==flags&&MarkerBits(m)==bits&&m.directionSet==(direction>=0)&&(!m.directionSet||m.direction==direction),"native-preliminary-marker-dispatch",n++);}
    fclose(f);fprintf(events,"TOTAL\troom_markers\t%d\n",n);
    f=Input(root,"room_setup.tsv");if(!f)return;unsigned int seed,visits,level,markerFlags,markerBits,expected,waterSeed,styleState;int backdrop,stage,type,disabled,wd,sd,water,shop;n=0;
    while(fscanf(f,"%d%u%d%d%u%u%u%d%u%u%u%u%u%d%d%d%d",&backdrop,&seed,&stage,&type,&visits,&flags,&level,&disabled,&markerFlags,&markerBits,&expected,&waterSeed,&styleState,&wd,&sd,&water,&shop)==17) {
        NativeRoomSeeds seeds={seed,seed^0xabcdefU,seed^0x123456U,seed^0x987U},before=seeds;NativeRoomMarkers markers={0};markers.flags=markerFlags;
        markers.waterPits=(markerBits&1)!=0;markers.noWaterPits=(markerBits&2)!=0;markers.water=(markerBits&4)!=0;markers.noWater=(markerBits&8)!=0;markers.native45=(markerBits&16)!=0;markers.native37=(markerBits&32)!=0;
        NativeRoomSetupContext context={backdrop,stage,disabled!=0,level};NativeRoomSetupState result;
        Check(NativeSetupRoom(type,flags,visits,seeds,markers,context,&result),"native-room-setup-context",n);
        Check(result.flags==expected&&result.waterSeed==waterSeed&&result.styleState==styleState&&result.waterDraws==wd&&result.styleDraws==sd&&result.waterAmount==(float)water&&result.firstVisit==(visits==0)&&result.shopVisit==(shop!=0),"native-room-setup-vector",n++);
        Check(!memcmp(&seeds,&before,sizeof(seeds)),"native-room-setup-does-not-consume-descriptor-owners");
    }
    fclose(f);fprintf(events,"TOTAL\troom_setup\t%d\n",n);
    NativeRoomSetupState unchanged;memset(&unchanged,0x61,sizeof(unchanged));NativeRoomSetupState old=unchanged;NativeRoomSeeds zero={0};NativeRoomMarkers empty={0};NativeRoomSetupContext missing={999,1,false,0};
    Check(!NativeSetupRoom(1,0,0,zero,empty,missing,&unchanged)&&!memcmp(&unchanged,&old,sizeof(old)),"native-room-setup-invalid-input-atomic");
    f=Input(root,"room_spawns.bin");if(!f)return;char magic[8];Check(fread(magic,1,8,f)==8&&!memcmp(magic,"NRSPAWN1",8),"room-spawn-packet-magic");
    unsigned int count=U32(f);Check(count==24313,"room-spawn-normal-catalog-count");n=0;std::vector<unsigned char> bytes;NativeSpawnPlan* plan=new NativeSpawnPlan;NativeSpawnPlan* previous=new NativeSpawnPlan;
    for(unsigned int i=0;i<count;++i){unsigned int id=U32(f),size=U32(f),seeds=U32(f);Check(size>0&&size<1024*1024&&seeds==4,"room-spawn-packet-bounds");if(!size||size>=1024*1024||seeds!=4)break;
        bytes.resize(size);Check(fread(&bytes[0],1,size,f)==size,"room-spawn-record-complete");RoomRecord room;Check(room.Open(&bytes[0],size,RRF_STB1)==RRR_OK,"room-spawn-record-parser");
        Check(NativeRoom(id)&&NativeRoom(id)->bytes==size&&NativeRoomCatalog(id)==NC_NORMAL,"room-spawn-record-identity");
        for(unsigned int k=0;k<seeds;++k){unsigned int award=U32(f),after=U32(f),hash=U32(f);NativeRoomSeeds owners={0x76543210,award,0x89abcdef,17},before=owners;
            NativeRoomSpawnResult r=NativePlanRoomSpawns(room,owners,plan);Check(r==NRS_OK,"room-spawn-all-selected-identities-preserved",id);
            if(r==NRS_OK)Check(plan->preliminaryState==after&&plan->finalState==after&&PlanHash(*plan)==hash,"room-spawn-independent-owner-choice-seed-marker-vector",id);
            Check(!memcmp(&owners,&before,sizeof(owners)),"room-spawn-descriptor-owners-immutable");
            if(k==0){memcpy(previous,plan,sizeof(*plan));owners.award=0;Check(NativePlanRoomSpawns(room,owners,plan)==NRS_ZERO_SEED&&!memcmp(previous,plan,sizeof(*plan)),"room-spawn-zero-owner-atomic");}
            ++n;
        }
    }
    Check(fgetc(f)==EOF&&!ferror(f),"room-spawn-packet-exact-end");delete previous;delete plan;fclose(f);fprintf(events,"TOTAL\troom_spawns\t%d\n",n);
}
void RngReferences(const char* root) {
    FILE* f=Input(root,"rng_vectors.tsv");if(!f)return;char line[512];int count=0;
    while(fgets(line,sizeof(line),f)) {
        char kind=line[0];unsigned int seed=0,expected=0,value=0,index=0,bits=0;bool ok=false;
        if(kind=='R') {Check(sscanf(line,"%*c%u%x%x%x",&index,&seed,&expected,&bits)==4,"rng-reference-row");NativeRng rng;ok=rng.Set(seed,index)&&rng.Next(&value)&&value==expected&&Bits(NativeRandomFloat(value))==bits;}
        else if(kind=='F'){Check(sscanf(line,"%*c%x%x",&seed,&bits)==2,"float-reference-row");ok=Bits(NativeRandomFloat(seed))==bits;}
        else if(kind=='D'){NativeRoomSeeds r;unsigned int v[4]={0};Check(sscanf(line,"%*c%x%x%x%x%x",&seed,v,v+1,v+2,v+3)==5,"descriptor-seed-reference-row");ok=NativeInitRoomSeeds(seed,&r)&&r.spawn==v[0]&&r.award==v[1]&&r.decoration==v[2]&&r.auxiliary==v[3];}
        else if(kind=='C'){char expectedCode[16]={0},code[16]={0};Check(sscanf(line,"%*c %x %x %15[^\r\n]",&seed,&expected,expectedCode)==3,"seed-code-reference-row");ok=expected<=255&&NativeSeedEncode(seed,code,sizeof(code))&&!strcmp(code,expectedCode)&&NativeSeedDecode(code,&value)&&value==seed;}
        else if(kind=='S'){unsigned int v[18]={0};char* cursor=line+1;int n=0;while(n<18){while(*cursor==' '||*cursor=='\t')++cursor;if(!*cursor||*cursor=='\r'||*cursor=='\n')break;char* next=0;v[n++]=strtoul(cursor,&next,16);if(next==cursor)break;cursor=next;}
            Check(n==18,"run-seed-reference-row");NativeSeeds seeds;ok=seeds.SetStart(v[0]);for(int i=0;i<14;++i)ok=ok&&seeds.stage[i]==v[1+i];ok=ok&&seeds.playerInit==v[15]&&seeds.NextSequence(&value)&&value==v[16]&&seeds.ForgetStage(1)&&seeds.stage[1]==v[17];}
        Check(ok,"saved-instruction-rng-vector",count++);
    }
    fclose(f);Check(count==2624,"rng-reference-count");fprintf(events,"TOTAL\trng_reference\t%d\n",count);
}
void ReadDefinition(int id,unsigned int placementSeed,int mode,int backdrop,int configStage,const NativeRoomSeeds* descriptorSeeds=0,unsigned int flags=0,unsigned int visits=0) {
    NativeRoomSeeds seeds;if(descriptorSeeds)seeds=*descriptorSeeds;else Check(NativeInitRoomSeeds(placementSeed,&seeds),"spawn-seeds",id);
    unsigned int values[]={id,mode,backdrop,configStage,seeds.spawn,seeds.award,seeds.decoration,seeds.auxiliary,flags,visits};
    std::vector<unsigned int> key(values,values+10);if(!constructionContexts.insert(key).second)return;
    int contextId=contextualConstructions++;
    NativeSceneFiles files={indexPath,objectRoot,backdrop,mode,0};
    //The transport reader depends on definition/index/root, not spawn seeds,
    //mode or backdrop. Cache only its immutable verified bytes; rebuild the
    //view, spawn plan and component for every distinct execution context.
    ReadOnlyRecord& cached=recordCache[id];NativeSceneResult result=NSC_OK;
    if(cached.bytes.empty()){
        unsigned char* raw=0;unsigned int size=0,hash=0;result=NativeSceneReadRecord(files,id,&raw,&size,&hash);
        Check(result==NSC_OK,"read-selected-normal-record",id);if(result!=NSC_OK)return;
        cached.bytes.assign(raw,raw+size);cached.hash=hash;cachedRecordBytes+=size;delete[] raw;
        if(readDefinitions.insert(id).second)++rawReads;
    }
    const unsigned char* bytes=&cached.bytes[0];unsigned int size=(unsigned int)cached.bytes.size(),hash=cached.hash;
    Check(expectedHashes[id]==hash,"raw-record-checksum",id);
    RoomRecord record;Check(record.Open(bytes,size,RRF_STB1)==RRR_OK,"raw-record-parser",id);
    const NativeRoomDef* def=NativeRoom(id);
    Check(record.type==(unsigned int)def->type&&record.variant==def->variant&&record.shape==(unsigned int)def->shape,"raw-record-identity",id);
    NativeSpawnPlan* plan=new NativeSpawnPlan;NativeSceneDependencies* dependencies=new NativeSceneDependencies;
    Check(NativePlanRoomSpawns(record,seeds,plan)==NRS_OK,"contextual-selected-plan",id);unsigned int before=PlanHash(*plan);NativeRoomSeeds savedSeeds=seeds;
    Check(NativeSceneInspectPlan(record,*plan,backdrop,mode,dependencies)==NSC_OK&&before==PlanHash(*plan)&&!memcmp(&seeds,&savedSeeds,sizeof(seeds)),"dependency-inspection-preserves-plan-and-owners",id);
    //Every unresolved identity in the actual plan is retained, including those
    //after the first failed constructor. Compact repeated identities only in
    //the report, never in the runtime plan or its main-pass order.
    std::map<std::vector<unsigned int>,int> identities;
    for(int i=0;i<dependencies->count;++i){const NativeSceneDependency& d=dependencies->missing[i];const NativeSpawnChoice& c=plan->choices[d.choice];
        Check(c.present&&d.grid==c.grid&&d.seed==c.seed&&d.type==c.entry.type&&d.variant==c.entry.variant&&d.subtype==c.entry.subtype,"complete-selected-dependency-identity",id);
        unsigned int identity[]={d.type,d.variant,d.subtype};++identities[std::vector<unsigned int>(identity,identity+3)];}
    for(std::map<std::vector<unsigned int>,int>::const_iterator it=identities.begin();it!=identities.end();++it)
        fprintf(events,"DEPENDENCY\t%d\t%u\t%u\t%u\t%d\n",contextId,it->first[0],it->first[1],it->first[2],it->second);
    missingDependencies+=dependencies->count;
    NativeRoomState* room=new NativeRoomState;memset(room,0x36,sizeof(*room));NativeRoomState* previous=new NativeRoomState(*room);
    NativeConstructionIssue issue;result=NativeSceneBuildRoom(record,seeds,backdrop,mode,configStage,room,&issue);
    if(result==NSC_OK){++constructed;Check(dependencies->count==0,"no-success-with-unimplemented-selected-dependency",id);}
    else {
        bool gap=result==NSC_UNSUPPORTED_CONTENT||result==NSC_UNSUPPORTED_SHAPE||result==NSC_UNSUPPORTED_EXTENT;
        Check(gap,"unexpected-construction-error-is-not-a-coverage-gap",id);if(gap)++constructionGaps;
        Check(result!=NSC_UNSUPPORTED_CONTENT||dependencies->count>0,"unsupported-construction-has-selected-dependency",id);
        Check(memcmp(room,previous,sizeof(*room))==0,"construction-atomic",id);
        fprintf(events,"GAP\tconstruct\t%d\t%d\t%d\t%u\t%u\t%u\n",id,result,issue.stage,issue.type,issue.variant,issue.subtype);
    }
    fprintf(events,"CONSTRUCT\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\n",contextId,id,result,mode,backdrop,configStage,seeds.spawn,seeds.award,seeds.decoration,seeds.auxiliary,flags,visits,dependencies->supported,dependencies->count);
    delete dependencies;delete plan;delete previous;delete room;
}
void Catalog(const char* root) {
    FILE* f=Input(root,"catalog.tsv");if(!f)return;
    FILE* index=fopen(indexPath,"rb");GpcResourceIndex resources;
    Check(index&&resources.Load(index)==GPC_OK,"normal-source-index");if(index)fclose(index);
    unsigned int id,type,variant,subtype,mask,weight,offset,bytes,hash;int stage,ordinal,difficulty,shape,width,height,package,n=0;
    std::set<int> boundSources;
    while(fscanf(f,"%u%d%d%u%u%u%d%d%u%d%d%u%u%u%u%d",&id,&stage,&ordinal,&type,&variant,&subtype,&difficulty,&shape,&mask,&width,&height,&weight,&offset,&bytes,&hash,&package)==16) {
        const NativeRoomDef* d=NativeRoom(id);Check(d!=0,"normal-id",id);if(!d)continue;
        Check(NativeRoomId(NC_NORMAL,n)==(int)id&&NativeRoomPoolIndex(id)==n&&NativeRoomCatalog(id)==NC_NORMAL,"normal-index-mapping",id);
        Check(d->stage==stage&&d->ordinal==ordinal&&d->type==(int)type&&d->variant==variant&&d->subtype==subtype&&d->difficulty==difficulty&&d->shape==shape&&d->doors==mask&&d->width==width&&d->height==height&&Bits(d->weight)==weight&&d->offset==offset&&d->bytes==bytes,"raw-metadata",id);
        expectedHashes[id]=hash;const NativeRoomSourceDef* s=NativeRoomSourceBinding(d->source);
        Check(s&&s->package==package,"source-binding",id);
        if(s&&boundSources.insert(d->source).second) {
            GpcEntry e;Check(resources.FindOriginal(s->request,&e)==GPC_OK&&e.package==s->package&&strcmp(e.virtualName,s->resource)==0&&memcmp(e.sha256,s->sha256,32)==0,"inherited-source",id);
        }
        ++n;
    }
    fclose(f);Check(n==NativeRoomCount(NC_NORMAL)&&n==24313,"normal-catalog-count",n);
    Check(NativeRoomCount(0)==0&&!NativeRoom(0),"unconfigured-catalog-and-ids-rejected");
    NativeLoadedStageView view;Check(NativeOpenLoadedStage(24,&view,NC_NORMAL)==NR_BAD_REQUEST&&NativeStageSource(24,NC_NORMAL)<0,"missing-stage-no-substitute");
    Check(!NativeRoom(kNormalDefinitionBase+24*65536)&&!NativeRoom(kNormalDefinitionBase+0xffff)&&!NativeRoom(-1),"invalid-normal-ids");
    for(int s=0;s<256;++s)if(NativeStageSource(s,NC_NORMAL)>=0) {
        Check(NativeOpenLoadedStage(s,&view,NC_NORMAL)==NR_OK,"normal-loader-view",s);
        for(int i=0;i<view.count;++i)Check(view.Definition(i)==kNormalDefinitionBase+s*65536+i,"loaded-stage-order",i);
    }
    int start=-1;Check(NativeFindRoomVariant(0,1,2,&start,NC_NORMAL)==NR_OK,"normal-start-lookup");
    Check(start==kNormalDefinitionBase+2&&NativeRoom(start)->doors==15&&NativeRoom(start)->bytes==55,"normal-start-source");
    ReadDefinition(start,0x12345678,0,1,1);
    Check(constructed==1,"normal-empty-start-constructs");
    Check(NativeRoomSourceBinding(NativeStageSource(25,NC_NORMAL))->package==4,"stage25-inherits-package4");
}
void Selectors(const char* root) {
    FILE* f=Input(root,"selectors.tsv");if(!f)return;NativeRoomPool* pool=new NativeRoomPool;
    int stage,result,id,draws,n=0;unsigned int doors,seed,after;int lastStage=-1;unsigned int lastDoors=0;
    while(fscanf(f,"%d%u%u%d%d%d%u",&stage,&doors,&seed,&result,&id,&draws,&after)==7) {
        if(stage!=lastStage||doors!=lastDoors) {pool->Reset(NC_NORMAL);lastStage=stage;lastDoors=doors;}
        NativeRoomQuery q={stage,1,13,0,0xffffffffU,1,15,doors,-1};int selected=-1,actualDraws=0;
        NativeRoomResult r=pool->SelectOrdinary(seed,q,true,&selected,&actualDraws);
        Check(r==result&&selected==id&&actualDraws==draws,"normal-selection-vector",n);
        if(r==NR_OK)Check(Bits(pool->Weight(selected))==after,"binary32-depletion",n);
        ++n;
    }
    fclose(f);Check(n==464,"selector-vector-count",n);
    pool->Reset(NC_NORMAL);int first=kNormalDefinitionBase+65536;const NativeRoomDef* a=NativeRoom(first);
    int alias=kNormalDefinitionBase+26*65536;Check(NativeRoom(alias)->offset==a->offset&&NativeRoom(alias)->source==a->source,"stage26-alias-source");
    int selected=-1;NativeRoomQuery q={1,a->type,a->shape,a->variant,a->variant,a->difficulty,a->difficulty,0,(int)a->subtype};
    Check(pool->SelectOrdinary(1,q,true,&selected)==NR_OK,"deplete-stage-alias");
    Check(Bits(pool->Weight(alias))==Bits(a->weight),"stage-alias-depletion-isolation");
    Check(pool->Weight(0)<0,"unconfigured-id-weight-rejected");
    delete pool;
}
void Floors(const char* root) {
    FILE* f=Input(root,"floors.tsv");if(!f)return;unsigned int seed,curses,layout,secondary,setup;int mode,type,target,deadEnds,low,high,count,n=0;
    while(fscanf(f,"%u%d%d%u%d%d%d%d%d%u%u%u",&seed,&mode,&type,&curses,&target,&deadEnds,&low,&high,&count,&layout,&secondary,&setup)==12) {
        int histogram[13];for(int i=0;i<13;++i)Check(fscanf(f,"%d",histogram+i)==1,"floor-hist-input");
        NativeSeeds seeds;NativeLevelSeeds level;NativeFloorContext c={1,type,mode,0,curses,0,0,false,false,NC_NORMAL};NativeFloorPlan p={0};
        Check(seeds.SetStart(seed)&&NativeInitNormalLevelSeeds(seeds,1,type,&level)&&NativePrepareFloor(level,c,&p)==NF_OK,"normal-floor-prepare",n);
        Check(p.target==target&&p.minimumDeadEnds==deadEnds&&p.minDifficulty==low&&p.maxDifficulty==high&&p.poolCount==count&&p.configStage==type+1&&p.catalog==NC_NORMAL,"normal-floor-context-vector",n);
        Check(p.layoutDraws==2&&p.secondaryDraws==0&&p.setupDraws==1&&p.streams.layout.state==layout&&p.topologyStream.state==layout&&p.streams.secondary.state==secondary&&p.streams.setup.state==setup,"floor-owner-draw-order",n);
        Check(!memcmp(p.shapeHistogram,histogram,sizeof(histogram)),"normal-floor-histogram",n);++n;
    }
    fclose(f);Check(n==90,"floor-vector-count",n);
}
void MidSpecials(const char* root) {
    FILE* f=Input(root,"mid_special.tsv");if(!f)return;
    NativeRoomPool* pool=new NativeRoomPool;NativeLevelLayout* layout=new NativeLevelLayout;
    NativeTopology* graph=new NativeTopology;NativeTopology base;NativeRng topology;topology.Set(0x6491def1,35);
    NativeTopologyConfig options={0};options.shapes=0x1ffe;
    Check(base.GenerateWrapped(topology,options,NativeTopologyStart(6,6),20,0)==NT_OK,"mid-special-fixture-topology");
    int n=0,unresolved=0,missing=0;char line[2048];
    while(fgets(line,sizeof(line),f)) {
        unsigned int v[26];int fields=0;for(char* token=strtok(line,"\t\r\n");token&&fields<26;token=strtok(0,"\t\r\n"))v[fields++]=(unsigned int)strtoul(token,0,10);
        Check(fields==26,"mid-packet-row",n);if(fields!=26)continue;
        NativeMidSpecialContext c={0};c.placement.nativeMode=0;c.placement.roomConfigStage=1;c.stage=v[2];c.stageType=v[3];c.firstPlayerKeys=v[4];c.firstPlayerCoins=v[5];
        unsigned int flags=v[6];c.disabledSins=v[7];c.profile48=(flags&1)!=0;c.profile65=(flags&2)!=0;c.profileC9OrCA=(flags&4)!=0;c.seedEffect32=(flags&8)!=0;
        c.configuredType6=(flags&16)!=0;c.configuredType11=(flags&32)!=0;c.configuredType9=(flags&64)!=0;c.configuredType20=(flags&128)!=0;
        c.levelChallengeSubtype=(flags&256)!=0;c.fullHealthPredicate=(flags&512)!=0;c.eligiblePlayer2=(flags&1024)!=0;
        c.configuredType24=(flags&2048)!=0;c.configuredType4=(flags&4096)!=0;c.profile1da=(flags&8192)!=0;memcpy(&c.planetariumChance,v+24,4);
        c.configuredType18=(flags&16)!=0;c.configuredType19=(flags&32)!=0;c.eligibleRoom19=(flags&4)!=0;c.lowHealthPlayer0=(flags&8)!=0;
        *graph=base;layout->Reset();pool->Reset(NC_NORMAL);NativeRng stream;stream.Set(v[1],35);NativeMidSpecialReport report={0};
        NativeMidSpecialResult result=v[0]==0?NativePlaceMiniboss(*graph,*pool,stream,*layout,c,&report):v[0]==1?NativePlaceChallengeRoom(*graph,*pool,stream,*layout,c,&report):v[0]==2?NativePlaceArcadeOrType20(*graph,*pool,stream,*layout,c,&report):v[0]==3?NativePlacePlanetarium(*graph,*pool,stream,*layout,c,&report):NativePlaceBedroom(*graph,*pool,stream,*layout,c,&report);
        Check(report.type==(int)v[8]&&report.subtype==(int)v[9]&&report.definition==(int)v[10]&&report.selectionResult==(int)v[11],"mid-original-selection-vector",n);
        Check(report.selectorDraws==(int)v[14]&&report.selectionCalls==(int)v[15]&&report.selectionSeed==v[18],"mid-selection-owner-calls",n);
        if(result==NMS_UNRESOLVED_GEOMETRY) {
            ++unresolved;Check(stream.state==v[16]&&report.layoutDraws==(int)v[17],"mid-undefined-geometry-stops-before-gates",n);
        } else {
            if(result==NMS_NO_ROOM){++missing;Check(v[0]!=0&&v[10]==0xffffffffU,"mid-required-source-gap",n);}
            else Check(result==NMS_OK,"mid-geometry-install-result",n);
            bool absentPlanet=v[0]==3&&report.geometryResult!=NSP_OK;
            Check(stream.state==(absentPlanet?v[16]:v[12])&&report.layoutDraws==(int)(absentPlanet?v[17]:v[13]),"mid-post-gate-owner-vector",n);
            Check(report.gateRoll==(absentPlanet?0:v[19])&&report.secondGateRoll==v[20]&&report.install==(!absentPlanet&&v[21]!=0)&&Bits(report.chanceRoll)==(absentPlanet?0:v[25]),"mid-native-install-gates",n);
            bool placed=report.action==NOA_PLACED;
            Check(report.flagsLowOr==(placed?v[22]:0)&&report.flagsHighOr==(placed?v[23]:0),"mid-visitation-flags-on-placement-only",n);
            Check(layout->count==(placed?1:0),"mid-no-descriptor-on-requeue-or-absent-geometry",n);
            if(placed)Check(layout->rooms[0].definition==(int)v[10]&&layout->rooms[0].placementSeed==v[12],"mid-final-owner-placement-seed",n);
        }
        ++n;
    }
    Check(n==5120,"mid-vector-count",n);fprintf(events,"TOTAL\tmid\t%d\t%d\t%d\n",n,unresolved,missing);
    fclose(f);delete graph;delete layout;delete pool;
}
void Pending(NormalRunFlow& run) {
    NormalRunFlow* saved=new NormalRunFlow(run);
    for(int i=0;i<3;++i)Check(run.Step()==NFR_PENDING,"pending-caller-status");
    saved->steps=run.steps;Check(!memcmp(saved,&run,sizeof(run)),"pending-preserves-all-owned-state");delete saved;
}
void SecretGeometry(const char* root) {
    FILE* f=Input(root,"secret_geometry.tsv");if(!f)return;
    NativeTopology* graph=new NativeTopology;char line[2048];int n=0,absent=0;
    while(fgets(line,sizeof(line),f)) {
        unsigned int v[9];int fields=0;for(char* t=strtok(line,"\t\r\n");t&&fields<9;t=strtok(0,"\t\r\n"))v[fields++]=(unsigned int)strtoul(t,0,10);
        Check(fields==9,"secret-geometry-packet");if(fields!=9)continue;
        memset(graph,0,sizeof(*graph));graph->rng.Set(v[3],35);graph->count=graph->deadEndCount=1;graph->deadEnds[0]=0;
        graph->rooms[0]=NativeTopologyStart(v[1],v[2],v[0]);graph->rooms[0].index=0;graph->rooms[0].deadEnd=true;
        graph->config.reserveAboveStart=true;unsigned char blocked[169];
        for(int p=0;p<169;++p){graph->occupied[p]=-1;blocked[p]=v[4]==3||(v[4]==1&&p%3==0);graph->config.reserved[p]=v[4]==2&&p%5==0;}
        NativeCell cells[4];int count=NativeShapeFootprint(v[0],cells);
        for(int k=0;k<count;++k){NativeCell p={cells[k].x+(int)v[1],cells[k].y+(int)v[2]};graph->occupied[NativeCellIndex(p)]=0;}
        int id=v[5]?graph->PickDistantRoom():graph->PickSecretRoom(blocked);
        int cell=id<0?-1:NativeCellIndex(graph->rooms[id].origin);
        Check(cell==(int)v[6]&&graph->rng.state==v[7]&&graph->draws==v[8],"secret-position-and-complete-topology-consumption",n);
        Check(graph->deadEndCount==1&&graph->deadEnds[0]==0&&graph->otherCount==0&&graph->rooms[0].deadEnd,"secret-preserves-ordinary-order-and-leaf-metadata",n);
        if(id<0){++absent;Check(graph->count==1,"secret-no-candidate-no-append");}
        else Check(id==1&&graph->count==2&&graph->rooms[id].nativeFlag44&&graph->rooms[id].connection.x==-1&&graph->rooms[id].connection.y==-1,"secret-node-native-initialization");
        ++n;
    }
    Check(n==2688&&absent>0,"secret-geometry-corpus",n);fprintf(events,"TOTAL\tsecret_geometry\t%d\t%d\n",n,absent);fclose(f);delete graph;
    f=Input(root,"secret_blacklists.tsv");if(!f)return;
    NativeLevelLayout* layout=new NativeLevelLayout;NativeSecretContext c={0};c.stage=1;c.startGridIndex=84;
    int id,rows=0;unsigned char mask[169];
    while(fscanf(f,"%d",&id)==1) {
        layout->Reset();layout->count=1;layout->rooms[0].definition=id;layout->rooms[0].gridIndex=70;
        Check(NativeSecretBlockedCells(*layout,c,mask),"secret-raw-metadata-blacklist");
        for(int p=0;p<169;++p){int expected=-1;Check(fscanf(f,"%d",&expected)==1,"secret-blacklist-packet");Check(mask[p]==expected,"secret-source-doors-shape-and-room-type",id);}
        ++rows;
    }
    Check(rows>500,"secret-blacklist-count",rows);
    layout->Reset();c.stage=11;c.startGridIndex=13;Check(NativeSecretBlockedCells(*layout,c,mask),"stage11-secret-start-neighbours");
    Check(mask[12]&&mask[14]&&mask[0]&&mask[26],"native-linear-start-reservation-at-grid-edge");
    fprintf(events,"TOTAL\tsecret_blacklists\t%d\n",rows);delete layout;fclose(f);
}
void SecretOwners() {
    NativeRoomPool* pool=new NativeRoomPool;NativeLevelLayout* layout=new NativeLevelLayout;NativeTopology base,*graph=new NativeTopology;
    NativeTopologyConfig options={0};options.shapes=0x1ffe;NativeRng rng;rng.Set(0x6491def1,35);
    Check(base.GenerateWrapped(rng,options,NativeTopologyStart(6,6),20,0)==NT_OK,"secret-owner-topology");
    for(unsigned int seed=1;seed<=64;++seed)for(int items=0;items<4;++items) {
        NativeSecretContext c={0};c.placement.roomConfigStage=1;c.stage=1;c.startGridIndex=84;c.held102=(items&1)!=0;c.held589=(items&2)!=0;
        *graph=base;pool->Reset(NC_NORMAL);layout->Reset();NativeSecretReport report;
        Check(NativePlaceSecretRooms(*graph,*pool,*layout,seed,c,&report)==NSR_OK,"secret-native-caller");
        int requests=(c.held102?2:1)+(c.held589?1:0);Check(report.requested==requests,"secret-item-request-count");
        NativeRng owner;owner.Set(seed,1);
        for(int k=0;k<requests;++k){Check(report.entries[k].seed==owner.state,"secret-index1-per-request-owner");if(report.entries[k].layoutIndex>=0)Check(layout->rooms[report.entries[k].layoutIndex].placementSeed==owner.state,"secret-descriptor-local-seed");unsigned int ignored;owner.Next(&ignored);}
        Check(report.localState==owner.state,"secret-post-request-local-owner");
        c.configuredType29=true;NativeTopology* saved=new NativeTopology(*graph);NativeLevelLayout* savedLayout=new NativeLevelLayout(*layout);NativeRoomPool* savedPool=new NativeRoomPool(*pool);
        Check(NativePlaceType29(*graph,*pool,*layout,seed,c,&report)==NSR_OK&&report.requested==0&&!memcmp(graph,saved,sizeof(*graph))&&!memcmp(layout,savedLayout,sizeof(*layout))&&!memcmp(pool,savedPool,sizeof(*pool)),"configured-type29-no-selection-or-geometry");
        c.configuredType29=false;Check(NativePlaceType29(*graph,*pool,*layout,seed,c,&report)==NSR_OK,"type29-native-caller");
        NativeRng local;local.Set(seed,71);unsigned int selected;local.Next(&selected);
        Check(report.entries[0].seed==selected&&report.localState==selected,"type29-clones-retained-seed-not-secret-local-tail");
        if(report.placed)Check(layout->rooms[report.entries[0].layoutIndex].nativeB4==99&&layout->rooms[report.entries[0].layoutIndex].displayFlags==0,"type29-unresolved-B4-is-not-display-flags");
        c.configuredType7=true;*graph=*saved;*layout=*savedLayout;*pool=*savedPool;
        Check(NativePlaceSecretRooms(*graph,*pool,*layout,seed,c,&report)==NSR_OK&&report.requested==0&&!memcmp(graph,saved,sizeof(*graph))&&!memcmp(layout,savedLayout,sizeof(*layout))&&!memcmp(pool,savedPool,sizeof(*pool)),"configured-secret-no-selection-or-geometry");
        delete saved;delete savedLayout;delete savedPool;
    }
    delete graph;delete layout;delete pool;
}
void Offgrid(const char* root) {
    FILE* f=Input(root,"offgrid.tsv");if(!f)return;
    NativeRoomPool* pool=new NativeRoomPool;NativeLevelLayout* layout=new NativeLevelLayout;int n=0,nulls=0;char line[4096];
    const int cells[]={-2,-6,-4,-5,-7,-8,-13,-18};
    while(fgets(line,sizeof(line),f)) {
        unsigned int v[72];int fields=0;for(char* t=strtok(line,"\t\r\n");t&&fields<72;t=strtok(0,"\t\r\n"))v[fields++]=(unsigned int)strtoul(t,0,10);
        Check(fields==72,"offgrid-packet");if(fields!=72)continue;
        NativeOffgridContext c={1,(int)v[1],n&1,(int)v[1]+1,(v[2]&1)!=0,(v[2]&2)!=0,(v[2]&4)!=0,v[3]!=0};
        pool->Reset(NC_NORMAL);layout->Reset();layout->count=1;NativeRng stream;stream.Set(v[0],35);NativeOffgridReport r;
        Check(NativePrepareOffgridRooms(*pool,stream,*layout,c,&r)==NOG_OK,"offgrid-native-caller",n);
        Check(r.count==8&&r.layoutDraws==28&&r.localDraws==10&&stream.state==v[4]&&r.finalSeed==v[4],"offgrid-complete-owner-consumption",n);
        Check(Bits(r.bossRolls[0])==v[5]&&Bits(r.bossRolls[1])==v[6],"offgrid-early-stage-boss-rolls-preserved");
        Check(r.entries[6].subtype==(int)v[7]&&layout->count==1,"offgrid-profile-bounds-and-separate-list-capacity");
        for(int k=0;k<8;++k) {
            const unsigned int* expected=v+8+k*8;const NativeOffgridEntry& e=r.entries[k];const NativePlacedRoom* p=layout->ByCell(cells[k]);
            Check(e.gridIndex==cells[k]&&e.definition==(int)expected[0]&&p&&p->definition==(int)expected[0],"offgrid-original-selected-record",n);
            Check(e.ownerEntry==expected[1]&&e.ownerFinal==expected[2]&&e.selectionSeed==expected[2]&&e.ownerIndex==(int)expected[3],"offgrid-index35-19-20-separation");
            Check(p&&!memcmp(&p->seeds,expected+4,sizeof(NativeRoomSeeds)),"offgrid-spawn-award-decoration-auxiliary");
            if(e.definition<0)++nulls;
        }
        Check(layout->ByCell(-19)==0&&layout->Special(0)==0&&layout->ByCell(-13,2)==layout->ByCell(-13,0),"offgrid-native-negative-lookup-bounds-and-dimension");
        ++n;
    }
    Check(n==1536,"offgrid-vector-count",n);fprintf(events,"TOTAL\toffgrid\t%d\t%d\n",n,nulls);fclose(f);delete layout;delete pool;
}
void LevelDoors(const char* root) {
    FILE* f=Input(root,"level_doors.tsv");if(!f)return;
    NativeLevelLayout* layout=new NativeLevelLayout;int count=0;char line[2048];
    while(fgets(line,sizeof(line),f)) {
        int v[41],n=0;for(char* t=strtok(line,"\t\r\n");t&&n<41;t=strtok(0,"\t\r\n"))v[n++]=atoi(t);
        Check(n==41,"level-doors-packet");if(n!=41)continue;
        layout->Reset();NativePlacementContext c={0};c.dimension=v[4];
        for(int i=0;i<2;++i){int grid=v[1+i*2];const NativeRoomDef* d=NativeRoom(v[i*2]);NativeTopologyRoom node=NativeTopologyStart(grid%13,grid/13,d->shape);node.doors=v[5+i];
            Check(layout->Place(node,v[i*2],0x12345678,c)==NP_OK,"level-doors-pair-setup");}
        Check(layout->FinalizeDoors(),"level-doors-native-finalize");
        for(int i=0;i<2;++i){const NativePlacedRoom& r=layout->rooms[i];
            Check(r.doors==(unsigned int)v[7+i],"level-doors-reciprocal-enabled-mask",count);
            Check(!memcmp(r.doorTargets,v+9+i*16,8*sizeof(int))&&!memcmp(r.doorTargetSlots,v+17+i*16,8*sizeof(int)),"level-doors-potential-target-cells-and-reverse-slots",count);}
        ++count;
    }
    Check(count>1000,"level-doors-fixture-count",count);fprintf(events,"TOTAL\tlevel_doors\t%d\n",count);delete layout;fclose(f);
}
unsigned int MtHash(const NativeMt& owner) {
    unsigned int h=2166136261U;for(int i=0;i<624;++i)Fold(h,owner.words[i]);Fold(h,owner.index);return h;
}
unsigned int DoorHash(const NativeDoorSet& doors) {
    unsigned int h=2166136261U;Fold(h,doors.attached);Fold(h,doors.draws);
    for(int i=0;i<8;++i)Fold(h,doors.potentialGrid[i]);
    for(int i=0;i<8;++i)if(doors.attached&(1U<<i)) {
        const NativeDoorActor& a=doors.actors[i];
        Fold(h,a.grid);Fold(h,a.slot);Fold(h,a.direction);Fold(h,a.type);Fold(h,a.variant);Fold(h,a.state);Fold(h,a.auxiliary);
        Fold(h,a.sourceType);Fold(h,a.targetType);Fold(h,a.targetGrid);Fold(h,a.previousState);Fold(h,a.previousVariant);
        Fold(h,a.seed);Fold(h,a.initializations);Fold(h,a.spawnFrame);Fold(h,a.flags);Fold(h,a.local.state);Fold(h,a.local.index);
        Fold(h,Bits(a.x));Fold(h,Bits(a.y));Fold(h,Bits(a.rotation));Fold(h,Bits(a.overlayRotation));
        Fold(h,a.initialized);Fold(h,a.native278);Fold(h,a.native279);Fold(h,a.native27a);
    }
    for(int i=0;i<448;++i)Fold(h,doors.native76c[i]);return h;
}
void DoorActors(const char* root) {
    FILE* f=Input(root,"door_actors.tsv");if(!f)return;
    int shape,n=0;unsigned int allowed,seed,frame,hash,ownerHash;
    while(fscanf(f,"%d%u%u%u%u%u",&shape,&allowed,&seed,&frame,&hash,&ownerHash)==6) {
        NativeRoomGrid geometry;Check(geometry.Init(shape,shape<=5?13:26,shape<=3||shape==6||shape==7?7:14),"door-native-room-geometry");
        NativeMt owner;owner.Seed(seed);NativeDoorSet doors;
        Check(NativeInitDoorSet(shape,geometry,allowed,frame,owner,&doors),"door-native-factory",n);
        Check(DoorHash(doors)==hash,"door-native-geometry-constructor-reset-and-order",n);
        Check(MtHash(owner)==ownerHash,"door-native-global-MT-state-and-consumption",n++);
    }
    Check(n==12288,"door-factory-fixture-count",n);fprintf(events,"TOTAL\tdoor_actors\t%d\n",n);fclose(f);
    f=Input(root,"door_variants.tsv");if(!f)return;char line[1024];n=0;
    while(fgets(line,sizeof(line),f)) {
        int v[27],fields=0;for(char* t=strtok(line,"\t\r\n");t&&fields<27;t=strtok(0,"\t\r\n"))v[fields++]=atoi(t);
        Check(fields==27,"door-variant-packet",n);if(fields!=27)continue;
        NativeDoorBinding c={v[0],v[1],v[2],v[3],(unsigned int)v[4],(unsigned int)v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]!=0,v[16]!=0,v[17]!=0,v[18]!=0,v[19]!=0};
        NativeDoorBinding before=c;NativeDoorSelection d;
        Check(NativeSelectDoor(c,v[20],v[21],&d),"door-variant-input",n);
        Check(d.sourceType==v[22]&&d.targetType==v[23]&&d.variant==v[24]&&d.auxiliary==v[25]&&d.openAfterAssetSetup==(v[26]!=0),"door-original-integer-selection-branch-vector",n++);
        Check(!memcmp(&before,&c,sizeof(c)),"door-selection-has-no-context-mutation");
    }
    Check(n==98304,"door-variant-fixture-count",n);fprintf(events,"TOTAL\tdoor_variants\t%d\n",n);fclose(f);
    f=Input(root,"door_mt.tsv");if(!f)return;n=0;
    while(fgets(line,sizeof(line),f)) {
        unsigned int v[13];int fields=0;for(char* t=strtok(line,"\t\r\n");t&&fields<13;t=strtok(0,"\t\r\n"))v[fields++]=(unsigned int)strtoul(t,0,10);
        Check(fields==13,"door-global-MT-packet",n);if(fields!=13)continue;
        NativeMt owner;owner.Seed(v[0]);for(unsigned int k=0;k<v[1];++k)owner.Next();
        NativeRoomGrid geometry;geometry.Init(8,26,14);NativeDoorSet doors;
        Check(NativeInitDoorSet(8,geometry,255,0,owner,&doors),"door-factory-accepts-advanced-global-owner",n);
        for(int slot=0;slot<8;++slot)Check(doors.actors[slot].seed==v[slot+2],"door-global-MT-independent-values-across-twists",n);
        Check(owner.Next()==v[10]&&owner.Next()==v[11]&&MtHash(owner)==v[12],"door-global-MT-independent-full-owner-state",n++);
    }
    Check(n==45,"door-global-MT-fixture-count",n);fprintf(events,"TOTAL\tdoor_mt\t%d\n",n);fclose(f);
    const int states[]={0,1,2,3,4,7,99},variants[]={0,1,4,8,12},cells[]={0,67,134,447};n=0;
    for(int s=0;s<7;++s)for(int v=0;v<5;++v)for(int g=0;g<4;++g)for(int slot=0;slot<8;++slot) {
        NativeDoorActor saved={0};saved.grid=cells[g];saved.type=16;saved.state=states[s];saved.variant=variants[v];saved.auxiliary=73;
        saved.sourceType=6;saved.targetType=29;saved.targetGrid=-7;saved.previousState=-4;saved.previousVariant=-9;
        saved.seed=19;saved.initializations=0xffffffffU;saved.flags=0xffffffffU;saved.local.Set(0x654321,11);
        saved.x=12;saved.y=34;saved.native278=saved.native279=saved.native27a=true;
        NativeDoorActor before=saved;NativeDoorSet doors={0};NativeMt mt;mt.Seed(123);NativeMt expected=mt;unsigned int next=expected.Next();
        Check(NativeCreateDoor(slot,cells[g],567,mt,&doors,&saved)==NDR_OK,"door-reuse-existing-grid-door");const NativeDoorActor& a=doors.actors[slot];
        int state=states[s]==0||states[s]==2?1:states[s];
        Check(a.state==state&&a.previousState==state&&a.variant==variants[v]&&a.previousVariant==variants[v]&&a.auxiliary==73&&a.initializations==0&&a.initialized&&a.spawnFrame==567,"door-saved-state-variant-aux-and-counter-preserved");
        Check(a.sourceType==0&&a.targetType==0&&a.targetGrid==-1&&a.flags==0&&!a.native278&&!a.native279&&!a.native27a&&doors.native76c[cells[g]]==1000,"door-saved-transient-reset");
        Check(a.seed==next&&a.x==saved.x&&a.y==saved.y&&!memcmp(&mt,&expected,sizeof(mt))&&!memcmp(&a.local,&saved.local,sizeof(a.local))&&!memcmp(&saved,&before,sizeof(saved)),"door-reuse-consumes-only-global-MT");
        NativeDoorSet old=doors;NativeMt invalid={0};
        Check(NativeCreateDoor(slot,-1,99,invalid,&doors)==NDR_ALREADY_ATTACHED&&!memcmp(&doors,&old,sizeof(old))&&invalid.index==0,"door-attached-slot-returns-before-RNG-and-grid-validation");++n;
    }
    NativeRoomGrid geometry;geometry.Init(1,13,7);NativeDoorSet unchanged;memset(&unchanged,0x61,sizeof(unchanged));NativeDoorSet old=unchanged;
    NativeMt owner;owner.Seed(17);NativeMt before=owner;NativeMt invalid={0};
    Check(!NativeInitDoorSet(0,geometry,15,0,owner,&unchanged)&&!memcmp(&unchanged,&old,sizeof(old))&&!memcmp(&owner,&before,sizeof(owner)),"door-invalid-shape-atomic");
    Check(!NativeInitDoorSet(1,geometry,256,0,owner,&unchanged)&&!NativeInitDoorSet(1,geometry,15,0,invalid,&unchanged)&&!memcmp(&unchanged,&old,sizeof(old)),"door-invalid-mask-and-owner-atomic");
    NativeDoorSet zero={0};NativeMt zeroOutput={0};zeroOutput.words[623]=1;zeroOutput.index=0;
    Check(NativeCreateDoor(0,0,0,zeroOutput,&zero)==NDR_OK&&zero.actors[0].seed==0&&zeroOutput.index==1,"door-zero-MT-output-is-not-repicked");
    NativeDoorSelection result={1,2,3,4,true},prior=result;NativeDoorBinding bad={0};bad.nativeMode=4;
    Check(!NativeSelectDoor(bad,0,0,&result)&&!memcmp(&result,&prior,sizeof(result)),"door-invalid-selection-atomic");
    fprintf(events,"TOTAL\tdoor_restore\t%d\n",n);
}
void RoomSpace(const char* root) {
    FILE* f=Input(root,"room_space.tsv");if(!f)return;char line[1024];int n=0;
    while(fgets(line,sizeof(line),f)) {
        unsigned int v[26];int fields=0;for(char* t=strtok(line,"\t\r\n");t&&fields<26;t=strtok(0,"\t\r\n"))v[fields++]=(unsigned int)strtoul(t,0,10);
        Check(fields==26,"room-space-packet",n);if(fields!=26)continue;
        int shape=v[0];NativeRoomGrid grid;grid.Init(shape,shape<=5?13:26,shape<=3||shape==6||shape==7?7:14);NativeRoomSpace space;
        Check(space.Init(shape,grid,v[1],v[2]),"room-space-original-bounds-initialization",n);
        Check(Bits(space.left)==v[9]&&Bits(space.top)==v[10]&&Bits(space.right)==v[11]&&Bits(space.bottom)==v[12]&&space.zeroCollision==(v[1]==35&&v[2]==16),"room-space-physical-bounds-and-special-stage",n);
        bool rects=true;for(int k=0;k<8;++k)rects=rects&&Bits(space.regions[k])==v[13+k];Check(rects,"room-space-original-L-rectangles",n);
        NativePoint p={FloatBits(v[3]),FloatBits(v[4])},clamped,wrapped;NativeRoomSpace before=space;
        Check(space.Clamp(p,FloatBits(v[5]),FloatBits(v[6]),FloatBits(v[7]),FloatBits(v[8]),&clamped)&&Bits(clamped.x)==v[21]&&Bits(clamped.y)==v[22],"room-space-native-clamp-float-order-and-corner-ties",n);
        Check(space.Wrap(p,FloatBits(v[5]),FloatBits(v[6]),FloatBits(v[7]),FloatBits(v[8]),&wrapped)&&Bits(wrapped.x)==v[23]&&Bits(wrapped.y)==v[24],"room-space-native-wrap-axis-and-distance",n);
        Check(space.Contains(p,FloatBits(v[5]))==(v[25]!=0)&&!memcmp(&before,&space,sizeof(space)),"room-space-original-containment-without-mutation",n++);
    }
    Check(n>10000,"room-space-fixture-count",n);fprintf(events,"TOTAL\troom_space\t%d\n",n);fclose(f);
    f=Input(root,"room_collision.tsv");if(!f)return;n=0;int cell,zero,occupied,collision,cost,ok,expected;
    while(fscanf(f,"%d%d%d%d%d%d%d",&cell,&zero,&occupied,&collision,&cost,&ok,&expected)==7) {
        NativeRoomCollision map;map.Clear();if(cell>=0&&cell<448)Check(map.Set(cell,occupied!=0,collision,cost),"room-collision-native-field-write");
        NativeRoomCollision before=map;int result=31337;
        Check(map.At(cell,zero!=0,&result)==(ok!=0)&&result==expected&&!memcmp(&before,&map,sizeof(map)),"room-collision-native-occupant-and-cost-precedence",n++);
    }
    Check(n==2240,"room-collision-fixture-count",n);fprintf(events,"TOTAL\troom_collision\t%d\n",n);fclose(f);
    n=0;
    for(int shape=1;shape<=12;++shape) {
        NativeRoomGrid grid;grid.Init(shape,shape<=5?13:26,shape<=3||shape==6||shape==7?7:14);NativeRoomCollision map;map.Clear();
        for(int k=0;k<448;++k)map.Set(k,k%3==0,k%5,(k*37)%4096);
        for(int k=0;k<448;++k) {
            NativePoint p={(float)(k%grid.stride)*40+40,(float)(k/grid.stride)*40+120};int actual=-99,expected=-99,cost=-99;
            Check(map.At(k<grid.cells?k:-1,false,&expected)&&map.AtPosition(grid,p,false,&actual)&&actual==expected,"room-collision-position-query-uses-native-stride-and-extent");
            Check(map.CostAtPosition(grid,p,&cost)&&cost==(k<grid.cells?(k*37)%4096:0),"room-collision-position-cost-query-retains-native-outside-zero");++n;
        }
        NativeRoomSpace space;space.Init(shape,grid,1,1);NativeRoomSpace before=space;NativePoint p={FloatBits(0x7fc00000),0},out={1,2};
        Check(!space.Clamp(p,0,0,0,0,&out)&&out.x==1&&out.y==2&&!space.Wrap(p,0,0,0,0,&out)&&!space.Contains(p,0),"room-space-nonfinite-query-atomic");
        Check(!space.Init(0,grid,1,1)&&!memcmp(&space,&before,sizeof(space)),"room-space-invalid-shape-atomic");
        NativeRoomCollision old=map;Check(!map.Set(448,true,0,0)&&!map.Set(-1,false,0,0)&&!memcmp(&old,&map,sizeof(map)),"room-collision-invalid-write-atomic");
        int result=789;Check(!map.AtPosition(grid,p,false,&result)&&result==789&&!map.CostAtPosition(grid,p,&result)&&result==789,"room-collision-nonfinite-query-atomic");
        map.Set(0,false,99,3000,27);NativePoint rounded={-19.999f,100};
        Check(map.AtPosition(grid,rounded,false,&result)&&result==1&&map.CostAtPosition(grid,rounded,&result)&&result==3000,"room-collision-negative-fraction-truncates-to-zero-cell");
        rounded.x=-20;Check(map.AtPosition(grid,rounded,false,&result)&&result==4&&map.CostAtPosition(grid,rounded,&result)&&result==0,"room-collision-negative-one-index-outside-policy");
        NativeRoomGrid invalid=grid;invalid.stride=0;result=789;
        Check(!map.AtPosition(invalid,rounded,false,&result)&&result==789&&!map.CostAtPosition(invalid,rounded,&result)&&result==789,"room-collision-invalid-geometry-query-atomic");
    }
    fprintf(events,"TOTAL\troom_collision_positions\t%d\n",n);
    f=Input(root,"room_bounds.bin");if(!f)return;char magic[8];Check(fread(magic,1,8,f)==8&&!memcmp(magic,"NRBOUND1",8),"room-bounds-packet-magic");
    unsigned int count=U32(f);Check(count==384,"room-bounds-packet-count");
    for(unsigned int i=0;i<count;++i) {
        int shape=U32(f),stage=U32(f),type=U32(f);unsigned char cells[448];unsigned int expected[4];
        Check(fread(cells,1,448,f)==448,"room-bounds-complete-occupant-packet");for(int j=0;j<4;++j)expected[j]=U32(f);
        NativeRoomGrid grid;grid.Init(shape,shape<=5?13:26,shape<=3||shape==6||shape==7?7:14);NativeRoomSpace space;space.Init(shape,grid,stage,type);
        NativeRoomCollision map;map.Clear();for(int k=0;k<448;++k)map.Set(k,cells[k]!=0,k%5,(k*37)%4096,cells[k]);NativeRoomCollision before=map;
        Check(space.RefreshBounds(grid,map)&&Bits(space.left)==expected[0]&&Bits(space.top)==expected[1]&&Bits(space.right)==expected[2]&&Bits(space.bottom)==expected[3],"room-bounds-native-occupied-type-row-column-refresh",i);
        Check(!memcmp(&map,&before,sizeof(map)),"room-bounds-refresh-does-not-mutate-grid-actors-costs-or-collision",i);
    }
    Check(fgetc(f)==EOF&&!ferror(f),"room-bounds-packet-exact-end");fprintf(events,"TOTAL\troom_bounds\t%u\n",count);fclose(f);
}
class InputProbe:public NativeActionSource {
    NativeFrameActions source;
    InputProbe& operator=(const InputProbe&);
    void Query(int kind,int action,int controller,unsigned int owner){++count;Fold(hash,kind);Fold(hash,action);Fold(hash,controller);Fold(hash,owner);}
public:
    int count;unsigned int hash;
    explicit InputProbe(const NativeActionFrame& f):source(f),count(0),hash(2166136261U){}
    NativePlayerInputResult Value(int a,int c,unsigned int o,float* n){Query(0,a,c,o);return source.Value(a,c,o,n);}
    NativePlayerInputResult Pressed(int a,int c,unsigned int o,bool* n){Query(1,a,c,o);return source.Pressed(a,c,o,n);}
};
class ChangingInput:public NativeActionSource {
public:
    int rightReads;
    ChangingInput():rightReads(0){}
    NativePlayerInputResult Value(int a,int,unsigned int,float* n){*n=a==1?(float)++rightReads:0;return NPI_OK;}
    NativePlayerInputResult Pressed(int,int,unsigned int,bool* n){*n=false;return NPI_OK;}
};
void PlayerInput(const char* root) {
    FILE* f=Input(root,"player_input.bin");if(!f)return;char magic[8];Check(fread(magic,1,8,f)==8&&!memcmp(magic,"NPIREF1\0",8),"player-input-packet-magic");
    unsigned int count=U32(f),fields=U32(f);Check(count==45440&&fields==36,"player-input-packet-count");
    for(unsigned int i=0;i<count;++i) {
        unsigned char bytes[144];Check(fread(bytes,1,144,f)==144,"player-input-complete-record",i);unsigned int v[36];
        for(int k=0;k<36;++k){const unsigned char* p=bytes+4*k;v[k]=(unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
        NativePlayerInputContext c={0};c.playerType=v[0];c.controller=(int)v[1];c.playerToken=v[2];c.parentPresent=v[3]!=0;c.parentEntityType=v[4];
        c.twinPresent=v[5]!=0;c.twinIsSelf=v[6]!=0;c.native1540=(int)v[7];c.twinNative1540=(int)v[8];c.controlsEnabled=v[9]!=0;c.controlsCooldown=(int)v[10];
        c.stage=v[11];c.stageType=v[12];c.dimension=v[13];c.evaluatedStageFlags=v[14];c.seedEffect12=v[15]!=0;c.seedEffect74=v[16]!=0;c.seedEffect75=v[17]!=0;c.seedEffect76=v[18]!=0;
        NativePlayerInputContext before=c;NativeActionFrame frame;frame.Clear(c.controller,c.playerToken,17);
        for(int k=0;k<8;++k)Check(frame.SupplyValue(k,FloatBits(v[19+k])),"player-input-resolved-action-value");frame.SupplyDrop(v[27]!=0);NativeActionFrame saved=frame;
        InputProbe move(frame),shoot(frame);NativePoint m={-99,-99},s=m;
        Check(NativeMovementInput(c,move,&m)==NPI_OK&&Bits(m.x)==v[28]&&Bits(m.y)==v[29],"player-native-movement-vector-effects-and-paired-drop",i);
        Check(NativeShootingInput(c,shoot,&s)==NPI_OK&&Bits(s.x)==v[30]&&Bits(s.y)==v[31],"player-native-shooting-vector-nested-movement-and-reflection",i);
        Check(move.count==(int)v[32]&&move.hash==v[33]&&shoot.count==(int)v[34]&&shoot.hash==v[35],"player-native-action-query-order-controller-and-owner",i);
        Check(!memcmp(&before,&c,sizeof(c))&&!memcmp(&saved,&frame,sizeof(frame)),"player-input-context-and-frame-immutable",i);
    }
    Check(fgetc(f)==EOF&&!ferror(f),"player-input-packet-exact-end");fclose(f);fprintf(events,"TOTAL\tplayer_input\t%u\n",count);
    NativePlayerInputContext c={0};c.controller=2;c.playerToken=17;c.stage=1;c.controlsEnabled=true;
    NativeActionFrame frame;frame.Clear(2,17,0);for(int k=0;k<8;++k)frame.SupplyValue(k,k==1?1.0f:0.0f);frame.SupplyDrop(true);
    NativePoint out={123,456};NativeActionFrame before=frame;frame.knownValues&=~4U;InputProbe missing(frame);
    Check(NativeMovementInput(c,missing,&out)==NPI_SOURCE_MISSING&&out.x==123&&out.y==456&&missing.count==3,"player-missing-action-is-atomic-no-zero-substitution");frame=before;
    c.playerType=20;frame.dropKnown=false;InputProbe absentDrop(frame);
    Check(NativeMovementInput(c,absentDrop,&out)==NPI_SOURCE_MISSING&&absentDrop.count==1&&out.x==123&&out.y==456,"player-paired-drop-requires-resolved-button");
    frame.knownValues=0;frame.SupplyDrop(true);InputProbe blocked(frame);
    Check(NativeMovementInput(c,blocked,&out)==NPI_OK&&Bits(out.x)==0&&Bits(out.y)==0&&blocked.count==1,"player-paired-drop-returns-before-value-queries-and-effects");frame=before;
    c.controlsEnabled=false;InputProbe disabled(frame);
    Check(NativeMovementInput(c,disabled,&out)==NPI_OK&&out.x==1&&out.y==0&&disabled.count==4,"raw-player-input-getter-is-not-the-later-controls-enabled-gate");
    c.playerType=0;c.controlsEnabled=true;c.seedEffect74=true;ChangingInput changing;NativePoint first={0,0},second={0,0};
    Check(NativeMovementInput(c,changing,&first)==NPI_OK&&NativeShootingInput(c,changing,&second)==NPI_OK&&first.x==1&&second.x==2&&changing.rightReads==2,"player-nested-movement-resamples-the-action-provider");
    c.stage=2;c.stageType=4;c.dimension=1;InputProbe reflected(frame);
    Check(NativeShootingInput(c,reflected,&out)==NPI_OK&&out.x==1,"player-nested-and-shooting-reflection-both-applied");
    c.stageType=6;InputProbe invalid(frame);out.x=123;out.y=456;
    Check(NativeMovementInput(c,invalid,&out)==NPI_BAD_REQUEST&&NativeShootingInput(c,invalid,&out)==NPI_BAD_REQUEST&&invalid.count==0&&out.x==123&&out.y==456,"player-invalid-context-atomic-before-query");
    c.stageType=0;c.dimension=0;c.seedEffect74=false;c.playerToken=18;InputProbe wrongOwner(frame);
    Check(NativeMovementInput(c,wrongOwner,&out)==NPI_SOURCE_MISSING&&out.x==123&&out.y==456,"player-input-frame-owner-cannot-be-substituted");
    c.playerToken=17;frame.values[0]=FloatBits(0x7fc00000);InputProbe nonfinite(frame);
    Check(NativeMovementInput(c,nonfinite,&out)==NPI_BAD_VALUE&&out.x==123&&out.y==456,"player-nonfinite-action-atomic");
    before=frame;Check(!frame.SupplyValue(8,0)&&!frame.SupplyValue(0,FloatBits(0x7f800000))&&!memcmp(&before,&frame,sizeof(frame)),"player-action-frame-invalid-write-atomic");
    int streamFrames=0;NativeActionFrame local[4];
    for(unsigned int tick=0;tick<256;++tick)for(int owner=0;owner<4;++owner) {
        local[owner].Clear(owner,100+owner,tick);float right=(float)((tick+owner)%5)*.25f;
        for(int k=0;k<8;++k)local[owner].SupplyValue(k,k==1?right:0);NativeFrameActions source(local[owner]);NativePlayerInputContext context={0};
        context.controller=owner;context.playerToken=100+owner;context.stage=1;NativePoint result={-1,-1};
        Check(NativeMovementInput(context,source,&result)==NPI_OK&&result.x==right&&result.y==0&&local[owner].frame==tick,"player-local-controller-frame-isolation-and-fresh-sampling");
        context.playerToken=100+(owner+1)%4;result.x=-1;
        Check(NativeMovementInput(context,source,&result)==NPI_SOURCE_MISSING&&result.x==-1,"player-local-input-owner-isolation");++streamFrames;
    }
    fprintf(events,"TOTAL\tplayer_input_frames\t%d\n",streamFrames);
}
//One trace spans both action and cache calls to check their interleaving.
class MotionProbe:public NativeActionSource,public NativeMovementEffects {
    void Query(int kind,int id,int controller,unsigned int owner){++count;Fold(hash,kind);Fold(hash,id);Fold(hash,controller);Fold(hash,owner);}
public:
    NativeActionFrame frames[2];unsigned int knownEffects,effects,effectOwner;
    int count;unsigned int hash;
    MotionProbe():knownEffects(3),effects(0),effectOwner(0),count(0),hash(2166136261U){frames[0].Clear(0,0,0);frames[1].Clear(1,1,0);}
    NativePlayerInputResult Value(int a,int c,unsigned int o,float* n){Query(0,a,c,o);for(int i=0;i<2;++i)if(frames[i].controller==c&&frames[i].playerToken==o){NativeFrameActions source(frames[i]);return source.Value(a,c,o,n);}return NPI_SOURCE_MISSING;}
    NativePlayerInputResult Pressed(int a,int c,unsigned int o,bool* n){Query(1,a,c,o);for(int i=0;i<2;++i)if(frames[i].controller==c&&frames[i].playerToken==o){NativeFrameActions source(frames[i]);return source.Pressed(a,c,o,n);}return NPI_SOURCE_MISSING;}
    NativePlayerInputResult Has(int id,unsigned int o,bool* n){Query(2,id,0,o);unsigned int flag=id==63?1U:id==50?2U:0;
        if(!n)return NPI_BAD_REQUEST;if(o!=effectOwner)return NPI_SOURCE_MISSING;
        if(!flag){*n=false;return NPI_OK;} //Saved velocity rows carry no other collectible effects.
        if(!(knownEffects&flag))return NPI_SOURCE_MISSING;*n=(effects&flag)!=0;return NPI_OK;}
    NativePlayerInputResult Collectible(int id,unsigned int o,bool* n){Query(3,id,0,o);
        if(!n)return NPI_BAD_REQUEST;if(o!=effectOwner)return NPI_SOURCE_MISSING;*n=false;return NPI_OK;}
};
NativePlayerInputContext InputContext(const unsigned int* v){
    NativePlayerInputContext c={0};c.playerType=v[0];c.controller=(int)v[1];c.playerToken=v[2];c.parentPresent=v[3]!=0;c.parentEntityType=v[4];
    c.twinPresent=v[5]!=0;c.twinIsSelf=v[6]!=0;c.native1540=(int)v[7];c.twinNative1540=(int)v[8];c.controlsEnabled=v[9]!=0;c.controlsCooldown=(int)v[10];
    c.stage=v[11];c.stageType=v[12];c.dimension=v[13];c.evaluatedStageFlags=v[14];c.seedEffect12=v[15]!=0;c.seedEffect74=v[16]!=0;c.seedEffect75=v[17]!=0;c.seedEffect76=v[18]!=0;return c;
}
NativePlayerVelocityState VelocityState(const unsigned int* v){
    NativePlayerVelocityState s={0};s.velocity.x=FloatBits(v[0]);s.velocity.y=FloatBits(v[1]);s.previousVelocity.x=FloatBits(v[2]);s.previousVelocity.y=FloatBits(v[3]);
    s.movement.x=FloatBits(v[4]);s.movement.y=FloatBits(v[5]);s.lastMovement.x=FloatBits(v[6]);s.lastMovement.y=FloatBits(v[7]);
    s.direction=(int)v[8];s.angle=FloatBits(v[9]);s.native16d8=(int)v[10];s.native1dc4=(int)v[11];
    s.native1ddc=(int)v[12];s.native1d94=(int)v[13];return s;
}
void PlayerVelocity(const char* root){
    FILE* f=Input(root,"player_motion.bin");if(!f)return;char magic[8];Check(fread(magic,1,8,f)==8&&!memcmp(magic,"NPVREF1\0",8),"player-velocity-packet-magic");
    unsigned int count=U32(f),fields=U32(f);Check(count==18432&&fields==107,"player-velocity-packet-count");
    NativePlayerVelocityState owners[4];memset(owners,0,sizeof(owners));
    for(unsigned int i=0;i<count;++i){unsigned int v[107];for(int k=0;k<107;++k)v[k]=U32(f);
        NativePlayerVelocityContext c={0};c.input=InputContext(v);c.twinInput=InputContext(v+19);c.twinInputKnown=v[38]!=0;c.flagsLow=v[39];c.flagsHigh=v[40];
        c.native1d78=FloatBits(v[41]);c.native2a0=FloatBits(v[42]);c.moveSpeed=FloatBits(v[43]);c.native2ec=FloatBits(v[44]);
        c.native1580=(int)v[45];c.native1798=(int)v[46];c.native1e24=(int)v[47];c.native14a4=v[48]!=0;c.native1ef4=v[49]!=0;c.native1f80=v[50]!=0;
        c.targetPresent=v[51]!=0;c.position.x=FloatBits(v[52]);c.position.y=FloatBits(v[53]);c.targetPosition.x=FloatBits(v[54]);c.targetPosition.y=FloatBits(v[55]);
        c.native188=v[56]!=0;c.native18c.x=FloatBits(v[57]);c.native18c.y=FloatBits(v[58]);c.native194.x=FloatBits(v[59]);c.native194.y=FloatBits(v[60]);
        NativePlayerVelocityState state=VelocityState(v+61),expected=VelocityState(v+91);
        if(i>=16388){int owner=c.input.controller;Check(!memcmp(&state,&owners[owner],sizeof(state)),"player-velocity-persistent-frame-owner-before-update",i);state=owners[owner];}
        MotionProbe p;p.effectOwner=c.input.playerToken;p.knownEffects=v[85];p.effects=v[86];
        for(int k=0;k<2;++k){const NativePlayerInputContext& ctx=k?c.twinInput:c.input;p.frames[k].Clear(ctx.controller,ctx.playerToken,i);
            int at=k?80:75;for(int a=0;a<4;++a)p.frames[k].SupplyValue(a,FloatBits(v[at+a]));p.frames[k].SupplyDrop(v[at+4]!=0);}
        NativePlayerVelocityContext saved=c;NativePlayerVelocityIssue issue;
        NativePlayerVelocityResult result=NativePlayerVelocityCore(c,p,p,&state,&issue);
        Check(result==(NativePlayerVelocityResult)v[87]&&issue.nextAddress==v[88]&&issue.missingEffect==(int)v[89]&&issue.sourceResult==(NativePlayerInputResult)v[90],"player-velocity-native-return-or-explicit-update-tail",i);
        bool equal=!memcmp(&state,&expected,sizeof(state));
        if(!equal){const unsigned char* actual=(const unsigned char*)&state;const unsigned char* wanted=(const unsigned char*)&expected;
            for(int field=0;field<12;++field){unsigned int a,b;memcpy(&a,actual+field*4,4);memcpy(&b,wanted+field*4,4);
                if(a!=b)fprintf(events,"VELOCITY_MISMATCH\t%u\t%d\t%08x\t%08x\n",i,field,a,b);}}
        Check(equal,"player-velocity-native-float-and-entity-state",i);
        Check(p.count==(int)v[105]&&p.hash==v[106],"player-velocity-action-cache-order-and-paired-owner",i);
        Check(!memcmp(&saved,&c,sizeof(c)),"player-velocity-evaluated-context-immutable",i);
        if(i>=16384)owners[c.input.controller]=state;
    }
    Check(fgetc(f)==EOF&&!ferror(f),"player-velocity-packet-exact-end");fclose(f);fprintf(events,"TOTAL\tplayer_velocity\t%u\n",count);fprintf(events,"TOTAL\tplayer_velocity_frames\t2048\n");
    NativePlayerVelocityContext c={0};c.input.stage=1;c.input.playerToken=5;c.input.controlsEnabled=true;c.moveSpeed=1;c.native2ec=1;
    NativePlayerVelocityState state={0};state.velocity.x=2;state.native16d8=17;NativePlayerVelocityState saved=state;NativePlayerVelocityIssue issue;
    MotionProbe absent;absent.effectOwner=5;absent.frames[0].Clear(0,5,0);for(int a=0;a<4;++a)absent.frames[0].SupplyValue(a,a==1?1.0f:0);absent.frames[0].knownValues&=~4U;
    Check(NativePlayerVelocityCore(c,absent,absent,&state,&issue)==NPV_SOURCE_MISSING&&issue.nextAddress==0x72af02&&absent.count==3&&!memcmp(&state,&saved,sizeof(state)),"player-velocity-missing-action-atomic-state");
    c.input.controlsEnabled=false;MotionProbe cache;cache.effectOwner=5;cache.knownEffects=0;
    Check(NativePlayerVelocityCore(c,cache,cache,&state,&issue)==NPV_SOURCE_MISSING&&issue.missingEffect==63&&cache.count==1&&!memcmp(&state,&saved,sizeof(state)),"player-velocity-unknown-effect-is-not-absent");
    MotionProbe wrong;wrong.effectOwner=6;
    Check(NativePlayerVelocityCore(c,wrong,wrong,&state,&issue)==NPV_SOURCE_MISSING&&!memcmp(&state,&saved,sizeof(state)),"player-velocity-effect-owner-isolation");
    c.native1d78=-1;MotionProbe early;
    Check(NativePlayerVelocityCore(c,early,early,&state,&issue)==NPV_RETURNED&&early.count==0&&!memcmp(&state,&saved,sizeof(state)),"player-velocity-native-negative-field-returns-before-all-input-and-cache");
    c.native1d78=0;c.targetPresent=true;c.position.x=100;c.targetPosition.x=80;
    Check(NativePlayerVelocityCore(c,early,early,&state,&issue)==NPV_RETURNED&&state.velocity.x==-20&&state.previousVelocity.x==saved.previousVelocity.x&&early.count==0,"player-velocity-follow-target-native-early-return");
    state=saved;c.targetPresent=false;c.moveSpeed=0;MotionProbe zero;zero.effectOwner=5;
    Check(NativePlayerVelocityCore(c,zero,zero,&state,&issue)==NPV_BAD_VALUE&&!memcmp(&state,&saved,sizeof(state)),"player-velocity-invalid-arithmetic-no-partial-state");
}
void WorldDoors(NativeWorld& world,int stage,int stageType) {
    const NativePlacedRoom* source=world.CurrentDescriptor();const NativeRoomDef* from=source?NativeRoom(source->definition):0;
    Check(from!=0,"world-door-source-selected");if(!from)return;
    NativeLevelLayout* before=new NativeLevelLayout(world.layout);NativeMt owner;owner.Seed(17);NativeMt expected=owner;
    Check(world.PrepareDoorActors(owner,345)==NDR_OK,"world-door-factory-explicit-global-owner");
    const NativeWorldResident* r=world.At(world.currentList);if(!r->doors){delete before;return;}
    unsigned int mask=0;int count=0;
    for(int slot=0;slot<8;++slot)if(source->doors&(1U<<slot)) {
        ++count;const NativeDoorActor& a=r->doors->actors[slot];unsigned int seed=expected.Next();
        Check(a.seed==seed&&a.slot==slot&&a.variant==0&&a.sourceType==0&&a.targetType==0,"world-door-native-construction-before-binding");
        int collision=-1;Check(world.Current()->collision.occupied[a.grid]&&world.Current()->collision.native76c[a.grid]==1000&&world.Current()->collision.At(a.grid,false,&collision)&&collision==0,"world-door-factory-native-collision-before-remaining-Init");
        const NativePlacedRoom* target=world.layout.ByCell(source->doorTargets[slot],world.layout.currentDimension);const NativeRoomDef* to=target?NativeRoom(target->definition):0;
        Check(to!=0,"world-door-keeps-actual-selected-target");if(!to)continue;
        NativeDoorBinding c={from->type,to->type,(int)from->variant,(int)to->variant,source->flags,target->flags,stage,stageType,r->nativeMode,world.currentGrid,source->doorTargets[slot],world.layout.currentDimension,-1,world.layout.count,0,false,false,false,true,true};
        NativeDoorBinding wrong=c;wrong.targetGrid=999;NativeDoorSet saved=*r->doors;
        Check(world.SelectDoorVariant(slot,wrong)==NDR_BAD_REQUEST&&!memcmp(&saved,r->doors,sizeof(saved))&&r->doorSelectionMask==mask,"world-door-wrong-descriptor-context-atomic");
        NativeDoorSelection selected;NativeSelectDoor(c,0,0,&selected);
        Check(world.SelectDoorVariant(slot,c)==NDR_OK,"world-door-selected-target-binding");mask|=1U<<slot;
        Check(a.sourceType==selected.sourceType&&a.targetType==selected.targetType&&a.targetGrid==c.targetGrid&&a.variant==selected.variant&&a.auxiliary==selected.auxiliary&&r->doorSelectionMask==mask,"world-door-native-variant-and-masks-committed");
    }
    NativeMt after=owner;
    Check(r->doors->attached==source->doors&&r->doors->draws==(unsigned int)count&&!memcmp(&owner,&expected,sizeof(owner)),"world-door-global-owner-exact-final-state");
    Check(world.PrepareDoorActors(owner,999)==NDR_ALREADY_ATTACHED&&!memcmp(&owner,&after,sizeof(owner)),"world-door-reentry-no-extra-MT");
    unsigned int visits=source->visitedCount;world.layout.rooms[world.currentList].visitedCount=1;
    Check(world.PrepareDoorActors(owner,999)==NDR_ALREADY_ATTACHED&&!memcmp(&owner,&after,sizeof(owner)),"world-door-visited-attached-room-no-reinitialization");
    world.layout.rooms[world.currentList].visitedCount=visits;
    Check(!memcmp(&world.layout,before,sizeof(*before)),"world-door-factory-and-binding-preserve-descriptor-owners");delete before;
}
void WorldStorage() {
    int start=-1;Check(NativeFindRoomVariant(0,1,2,&start,NC_NORMAL)==NR_OK,"world-start-source");
    NativeLevelLayout* layout=new NativeLevelLayout;layout->Reset();NativePlacementContext c={0};c.roomConfigStage=1;
    for(int dim=0;dim<3;++dim)for(int cell=0;cell<169;++cell){c.dimension=dim;NativeTopologyRoom node=NativeTopologyStart(cell%13,cell/13);node.doors=15;
        Check(layout->Place(node,start,(unsigned int)(1+dim*169+cell),c)==NP_OK,"world-full-capacity-placement");
        Check(layout->rooms[layout->count-1].nativeB4==0xffffffffU&&layout->rooms[layout->count-1].visitedCount==0,"native-descriptor-constructor-sentinels");}
    for(int i=0;i<18;++i){NativePlacedRoom& p=layout->offGrid[i];p.definition=start;p.placementSeed=0xabc000U+i;NativeInitRoomSeeds(p.placementSeed,&p.seeds);}
    Check(layout->FinalizeDoors()&&layout->count==507,"world-full-grid-and-negative-capacity");
    NativeWorld* world=new NativeWorld;NativeSceneFiles files={indexPath,objectRoot,1,0};NativeWorldIssue issue;
    NativeRoomSetupContext setup={1,1,false,0};
    Check(world->Start(*layout,0,0,files,&issue,&setup)==NWR_OK,"world-atomic-start");
    for(int dim=0;dim<3;++dim)for(int cell=0;cell<169;++cell){Check(world->Enter(cell,dim,files,&issue,&setup)==NWR_OK,"world-lazy-real-record-entry");
        const NativeWorldResident* r=world->At(world->currentList);NativeRoomSetupState expected;NativeRoomMarkers markers={0};const NativePlacedRoom* p=world->CurrentDescriptor();
        Check(r->setupKnown&&NativeSetupRoom(1,0,0,p->seeds,markers,setup,&expected)&&p->flags==expected.flags&&r->setup.styleState==expected.styleState&&p->visitedCount==0,"world-native-setup-committed-without-claiming-complete-Init");
        NativePoint center={320,280},outside={0,0},inside;
        Check(world->Current()->space.Contains(center,0)&&world->Current()->space.Clamp(outside,0,0,0,0,&inside)&&inside.x==60&&inside.y==140,"world-room-physical-boundaries-from-selected-source");
        world->Current()->collision.Set(67,true,2,1000+dim*169+cell);
        world->Current()->decorationTick=1000+dim*169+cell;}
    for(int i=1;i<=18;++i){Check(world->Enter(-i,0,files,&issue)==NWR_OK,"world-negative-descriptor-entry");world->Current()->decorationTick=2000+i;}
    Check(world->residentCount==525,"world-no-twelve-room-storage-limit");
    NativeSceneFiles unavailable={"missing-index","missing-root",1,0};
    for(int dim=2;dim>=0;--dim)for(int cell=168;cell>=0;--cell){Check(world->Enter(cell,dim,unavailable)==NWR_OK,"world-reentry-without-source-replay");
        int collision=-1;Check(world->Current()->collision.At(67,false,&collision)&&collision==2&&world->Current()->collision.native76c[67]==1000+dim*169+cell,"world-collision-state-retained-for-every-resident");
        Check(world->Current()->decorationTick==1000+dim*169+cell,"world-dynamic-room-state-retained");}
    for(int i=18;i>=1;--i){Check(world->Enter(-i,0,unavailable)==NWR_OK&&world->Current()->decorationTick==2000+i,"world-negative-state-is-distinct");}
    NativeRoomState* active=world->Current();unsigned int serial=world->entrySerial;int current=world->currentList;
    Check(world->Start(*layout,0,0,unavailable,&issue)==NWR_SOURCE_ERROR&&world->Current()==active&&world->entrySerial==serial&&world->currentList==current&&world->residentCount==525,"world-failed-level-start-preserves-entire-current-world");
    layout->owners[0][0]=-1;Check(!world->Bind(*layout)&&world->Current()==active&&world->entrySerial==serial,"world-invalid-ownership-bind-atomic");
    Check(world->Enter(169,0,files)==NWR_NO_ROOM&&world->Enter(0,3,files)==NWR_BAD_REQUEST&&world->Current()==active,"world-invalid-entry-preserves-current");
    Check(world->Bind(world->layout)&&world->residentCount==0&&!world->Current(),"world-self-bind-owned-metadata-safe");
    world->Reset();Check(world->residentCount==0&&world->layout.count==0&&!world->Current(),"world-reset-releases-residents");
    fprintf(events,"TOTAL\tworld_capacity\t507\t18\t525\n");delete world;delete layout;
}
void Flows(int seeds,const char* output) {
    FILE* trace=fopen(output,"wb");Check(trace!=0,"flow-trace-open");if(!trace)return;
    fprintf(trace,"seed\tmode\ttype\tcurses\tphase\tresult\tcode\tattempts\trejections\trooms\tlayout\ttopology\tsecondary\tsetup\tlevel_seed\tcurrent_grid\tprevious_grid\tdefinitions\n");
    NormalRunFlow* run=new NormalRunFlow;NativeProfile profile={0};int pending=0,unresolved=0,guard=0,blocked=0,worlds=0,worldEntries=0,worldGaps=0;
    for(int i=0;i<seeds;++i)for(int mode=0;mode<2;++mode)for(int type=0;type<3;++type)for(int curse=0;curse<3;++curse) {
        unsigned int seed=i==0?1:i==1?0xffffffffU:(unsigned int)i*0x9e3779b9U;
        memset(run,0,sizeof(*run));NativeRunStart start={0,mode,false};
        Check(run->Begin(seed,start,profile),"normal-begin",i);Check(run->phase==NRF_NEED_FLOOR_CONTEXT&&run->pool.catalog==NC_NORMAL,"normal-pending-floor-context");Pending(*run);
        NativeFloorContext c={1,type,mode,0,curse==0?0U:curse==1?2U:4U,0,0,false,false,NC_NORMAL};
        NativeFloorContext wrong=c;wrong.catalog=0;NormalRunFlow* saved=new NormalRunFlow(*run);
        Check(run->SupplyFloorContext(wrong)==NFR_BAD_REQUEST&&!memcmp(saved,run,sizeof(*run)),"invalid-context-atomic");delete saved;
        NativeLevelSeeds fixtureOwners;NativeInitNormalLevelSeeds(run->seeds,1,type,&fixtureOwners);
        Check(run->SupplyFloorContext(c,&fixtureOwners.setup)==NFR_OK,"normal-caller-context");
        NormalRunFlow* resumed=new NormalRunFlow(*run);
        for(int step=0;step<1000&&run->phase!=NRF_BLOCKED&&run->phase!=NRF_NEED_ROOM_ENTRY;++step) {
            NormalRunPhase before=run->phase;unsigned int state=run->floor.plan.streams.layout.state;
            run->Step();
            if(before==NRF_TYPE8&&run->phase==NRF_TYPE2_SHOP)Check(run->floor.plan.streams.layout.state==Advance(state,2),"type8-before-shop-seed");
            if(before==NRF_TYPE2_SHOP&&run->phase==NRF_TYPE4_TREASURE)Check(run->floor.plan.streams.layout.state==Advance(state,2),"type2-shop-owner-draws");
            if(before==NRF_TYPE4_TREASURE&&run->phase==NRF_OPTIONAL)Check(run->floor.plan.streams.layout.state==Advance(state,2)||run->floor.plan.streams.layout.state==Advance(state,3),"type4-treasure-first-owner-restore");
            if(before==NRF_PROFILE_ROOM&&run->phase==NRF_TYPE10)Check(run->floor.plan.streams.layout.state==Advance(state,3)||run->floor.plan.streams.layout.state==Advance(state,4),"profile-room-owner-subtype-and-gates");
            if(before==NRF_TYPE10&&run->phase==NRF_MINIBOSS)Check(run->floor.plan.streams.layout.state==Advance(state,1),"type10-local-owner-separation");
            if(before==NRF_TYPE8&&run->phase==NRF_TYPE2_SHOP)Check(run->retainedRoomSeed==run->floor.plan.streams.layout.state,"retain-006efeb4-seed");
            if(before==NRF_SECRET||before==NRF_TYPE29)Check(run->floor.plan.streams.layout.state==state,"secret-callers-do-not-consume-layout-owner");
            if(before==NRF_OFFGRID&&run->phase==NRF_LEVEL_FINISH)Check(run->floor.plan.streams.layout.state==Advance(state,28),"normal-offgrid-layout-owner-draws");
            if(before==NRF_LEVEL_FINISH&&run->phase==NRF_NEED_ROOM_ENTRY)Check(run->levelSeed==Advance(state,1)&&run->floor.plan.streams.layout.state==run->levelSeed&&run->currentGrid==84&&run->previousGrid==84,"normal-level-finalization-owner-and-original-start");
        }
        if(run->phase==NRF_NEED_ROOM_ENTRY) {
            ++pending;Check(run->floor.phase==NF_COMPLETE,"native-content-caller-accepts-descriptor-layout");Pending(*run);
            int kinds[32]={0};for(int r=0;r<run->layout.count;++r){const NativePlacedRoom& p=run->layout.rooms[r];const NativeRoomDef* d=NativeRoom(p.definition);Check(NativeRoomCatalog(p.definition)==NC_NORMAL,"normal-selected-identity");if(d->type<32)++kinds[d->type];ReadDefinition(p.definition,p.placementSeed,mode,type+1,run->floor.plan.configStage,&p.seeds,p.flags,p.visitedCount);}
            Check(kinds[5]==(c.effectiveCurses==2?2:1)&&kinds[8]==1&&kinds[2]==1&&kinds[4]==(c.effectiveCurses==2?2:1),"original-prefix-room-kinds");
            Check(run->firstPlayerKeys==0&&run->fullHealthPredicate,"fresh-player-context-source");
            Check(kinds[1]>0&&run->layout.count==run->topology.count&&run->ordinaryReport.tailBurns==run->topology.count-run->ordinaryReport.orderCount,"all-generated-grid-rooms-assigned-with-native-tail-burns");
            for(int cell=0;cell<169;++cell)if(run->topology.occupied[cell]>=0)Check(run->layout.ByCell(cell)!=0,"generated-grid-footprint-has-selected-room");
            Check(run->offgridReport.count==8&&run->floor.plan.topologyStream.state==run->topology.rng.state,"complete-descriptor-content-caller");
            for(int k=0;k<18;++k){const NativePlacedRoom& p=run->layout.offGrid[k];if(p.definition>=0)ReadDefinition(p.definition,p.placementSeed,mode,type+1,run->floor.plan.configStage,&p.seeds,p.flags,p.visitedCount);}
            for(int k=0;k<18;++k){const NativePlacedRoom& p=run->layout.offGrid[k];const NativeRoomDef* data=NativeRoom(p.definition);Check(p.doors==(data?data->doors:0),"normal-negative-descriptor-native-allowed-doors");}
            NativeWorld* ownership=new NativeWorld;Check(ownership->Bind(run->layout)&&ownership->residentCount==0&&ownership->layout.count==run->layout.count,"normal-complete-floor-ownership-valid-without-preconstructing-unvisited-rooms");delete ownership;
            for(int r=0;r<run->layout.count;++r)for(int slot=0;slot<8;++slot)if(run->layout.rooms[r].doors&(1U<<slot)) {
                int reverse=-1;const NativePlacedRoom* target=run->layout.DoorTarget(r,slot,&reverse);
                Check(target&&reverse>=0&&reverse<8,"normal-native-door-has-selected-reciprocal-target");
            }
            if(i<4&&mode==0&&type==0) {
                NormalRunFlow* sourceState=new NormalRunFlow(*run);NativeWorld* world=new NativeWorld;NativeSceneFiles files={indexPath,objectRoot,1,mode};NativeWorldIssue issue;
                Check(world->Start(run->layout,run->currentGrid,0,files,&issue)==NWR_OK,"normal-world-starts-exact-selected-room");
                NativeRoomState* startRoom=world->Current();Check(startRoom&&startRoom->type==1&&startRoom->variant==2&&world->residentCount==1&&world->layout.count==run->layout.count,"normal-world-start-identity-and-all-unvisited-descriptors");
                WorldDoors(*world,1,type);const NativeDoorSet* startDoors=world->At(world->currentList)->doors;unsigned int startDoorHash=DoorHash(*startDoors);
                NativeRoomCollision startCollision=startRoom->collision;
                ++worlds;
                for(int r=0;r<run->layout.count;++r) {
                    const NativePlacedRoom& p=run->layout.rooms[r];int current=world->currentList,cell=world->currentGrid;unsigned int serial=world->entrySerial;NativeRoomState* previous=world->Current();
                    NativeWorldResult entered=world->Enter(p.safeGridIndex,p.dimension,files,&issue);
                    fprintf(events,"WORLD_ENTRY\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%u\n",seed,c.effectiveCurses,p.listIndex,p.gridIndex,p.definition,entered,issue.source,issue.construction.type,issue.construction.variant,issue.construction.subtype);
                    if(entered==NWR_OK){++worldEntries;Check(world->CurrentDescriptor()->definition==p.definition&&world->At(r)->hash==expectedHashes[p.definition],"normal-world-selected-raw-binding");
                        world->Current()->decorationTick=100+r;Check(world->Enter(p.safeGridIndex,p.dimension,files)==NWR_OK&&world->Current()->decorationTick==100+r,"normal-world-reentry-preserves-dynamic-state");
                        for(int slot=0;slot<8;++slot) {
                            const NativePlacedRoom* next=run->layout.DoorTarget(r,slot);if(!next)continue;
                            world->Enter(p.safeGridIndex,p.dimension,files);NativeWorldResult traversed=world->EnterLinked(slot,files,&issue);
                            Check(traversed==NWR_OK||traversed==NWR_CONSTRUCTION_GAP,"normal-world-door-entry-uses-owned-target-cell");
                            Check(traversed!=NWR_OK||world->CurrentDescriptor()->definition==next->definition,"normal-world-door-entry-keeps-selected-target");
                        }
                    }
                    else {++worldGaps;Check(entered==NWR_CONSTRUCTION_GAP&&world->currentList==current&&world->currentGrid==cell&&world->entrySerial==serial&&world->Current()==previous&&issue.definition==p.definition,"normal-world-construction-gap-is-atomic-no-repick");}
                }
                Check(world->Enter(run->currentGrid,0,files)==NWR_OK&&world->Current()==startRoom,"normal-world-original-start-resident-retained");
                Check(world->currentGrid==run->currentGrid&&world->At(world->currentList)->doors==startDoors&&DoorHash(*startDoors)==startDoorHash,"normal-world-door-actors-and-selected-state-retained-on-reentry");
                Check(!memcmp(&startCollision,&startRoom->collision,sizeof(startCollision)),"normal-world-room-collision-retained-with-selected-door-actors");
                Check(!memcmp(run,sourceState,sizeof(*run)),"normal-world-entry-does-not-consume-any-generation-owner-or-depletion");delete sourceState;delete world;
            }
            fprintf(events,"GAP\troom-entry-006f3b30\t%u\t%d\t%d\t%u\n",seed,mode,type,c.effectiveCurses);
        } else if(run->result==NFR_NATIVE_UNRESOLVED) {++unresolved;fprintf(events,"GAP\tnative-geometry-input\t%u\t%d\n",seed,run->lastCode);}
        else if(run->result==NFR_ATTEMPT_GUARD) {++guard;fprintf(events,"GAP\tport-attempt-guard\t%u\n",seed);}
        else {++blocked;Check(false,"unexpected-normal-flow-block",run->lastCode);}
        for(int step=0;step<1000&&resumed->phase!=NRF_BLOCKED&&resumed->phase!=NRF_NEED_ROOM_ENTRY;++step)resumed->Step();
        Check(run->phase==resumed->phase&&run->result==resumed->result&&run->floor.plan.streams.layout.state==resumed->floor.plan.streams.layout.state&&run->floor.plan.streams.setup.state==resumed->floor.plan.streams.setup.state&&run->topology.rng.state==resumed->topology.rng.state&&run->layout.count==resumed->layout.count&&run->runFlagsLow==resumed->runFlagsLow&&run->runFlagsHigh==resumed->runFlagsHigh,"staged-continuation-owners");
        Check(!memcmp(run->pool.weights,resumed->pool.weights,sizeof(run->pool.weights))&&!memcmp(&run->layout,&resumed->layout,sizeof(run->layout))&&!memcmp(&run->bosses,&resumed->bosses,sizeof(run->bosses)),"staged-continuation-catalog-layout-and-boss-state");
        delete resumed;
        fprintf(trace,"%u\t%d\t%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t",seed,mode,type,c.effectiveCurses,run->phase,run->result,run->lastCode,run->floor.attempts,run->rejections,run->layout.count,run->floor.plan.streams.layout.state,run->topology.rng.state,run->floor.plan.streams.secondary.state,run->floor.plan.streams.setup.state,run->levelSeed,run->currentGrid,run->previousGrid);
        for(int r=0;r<run->layout.count;++r)fprintf(trace,"%s%d",r?",":"",run->layout.rooms[r].definition);fprintf(trace,"\n");
    }
    Check(pending>0,"normal-prefix-reached");fprintf(events,"TOTAL\tflows\t%d\t%d\t%d\t%d\t%d\n",seeds*18,pending,unresolved,guard,blocked);
    fprintf(events,"TOTAL\tworld_entries\t%d\t%d\t%d\n",worlds,worldEntries,worldGaps);fclose(trace);delete run;
}
void InitialStages() {
    NormalRunFlow* run=new NormalRunFlow;
    for(unsigned int seed=1;seed<=64;++seed)for(int gates=0;gates<4;++gates) {
        memset(run,0,sizeof(*run));NativeProfile p={0};p.awards[86]=(gates&1)!=0;p.awards[342]=(gates&2)!=0;
        NativeRunStart start={0,0,false};Check(run->Begin(seed,start,p),"initial-stage-begin");
        int type=-1;NativeSeeds previous=run->seeds;
        Check(run->InitialStageType(&type),"initial-stage-query");unsigned int s=previous.stage[1];
        Check(type==((s%3==0&&(gates&2))?2:((s&1)==0&&(gates&1))?1:0),"initial-stage-native-gates");
        Check(!memcmp(&previous,&run->seeds,sizeof(previous)),"initial-stage-no-owner-consumption");
        Check(run->SupplyInitialFloor(0)==NFR_OK&&run->context.stageType==type,"initial-stage-caller-connection");
    }
    delete run;
}
void InitialCaller() {
    NormalRunFlow* run=new NormalRunFlow;int automatic=0,latePending=0;
    for(unsigned int seed=1;seed<=64;++seed)for(int mode=0;mode<2;++mode)for(int late=0;late<2;++late) {
        NativeProfile profile={0};profile.awards[33]=late!=0;NativeRunStart player={0,mode,false};
        memset(run,0x61,sizeof(*run));Check(run->Begin(seed,player,profile),"initial-caller-begin");
        Check(run->runFlagsLow==0&&run->runFlagsHigh==0&&!run->minibossPlaced&&run->floor.phase==NF_IDLE,"new-run-clears-prior-staged-world");
        if(late) {
            Check(run->StartInitialFloor()==NFR_PENDING&&run->phase==NRF_NEED_FLOOR_CONTEXT,"late-curse-input-remains-pending");Pending(*run);++latePending;
            NativeFloorContext c={1,0,mode,0,0,0,0,false,false,NC_NORMAL};NativeRng bad;bad.Set(17,2);
            NormalRunFlow* old=new NormalRunFlow(*run);
            Check(run->SupplyFloorContext(c,&bad)==NFR_BAD_REQUEST&&!memcmp(old,run,sizeof(*run)),"invalid-curse-owner-atomic");delete old;
            NativeLevelSeeds input;NativeInitNormalLevelSeeds(run->seeds,1,0,&input);unsigned int discarded;input.setup.Next(&discarded);
            unsigned int expected=Advance(input.setup.state,1);
            Check(run->SupplyFloorContext(c,&input.setup)==NFR_OK&&run->floor.plan.streams.setup.state==expected&&run->floor.plan.streams.secondary.state==run->seeds.stage[1],"explicit-evaluated-curse-owner-is-retained");
        } else {
            Check(run->StartInitialFloor()==NFR_OK&&run->phase==NRF_TOPOLOGY&&run->context.effectiveCurses==0,"initial-caller-proven-zero-curse");
            unsigned int n=run->seeds.stage[1],roll=Advance(n,3);int target=8+(roll&1)+(mode?2+(roll&1):0);
            Check(run->floor.plan.target==target&&run->floor.plan.streams.layout.state==Advance(n,4)&&run->floor.plan.streams.setup.state==roll&&run->floor.plan.streams.secondary.state==n,"post-log-and-local-setup-owner-composition");
            for(int step=0;step<1000&&run->phase!=NRF_BLOCKED&&run->phase!=NRF_NEED_ROOM_ENTRY;++step)run->Step();
            Check(run->phase==NRF_NEED_ROOM_ENTRY&&run->floor.phase==NF_COMPLETE,"automatic-first-stage-descriptor-generation");++automatic;
        }
    }
    for(int stage=1;stage<=13;++stage)Check(NativeNormalChallengeSubtype(stage)==(stage<=8&&!(stage&1)),"stage-even-challenge-subtype",stage);
    fprintf(events,"TOTAL\tinitial_caller\t%d\t%d\n",automatic,latePending);delete run;
}
void StaticWalls(const char* root) {
    //Straight native writes at006c5670/006c5720/006d8a74; these expected
    //constants do not call a second translation of the constructor.
    NativeStaticGrid wall;memset(&wall,0x43,sizeof(wall));NativeStaticGrid previous=wall;
    Check(!NativeSelectedWall(16,67,0,0,1,0,&wall)&&!memcmp(&wall,&previous,sizeof(wall)),"wall-room16-graphics-dependency-is-atomic");
    Check(!NativeSelectedWall(1,448,0,0,1,0,&wall)&&!memcmp(&wall,&previous,sizeof(wall)),"wall-grid-storage-boundary-atomic");
    Check(NativeSelectedWall(1,134,65535,17,0xfedcba98U,29,&wall)&&wall.grid==134&&wall.type==15&&wall.variant==65535&&wall.state==0&&
        wall.seed==0xfedcba98U&&wall.native18==0&&wall.native20==0x16a9de81U&&wall.collisionClass==4&&wall.initializations==1&&wall.initialized&&wall.initializedFrame==29,"wall-native-instruction-writes");
    FILE* f=Input(root,"static_wall_room.bin");if(!f)return;char magic[8];Check(fread(magic,1,8,f)==8&&!memcmp(magic,"NGSWALL1",8),"wall-original-record-packet");
    int id=(int)U32(f);unsigned int size=U32(f);std::vector<unsigned char> raw(size);Check(size<65536&&fread(&raw[0],1,size,f)==size&&fgetc(f)==EOF,"wall-original-record-size");fclose(f);
    RoomRecord record;Check(record.Open(&raw[0],size,RRF_STB1)==RRR_OK&&record.shape==2&&record.stackCount==18,"wall-original-record-view");
    NativeRoomSeeds seeds;NativeInitRoomSeeds(0x12345678,&seeds);NativeRoomSeeds saved=seeds;
    NativeSpawnPlan* plan=new NativeSpawnPlan;Check(NativePlanRoomSpawns(record,seeds,plan)==NRS_OK,"wall-original-main-pass");
    NativeRoomState* room=new NativeRoomState;NativeConstructionIssue detail;
    Check(NativeSceneBuildRoom(record,seeds,1,0,35,room,&detail,0,29)==NSC_OK&&room->staticGridCount==18&&room->decorationCount==0,"wall-original-selected-component-builds");
    for(int i=0;i<room->staticGridCount;++i){const NativeStaticGrid& a=room->staticGrids[i];const NativeSpawnChoice& c=plan->choices[i];int collision=-1,cost=-1;NativePoint position;
        Check(a.grid==c.grid&&a.seed==c.seed&&a.variant==(int)c.entry.variant&&a.initializedFrame==29,"wall-main-award-clone-owned-by-actor");
        Check(room->geometry.Position(a.grid,&position.x,&position.y)&&room->collision.AtPosition(room->geometry,position,false,&collision)&&collision==4&&
            room->collision.CostAtPosition(room->geometry,position,&cost)&&cost==1000&&room->collision.types[a.grid]==15,"wall-factory-produces-real-collision-query-inputs");}
    Check(!memcmp(&seeds,&saved,sizeof(seeds)),"wall-does-not-consume-descriptor-seeds");
    NativeRoomState* before=new NativeRoomState(*room);NativeSceneDependencies* deps=new NativeSceneDependencies;
    Check(NativeSceneInspectPlan(record,*plan,1,0,deps)==NSC_OK&&deps->count==0&&deps->supported==18,"wall-plan-dependency-closure");
    record.type=16;
    Check(NativeSceneInspectPlan(record,*plan,1,0,deps)==NSC_OK&&deps->count==18&&NativeSceneBuildRoom(record,seeds,1,0,35,room,&detail)==NSC_UNSUPPORTED_CONTENT&&
        !memcmp(room,before,sizeof(*room)),"wall-special-graphics-branch-remains-missing");record.type=NativeRoom(id)->type;
    Check(NativeSceneBuildRoom(record,seeds,1,9,35,room,&detail)==NSC_BAD_REQUEST&&!memcmp(room,before,sizeof(*room)),"invalid-mode-not-unsupported-content");
    ++record.stackCount;
    Check(NativeSceneBuildRoom(record,seeds,1,0,35,room,&detail)==NSC_SOURCE_ERROR&&!memcmp(room,before,sizeof(*room)),"malformed-stack-plan-not-unsupported-content");--record.stackCount;
    NativeSceneFiles missing={"missing-index","missing-root",1,0,0};unsigned char* bytes=0;unsigned int length=0,hash=0;
    Check(NativeSceneReadRecord(missing,id,&bytes,&length,&hash)==NSC_SOURCE_ERROR&&!bytes&&!length&&!hash,"unavailable-source-not-unsupported-content");
    NativeLevelLayout* layout=new NativeLevelLayout;layout->Reset();NativeTopologyRoom node=NativeTopologyStart(6,6);node.doors=0;NativePlacementContext placement={0};placement.roomConfigStage=35;
    Check(layout->Place(node,id,0x12345678,placement)==NP_OK&&layout->FinalizeDoors(),"wall-real-definition-descriptor");
    NativeWorld* world=new NativeWorld;NativeSceneFiles files={indexPath,objectRoot,1,0,29};NativeRoomSetupContext setup={1,35,false,0};NativeWorldIssue issue;
    Check(world->Start(*layout,84,0,files,&issue,&setup)==NWR_OK&&world->Current()->staticGridCount==18&&world->At(0)->configStage==35,"wall-world-owns-actual-static-actors");
    NativeRoomState* resident=world->Current();NativeRoomCollision collision=resident->collision;
    Check(world->Enter(84,0,missing,&issue,&setup)==NWR_OK&&world->Current()==resident&&world->Current()->staticGrids[0].initializations==1&&
        !memcmp(&collision,&world->Current()->collision,sizeof(collision)),"wall-resident-reentry-does-not-reinitialize-or-reread");
    setup.configStage=1;unsigned int serial=world->entrySerial;
    Check(world->Enter(84,0,files,&issue,&setup)==NWR_BAD_REQUEST&&world->Current()==resident&&world->entrySerial==serial,"world-rejects-changed-config-stage-on-reentry");
    fprintf(events,"TOTAL\tstatic_walls\t18\t1\n");delete world;delete layout;delete deps;delete before;delete room;delete plan;
}
bool BodyCoordinate(float actual,unsigned int expected){
    unsigned int a=Bits(actual),b=expected;if(a==b)return true;
    if((a&0x7fffffffU)>=0x7f800000U||(b&0x7fffffffU)>=0x7f800000U)return false;
    float delta=actual-FloatBits(b);if(delta<0)delta=-delta;
    if(delta<=0.000001f)return true;
    if((a^b)&0x80000000U)return false;return a>b?a-b<=4:b-a<=4;
}
void EntityBodies(const char* root){
    int initializations=0;NativeEntityBody body;
    const unsigned int seeds[]={1,0x12345678U,0x80000000U,0xffffffffU};
    for(int variant=0;variant<2;++variant)for(int frame=0;frame<2;++frame)for(int seed=0;seed<4;++seed){
        unsigned int gameFrame=frame?0x7fffffffU:0;
        Check(body.BeginFresh(1,variant,0,seeds[seed],gameFrame,1)==NEB_OK&&body.ready,"entity-body-original-player-properties-initialize");
        Check(body.initialSeed==seeds[seed]&&body.random.state==seeds[seed]&&body.random.index==30&&body.type==1&&body.variant==variant&&body.subtype==0,
            "entity-body-native-initial-seed-and-undrawn-index30");
        Check(body.initialFrame==(int)gameFrame&&body.frameCount==0&&body.native27c==-1&&body.state==2&&body.gridCollisionClass==5&&body.entityCollisionClass==4&&body.native169&&body.native16a&&!body.native168,
            "entity-body-base-init-boundary-not-player-specific-collision-reset");
        Check(body.position.x==0&&body.position.y==0&&body.velocity.x==0&&body.velocity.y==0&&body.previousPosition.x==0&&body.previousPosition.y==0&&
            body.previousVelocity.x==0&&body.previousVelocity.y==0,"entity-body-does-not-run-later-player-placement");
        Check(body.collision.radius==(variant?7.0f:10.0f)&&body.collision.xMultiplier==1&&body.collision.yMultiplier==1&&body.collision.points.size()==40&&
            body.mass==5&&body.friction==1&&body.collisionDamage==0&&body.hitPoints==10&&body.maxHitPoints==10&&Bits(body.shadowScale)==Bits(variant?0.1f:0.16f)&&
            body.native2a0==0&&body.native2ec==1&&Bits(body.native2fc)==0x3f666666U,"entity-body-inherited-physics-and-base-HP-not-player-hearts");
        unsigned int hash=body.StateHash(),draw;
        Check(body.BeginFresh(1,variant,0,0,0,1)==NEB_BAD_REQUEST&&body.StateHash()==hash,"entity-body-zero-seed-atomic");
        Check(body.BeginFresh(14,0,0,seeds[seed],0,1)==NEB_UNSUPPORTED_PROPERTIES&&body.StateHash()==hash,"entity-body-missing-properties-never-substitute-player");
        Check(body.BeginFresh(1,variant,9,seeds[seed],0,1)==NEB_UNSUPPORTED_PROPERTIES&&body.StateHash()==hash,"entity-body-no-unimplemented-subtype-fallback");
        Check(body.random.Next(&draw)&&draw==NativeShift(seeds[seed],4,3,17),"entity-body-draw-uses-entity-owner-not-startup-bank-index35");
        const NativeEntityBodyProperties* props=NativePreparedPlayerBody(variant);
        Check(body.collision.Resize(3.5f,2.0f,0.5f,7,*props)&&body.collision.points.size()==7,"entity-body-real-size-service-mutates-owned-samples");
        Check(body.BeginFresh(1,variant,0,seeds[seed],gameFrame,1)==NEB_OK&&body.StateHash()==hash,"entity-body-fresh-init-reconstructs-fields-and-owned-geometry");++initializations;
    }
    FILE* f=Input(root,"entity_body_shapes.tsv");if(!f)return;
    int count,cases=0;unsigned int rb,xb,yb,size;
    while(fscanf(f,"%d%u%u%u%u",&count,&rb,&xb,&yb,&size)==5){
        NativeEntityCollisionShape shape;const NativeEntityBodyProperties* props=NativePreparedPlayerBody(0);
        Check(shape.Resize(FloatBits(rb),FloatBits(xb),FloatBits(yb),count,*props)&&shape.points.size()==size,"entity-shape-native-point-count",cases);
        for(unsigned int i=0;i<size;++i){unsigned int x,y;Check(fscanf(f,"%u%u",&x,&y)==2,"entity-shape-reference-row");
            Check(i<shape.points.size()&&BodyCoordinate(shape.points[i].x,x)&&BodyCoordinate(shape.points[i].y,y),"entity-shape-native-even-then-odd-order-and-rounded-angle",i);}
        ++cases;
    }
    fclose(f);Check(cases==36,"entity-shape-reference-case-count");
    NativeEntityCollisionShape shape;const NativeEntityBodyProperties* props=NativePreparedPlayerBody(0);
    Check(shape.Resize(10,-1,-1,-1,*props)&&shape.points.size()==40&&shape.xMultiplier==1&&shape.yMultiplier==1,"entity-shape-paired-native-default-resolution");
    NativePoint last=shape.points.back();Check(shape.Resize(10,1,1,3,*props)&&shape.points.size()==40&&shape.points.back().x==last.x&&shape.points.back().y==last.y,
        "entity-shape-unchanged-size-early-return-preserves-count-and-order");
    Check(shape.Resize(9,1,1,3,*props)&&shape.points.size()==3,"entity-shape-size-change-rebuilds-count");
    Check(!shape.Resize(FloatBits(0x7fc00000U),1,1,4,*props)&&shape.radius==9&&shape.points.size()==3,"entity-shape-invalid-input-preserves-owned-shape");
    fprintf(events,"TOTAL\tentity_body\t%d\t%d\n",initializations,cases);
}
void PlayerSprites(const char* root){
    FILE* f=Input(root,"player_sprite_asset.tsv");if(!f)return;unsigned int hash=0,clips=0,layers=0,nulls=0,eventsCount=0;
    Check(fscanf(f,"%u%u%u%u%u",&hash,&clips,&layers,&nulls,&eventsCount)==5,"sprite-original-asset-binding-row");fclose(f);
    const NativeEntityBodyProperties* props=NativePreparedPlayerBody(0);const NativeSpriteAsset* asset=NativePreparedSprite(props->animationPath);
    Check(asset&&NativeSpriteAssetHash(*asset)==hash&&asset->clipCount==clips&&asset->layerCount==layers&&asset->nullCount==nulls&&asset->eventCount==eventsCount,
        "sprite-exact-original-cache-words-tracks-events-and-inherited-sheets");if(!asset)return;
    Check(clips==38&&layers==15&&nulls==8&&eventsCount==4,"sprite-original-player-record-scope");
    NativeSprite sprite,empty;unsigned int cold=sprite.StateHash();
    Check(!sprite.asset&&!sprite.mainState.clip&&!sprite.overlayState.clip&&sprite.mainState.frame==-1&&sprite.overlayState.frame==-1&&
        !sprite.mainState.playing&&!sprite.overlayState.playing&&sprite.mainState.layerFrames.empty()&&sprite.overlayState.nullFrames.empty()&&
        sprite.offset.x==0&&sprite.offset.y==0&&sprite.scale.x==1&&sprite.scale.y==1&&sprite.speed==1&&!sprite.flipX&&!sprite.flipY&&!sprite.updateEnabled,
        "sprite-native-constructor-and-clear-own-empty-animation-states");
    for(int i=0;i<7;++i)Check(sprite.color[i]==(i<4?1.0f:0)&&sprite.secondaryColor[i]==(i<3?1.0f:0),"sprite-native-primary-and-secondary-color-defaults",i);
    Check(sprite.Load("missing/actual-asset.anm2")==NSPR_ASSET_MISSING&&sprite.StateHash()==cold,"sprite-unsupported-source-is-not-substituted");
    Check(sprite.Load(props->animationPath)==NSPR_OK&&sprite.asset==asset&&sprite.updateEnabled&&!sprite.mainState.clip&&!sprite.overlayState.clip,
        "sprite-load-preserves-native-no-implicit-default-animation");
    const NativeSpriteSheet* ghostSheet=sprite.OriginalLayerSheet(sprite.LayerOrdinal("ghost"));
    unsigned int sheetAssetHash=NativeSpriteAssetHash(*asset);
    Check(ghostSheet&&sprite.SetFrame("WalkDown",0)==NSPR_OK&&sprite.SetFrame("HeadDown",0,true)==NSPR_OK,
        "sprite-sheet-replacement-original-ghost-and-stopped-animation-binding");
    if(!ghostSheet)return;
    unsigned int layerCursorHash=sprite.mainState.CursorHash(),overlayCursorHash=sprite.overlayState.CursorHash(),sheetBefore=sprite.StateHash();
    Check(sprite.ReplaceLayerSheet(-1,ghostSheet->path)==NSPR_BAD_REQUEST&&sprite.StateHash()==sheetBefore&&
        sprite.ReplaceLayerSheet(15,ghostSheet->path)==NSPR_BAD_REQUEST&&sprite.StateHash()==sheetBefore,
        "sprite-invalid-sheet-layer-preserves-instance");
    Check(sprite.ReplaceLayerSheet(0,ghostSheet->path)==NSPR_OK&&!sprite.layerSheets[0].ready&&sprite.RefreshSheets()==NSPR_OK&&
        sprite.layerSheets[0].ready&&sprite.layerSheets[0].path==ghostSheet->path&&sprite.layerSheets[0].width==ghostSheet->width&&
        sprite.layerSheets[0].height==ghostSheet->height&&sprite.mainState.CursorHash()==layerCursorHash&&sprite.overlayState.CursorHash()==overlayCursorHash&&
        sprite.mainState.frame==0&&sprite.overlayState.frame==0&&NativeSpriteAssetHash(*asset)==sheetAssetHash,
        "sprite-sheet-refresh-resolves-real-resource-without-resetting-animation-or-mutating-asset");
    Check(sprite.ReplaceLayerSheet(1,"unimplemented-sheet.png")==NSPR_OK&&!sprite.layerSheets[1].ready&&sprite.RefreshSheets()==NSPR_ASSET_MISSING&&
        !sprite.layerSheets[1].ready&&sprite.layerSheets[0].ready&&sprite.layerSheets[1].path=="unimplemented-sheet.png",
        "sprite-missing-replacement-does-not-reuse-a-loaded-texture");
    Check(sprite.Load(props->animationPath)==NSPR_OK&&sprite.layerSheets[0].path==sprite.OriginalLayerSheet(0)->path&&sprite.layerSheets[1].ready,
        "sprite-reload-reconstructs-original-per-layer-bindings");
    f=Input(root,"player_sprite_timelines.tsv");if(!f)return;char kind;unsigned int index,speed,steps,cases=0,updates=0;
    while(fscanf(f," %c%u%u%u",&kind,&index,&speed,&steps)==4){
        Check(kind=='A'&&index<asset->clipCount,"sprite-timeline-case-binding");if(kind!='A'||index>=asset->clipCount)break;
        Check(sprite.Play(asset->clips[index].name,true)==NSPR_OK,"sprite-play-original-animation");sprite.speed=FloatBits(speed);
        for(unsigned int step=0;step<steps;++step){unsigned int frame=0,playing=0,now=0,history=0,cursors=0;
            Check(fscanf(f," %c%u%u%u%u%u",&kind,&frame,&playing,&now,&history,&cursors)==6&&kind=='F',"sprite-timeline-state-row");
            Check(sprite.Update()==NSPR_OK&&Bits(sprite.mainState.frame)==frame&&sprite.mainState.playing==(playing!=0)&&sprite.mainState.events==now&&
                sprite.mainState.history==history&&sprite.mainState.CursorHash()==cursors,"sprite-native-timing-cursors-events-loop-and-finish",index);++updates;
        }++cases;
    }
    fclose(f);Check(cases==152,"sprite-real-animation-and-speed-case-count");
    sprite.speed=1;Check(sprite.Play("Hit",true)==NSPR_OK&&sprite.Update()==NSPR_OK&&sprite.mainState.frame==1&&sprite.mainState.events==1&&sprite.mainState.history==1,
        "sprite-original-hit-event-at-frame1");
    Check(sprite.SetFrame("Hit",1)==NSPR_OK&&!sprite.IsPlaying("Hit")&&sprite.IsFinished("Hit")&&sprite.mainState.events==1,"sprite-native-stopped-seek-event");
    unsigned int stopped=sprite.StateHash();Check(sprite.Update()==NSPR_OK&&sprite.StateHash()==stopped&&sprite.Play("Hit",false)==NSPR_OK&&sprite.StateHash()==stopped,
        "sprite-stopped-event-persists-and-same-nonloop-does-not-restart");
    Check(sprite.Play("Hit",true)==NSPR_OK&&sprite.mainState.frame==0&&sprite.mainState.playing&&sprite.mainState.events==0&&sprite.mainState.history==0,
        "sprite-forced-play-restarts-native-state-and-event-history");
    sprite.speed=0.5f;for(int i=0;i<16;++i)Check(sprite.Update()==NSPR_OK,"sprite-half-speed-hit-update");
    Check(sprite.mainState.frame==7.5f&&!sprite.mainState.playing,"sprite-native-finish-retains-last-frame-fraction-at00403c2d");sprite.speed=1;
    Check(sprite.SetFrame("Hit",100)==NSPR_OK&&sprite.mainState.frame==8&&!sprite.mainState.playing,"sprite-seek-clamps-to-frame-count-not-last-frame");
    Check(sprite.SetFrame("Hit",-3)==NSPR_OK&&sprite.mainState.frame==-3&&!sprite.mainState.playing,"sprite-negative-stopped-seek-is-not-clamped-to-zero");
    Check(sprite.Play("WalkDown",true)==NSPR_OK&&sprite.Play("HeadDown",true,true)==NSPR_OK,"sprite-main-and-overlay-own-independent-clips");
    for(int i=0;i<4;++i)Check(sprite.Update()==NSPR_OK,"sprite-independent-owner-update");
    Check(sprite.mainState.frame==4&&sprite.mainState.playing&&sprite.overlayState.frame==3&&!sprite.overlayState.playing&&sprite.IsPlaying("WalkDown")&&!sprite.IsPlaying("walkdown"),
        "sprite-overlay-finishes-without-stopping-main-and-names-remain-case-sensitive");
    unsigned int before=sprite.StateHash();Check(sprite.Play("WalkDown",false)==NSPR_OK&&sprite.StateHash()==before,"sprite-same-active-loop-is-not-restarted");
    Check(sprite.Select("not-an-original-animation",true)==NSPR_ANIMATION_MISSING&&sprite.StateHash()==before,"sprite-missing-animation-preserves-state");
    Check(sprite.Load("unimplemented-original-path.anm2")==NSPR_ASSET_MISSING&&sprite.StateHash()==before,"sprite-reload-missing-source-is-atomic");
    const NativeSpriteFrame* layer=sprite.LayerFrame(1);const NativeSpriteFrame* anchor=sprite.NullFrame(1,true);
    Check(layer&&anchor&&layer->delay>0&&anchor->delay>0&&!sprite.LayerFrame(999)&&!sprite.NullFrame(999),"sprite-original-layer-and-null-frame-views");
    unsigned int cursors=sprite.mainState.CursorHash();float frame=sprite.mainState.frame;
    Check(sprite.Select("WalkLeft",false)==NSPR_OK&&sprite.mainState.CursorHash()==cursors&&sprite.mainState.frame==frame&&sprite.mainState.playing,
        "sprite-native-keep-state-selection-preserves-cursors-time-and-play-state");
    sprite.speed=-1;before=sprite.StateHash();Check(sprite.Update()==NSPR_UNSUPPORTED_STATE&&sprite.StateHash()==before,"sprite-reverse-timing-remains-explicit-without-partial-advance");
    sprite.speed=FloatBits(0x7fc00000U);before=sprite.StateHash();Check(sprite.Update()==NSPR_BAD_REQUEST&&sprite.StateHash()==before,"sprite-invalid-time-preserves-both-instances");
    sprite.offset.x=17;sprite.scale.y=2;sprite.color[0]=0.5f;sprite.flipX=true;sprite.speed=0.5f;
    Check(sprite.Load(props->animationPath)==NSPR_OK&&sprite.offset.x==17&&sprite.scale.y==2&&sprite.color[0]==0.5f&&sprite.flipX&&sprite.speed==0.5f&&
        !sprite.mainState.clip&&!sprite.overlayState.clip,"sprite-native-reload-retains-transform-color-and-speed");
    sprite.Clear();Check(sprite.StateHash()==cold&&sprite.StateHash()==empty.StateHash(),"sprite-clear-releases-owned-cursors-and-restores-constructor-state");
    //Independent addressed branch fixture: original player clips have no
    //frame0 loop event. Test native mask/restart rules without changing data.
    NativeSpriteEvent event={33,0};NativeSpriteClip clip={"branch",2,true,{-1,true,0,0},0,0,0,0,&event,1};
    NativeSpriteAsset branch={"component-branch","branch",0,0,0,0,0,0,0,0,&clip,1};sprite.asset=&branch;sprite.updateEnabled=true;
    Check(sprite.Play("branch",true)==NSPR_OK&&sprite.mainState.events==2&&sprite.mainState.history==2,"sprite-frame0-trigger-and-native-bts-low-five-bits");
    sprite.speed=5;Check(sprite.Update()==NSPR_OK&&sprite.mainState.frame==0&&sprite.mainState.playing&&sprite.mainState.events==2&&sprite.mainState.history==2,
        "sprite-loop-discards-overshoot-and-restarts-frame0-events");
    NativePlayerSprites player;Check(player.BeginFresh(props->animationPath)==NSPR_OK&&player.bodyReady,"player-sprite-component-first-start");
    unsigned int playerHash=player.StateHash();Check(player.BeginFresh("missing.anm2")==NSPR_ASSET_MISSING&&player.StateHash()==playerHash,"player-sprite-component-invalid-start-preserves-owners");
    Check(player.sprites[0].Play("WalkDown",true)==NSPR_OK&&player.sprites[0].Update()==NSPR_OK&&player.BeginFresh(props->animationPath)==NSPR_OK&&
        player.StateHash()==playerHash,"player-sprite-component-fresh-start-reconstructs-original-stopped-frames");
    fprintf(events,"TOTAL\tplayer_sprites\t%u\t%u\n",cases,updates);
}
void PlayerAuxSprites(const char* root){
    FILE* f=Input(root,"player_aux_assets.tsv");if(!f)return;char path[512];unsigned int hash,clips,layers;
    std::vector<const NativeSpriteAsset*> assets;
    while(fscanf(f," %511[^\t]%u%u%u",path,&hash,&clips,&layers)==4){const NativeSpriteAsset* a=NativePreparedSprite(path);
        Check(a&&NativeSpriteAssetHash(*a)==hash&&a->clipCount==clips&&a->layerCount==layers&&!a->nullCount&&!a->eventCount,
            "aux-sprite-original-cache-or-native-XML-words-and-PNG-bindings",(int)assets.size());assets.push_back(a);}
    fclose(f);Check(assets.size()==9,"aux-sprite-nine-original-assets-ten-independent-owners");if(assets.size()!=9)return;
    for(unsigned int i=0;i<assets.size();++i)if(!assets[i])return;
    NativePlayerSprites player;Check(player.BeginFresh(NativePreparedPlayerBody(0)->animationPath)==NSPR_OK&&player.bodyReady&&player.startupAssetsReady,
        "aux-sprite-actual-player-Init-prepares-original-asset-block");
    f=Input(root,"player_aux_startup.tsv");if(!f)return;int slot,play,blend,n=0;
    while(fscanf(f," %d %511[^\t]%d%d",&slot,path,&play,&blend)==4){
        Check(slot>0&&slot<14,"aux-sprite-native-owner-slot");if(slot<=0||slot>=14)break;const NativeSprite& s=player.sprites[slot];
        Check(s.asset==NativePreparedSprite(path)&&s.asset&&s.viewportScale==(slot!=11)&&s.layerBlendPresets.size()==s.asset->layerCount,
            "aux-sprite-native-table-and-literal-paths-feed-real-owners",slot);
        Check(s.mainState.playing==(play!=0)&&(!play?!s.mainState.clip:s.mainState.clip&&!strcmp(s.mainState.clip->name,"Idle")&&s.mainState.frame==0)&&
            !s.overlayState.clip&&!s.overlayState.playing,"aux-sprite-native-Idle-only-and-no-invented-default-plays",slot);
        for(unsigned int i=0;i<s.layerBlendPresets.size();++i)Check(s.layerBlendPresets[i]==((int)i==blend?2:-1),"aux-sprite-explicit-crown-layer-blend-and-no-guessed-defaults",slot);
        ++n;
    }
    fclose(f);Check(n==10,"aux-sprite-all-native-Init-table-consumers");
    unsigned int untouched=player.sprites[5].StateHash(),assetHash=NativeSpriteAssetHash(*player.sprites[4].asset);
    Check(player.sprites[4].asset==player.sprites[5].asset&&player.sprites[4].Play("Charging")==NSPR_OK&&player.sprites[4].Update()==NSPR_OK&&
        player.sprites[4].mainState.frame==1&&player.sprites[5].StateHash()==untouched&&NativeSpriteAssetHash(*player.sprites[4].asset)==assetHash,
        "aux-sprite-shared-charge-asset-has-independent-native-instance-state");
    NativeSprite& crown=player.sprites[2];unsigned int before=crown.StateHash();
    Check(crown.SetLayerBlendPreset(2,2)==NSPR_BAD_REQUEST&&crown.StateHash()==before&&crown.SetLayerBlendPreset(1,9)==NSPR_BAD_REQUEST&&crown.StateHash()==before,
        "aux-sprite-invalid-layer-or-preset-is-atomic");
    Check(crown.Play("Idle")==NSPR_OK&&!crown.LayerFrame(1)&&crown.LayerFrame(0)&&crown.mainState.layerFrames.size()==2&&crown.mainState.nullFrames.empty(),
        "aux-sprite-original-reversed-track-order-and-empty-track-are-retained");
    Check(crown.Load("gfx/CrownOfLight.anm2")==NSPR_OK&&crown.viewportScale&&crown.layerBlendPresets[1]==-1&&!crown.mainState.clip,
        "aux-sprite-reload-retains-transform-binding-but-reconstructs-per-asset-overrides");
    before=player.StateHash();Check(player.BeginFresh("missing-original-body.anm2")==NSPR_ASSET_MISSING&&player.StateHash()==before,
        "aux-sprite-failed-player-start-preserves-every-original-owner");
    Check(player.BeginFresh(NativePreparedPlayerBody(0)->animationPath)==NSPR_OK&&player.sprites[2].layerBlendPresets[1]==2&&!player.sprites[4].mainState.clip&&
        player.sprites[11].IsPlaying("Idle")&&player.sprites[11].mainState.frame==0,"aux-sprite-fresh-start-rebuilds-overrides-and-native-play-state");
    NativeSprite charge;Check(charge.Load("gfx/chargebar.anm2")==NSPR_OK&&charge.Play("Charging")==NSPR_OK,"aux-sprite-addressed-charge-literal-start");
    for(int frame=0;frame<101;++frame)Check(charge.Update()==NSPR_OK,"aux-sprite-addressed-charge-literal-step");
    Check(charge.mainState.frame==100&&!charge.mainState.playing&&charge.mainState.events==0&&charge.mainState.nullFrames.empty(),
        "aux-sprite-original-101-frame-nonloop-finishes-at100");
    f=Input(root,"player_aux_timelines.tsv");if(!f)return;char kind;unsigned int assetIndex,clipIndex,speed,steps,cases=0,updates=0;NativeSprite sprite;
    while(fscanf(f," %c%u%u%u%u",&kind,&assetIndex,&clipIndex,&speed,&steps)==5){
        Check(kind=='A'&&assetIndex<assets.size()&&clipIndex<assets[assetIndex]->clipCount,"aux-sprite-timeline-binding");
        if(kind!='A'||assetIndex>=assets.size()||clipIndex>=assets[assetIndex]->clipCount)break;
        const NativeSpriteAsset* a=assets[assetIndex];Check(sprite.Load(a->path)==NSPR_OK&&sprite.Play(a->clips[clipIndex].name,true)==NSPR_OK,"aux-sprite-play-original-clip");sprite.speed=FloatBits(speed);
        for(unsigned int step=0;step<steps;++step){unsigned int frame=0,playing=0,now=0,history=0,cursors=0;
            Check(fscanf(f," %c%u%u%u%u%u",&kind,&frame,&playing,&now,&history,&cursors)==6&&kind=='F',"aux-sprite-timeline-state-row");
            Check(sprite.Update()==NSPR_OK&&Bits(sprite.mainState.frame)==frame&&sprite.mainState.playing==(playing!=0)&&sprite.mainState.events==now&&
                sprite.mainState.history==history&&sprite.mainState.CursorHash()==cursors,"aux-sprite-native-charge-crown-blood-timing-and-cursors",(int)assetIndex);++updates;
        }++cases;
    }
    fclose(f);Check(cases==93,"aux-sprite-all-original-clips-and-speeds");fprintf(events,"TOTAL\tplayer_aux_sprites\t%u\t%u\t%u\n",(unsigned int)assets.size(),cases,updates);
}
struct EffectObservation {int id;unsigned int count,flags,size;bool changed,has,player;};
struct EffectObserver:NativeEffectCallbacks {
    std::vector<EffectObservation> calls;
    void Capture(NativePlayerEffects& e,const NativeEffectDef& d,unsigned int count,bool player){
        EffectObservation o={d.id,count,e.flags,(unsigned int)e.Entries().size(),e.changed,e.Has(d.type,d.id),player};calls.push_back(o);
        Check(e.Update(this)==NEF_CALLBACK_REENTRY,"effect-callback-recursive-mutation-explicitly-unimplemented");
    }
    void AddCostume(NativePlayerEffects& e,const NativeEffectDef& d){Capture(e,d,0,true);}
    void Remove(NativePlayerEffects& e,const NativeEffectDef& d,unsigned int count,bool player){Capture(e,d,count,player);}
};
void PlayerEffects(const char* root){
    FILE* f=Input(root,"player_effect_defs.tsv");if(!f)return;int type,id,duration,persistent,n=0;unsigned int flags;
    while(fscanf(f,"%d%d%u%d%d",&type,&id,&flags,&duration,&persistent)==5){const NativeEffectDef* d=NativePreparedEffect(type,id);
        Check(d&&d->type==type&&d->id==id&&d->cacheFlags==flags&&d->duration==duration&&d->persistent==(persistent!=0),"effect-original-metadata-fields-and-stable-identity",n++);
        Check(d==NativePreparedEffect(type,id),"effect-catalogue-pointer-identity-stable");}
    fclose(f);Check(n==955&&!NativePreparedEffect(0,0)&&!NativePreparedEffect(3,9999),"effect-original-sparse-definitions-no-substitutions");
    const NativeEffectDef* speed=NativePreparedEffect(0,50);const NativeEffectDef* chariot=NativePreparedEffect(0,63);
    const NativeEffectDef* lost=NativePreparedEffect(0,112);const NativeEffectDef* dice=NativePreparedEffect(3,105);
    Check(speed&&chariot&&lost&&dice,"effect-addressed-fixture-definitions");if(!speed||!chariot||!lost||!dice)return;
    //Literal native/parser cases, separate from catalogue roundtripping.
    Check(speed->cacheFlags==16&&speed->duration==0&&!speed->persistent&&chariot->cacheFlags==19&&chariot->duration==300&&chariot->persistent&&
        lost->cacheFlags==224&&lost->duration==0&&lost->persistent&&dice->cacheFlags==0,"effect-native-string-bits-cooldown-and-persistent-literals");
    NativePlayerEffects e;EffectObserver observer;
    unsigned int cold=e.StateHash();Check(e.Clear(true)==NEF_OK&&e.StateHash()==cold&&e.Update()==NEF_OK&&!e.changed,"effect-fresh-empty-init-clear-needs-no-callback");
    Check(e.Add(speed,0,true)==NEF_OK&&e.StateHash()==cold&&e.Add(speed,1,true)==NEF_NEED_CALLBACKS&&e.StateHash()==cold,
        "effect-zero-count-short-circuit-and-real-costume-provider-required");
    Check(e.Add(speed,1,true,&observer)==NEF_OK&&observer.calls.size()==1&&observer.calls[0].has&&observer.calls[0].size==1&&
        observer.calls[0].flags==0&&!observer.calls[0].changed&&e.flags==16&&e.changed,"effect-new-entry-visible-before-costume-and-cache-dirty");
    Check(e.Add(speed,-1,true)==NEF_OK&&e.Count(0,50)==0&&e.Has(0,50)&&observer.calls.size()==1,
        "effect-existing-count-zero-still-present-no-second-costume-callback");
    e.flags=0;e.changed=false;Check(e.Remove(speed,0)==NEF_NEED_CALLBACKS&&e.Has(0,50),"effect-remove-zero-still-requires-native-removal-service");
    Check(e.Remove(speed,0,&observer)==NEF_OK&&!e.Has(0,50)&&observer.calls.back().count==0&&observer.calls.back().flags==0&&
        !observer.calls.back().changed&&e.flags==16&&e.changed,"effect-remove-erases-before-callback-flags-follow-callback");
    Check(e.Add(chariot,2,false)==NEF_OK&&e.Add(chariot,3,true)==NEF_OK&&e.Count(0,63)==5&&e.Entries()[0].timer==600,
        "effect-add-extends-duration-once-per-call-not-per-count");
    Check(e.Add(lost,-1,false)==NEF_OK&&e.Add(speed,1,false)==NEF_OK,"effect-ordered-persistent-and-nonpersistent-entries");
    unsigned int before=e.StateHash();Check(e.Clear(false)==NEF_NEED_CALLBACKS&&e.StateHash()==before,"effect-clear-preflights-required-provider-before-any-mutation");
    e.flags=0;e.changed=false;Check(e.Clear(false,&observer)==NEF_OK&&e.Entries().size()==2&&e.Has(0,63)&&e.Has(0,112)&&!e.Has(0,50)&&
        observer.calls.back().flags==16&&!observer.calls.back().changed&&!observer.calls.back().has,"effect-clear-keeps-native-persistent-entries-flags-precede-callback");
    e.flags=0;e.changed=false;Check(e.Remove(lost,1,&observer)==NEF_OK&&!e.Has(0,112)&&observer.calls.back().count==1,
        "effect-remove-count-is-unsigned-min-even-negative-stored-count");
    Check(e.Clear(true,&observer)==NEF_OK&&e.Entries().empty(),"effect-clear-all-removes-persistent-entry");
    NativePlayerEffects timer;EffectObserver expired;timer.Add(chariot,4,false);timer.Add(speed,1,false);
    for(int frame=0;frame<299;++frame)Check(timer.Update()==NEF_OK&&timer.Has(0,63)&&timer.Has(0,50)&&timer.flags==0,
        "effect-positive-timer-fixed-step-nonpositive-timer-persists",frame);
    before=timer.StateHash();Check(timer.Entries()[0].timer==1&&timer.Update()==NEF_NEED_CALLBACKS&&timer.StateHash()==before,
        "effect-expiry-provider-gap-does-not-decrement-or-reset-flags");
    timer.changed=false;Check(timer.Update(&expired)==NEF_OK&&!timer.Has(0,63)&&timer.Has(0,50)&&timer.flags==19&&expired.calls.size()==1&&
        expired.calls[0].count==4&&expired.calls[0].flags==19&&!expired.calls[0].changed&&!expired.calls[0].has,"effect-expiry-erases-count-and-flags-visible-before-removal-callback");
    timer.disabled=true;before=timer.StateHash();Check(timer.Update()==NEF_OK&&timer.Clear(true)==NEF_OK&&timer.Remove(speed,1)==NEF_OK&&
        timer.Add(dice,1,true)==NEF_OK&&timer.StateHash()==before&&!timer.Has(0,50)&&timer.Count(0,50)==0,"effect-disabled-freezes-all-mutations-flags-and-queries");
    timer.disabled=false;timer.playerOwner=false;timer.changed=false;timer.flags=0;
    Check(timer.Add(dice,2,true)==NEF_OK&&timer.Has(1,105)&&timer.Count(1,105)==2&&!timer.Has(0,105),"effect-collectible-query-includes-active-and-room-owner-has-no-costume-call");
    Check(timer.Remove(dice,1,&expired)==NEF_OK&&expired.calls.back().count==0&&!expired.calls.back().player&&timer.Count(1,105)==1,
        "effect-room-removal-provider-receives-native-zero-argument");
    NativeEffectDef same=*speed;Check(timer.Add(&same,1,false)==NEF_OK&&timer.Entries().size()==3,"effect-native-identity-is-definition-pointer-not-id");
    NativePlayerEffects wrap;wrap.Add(speed,2147483647,false);wrap.Add(speed,1,false);
    Check(wrap.Count(0,50)==(-2147483647-1)&&wrap.Has(0,50),"effect-add-signed32-wrap-preserves-presence");
    fprintf(events,"TOTAL\tplayer_effects\t%d\t12\n",n);
}
void PlayerState(const char* root){
    FILE* f=Input(root,"player_state_rng.tsv");if(!f)return;
    int cases=0;unsigned int seed,trinket97,draw,native12c8;
    while(fscanf(f,"%u%u%u%u",&seed,&trinket97,&draw,&native12c8)==4)for(int d6=0;d6<2;++d6){
        NativePlayerStartup startup;NativeEntityBody body;NativePlayerState state;NativeRunStart start={0,(int)(seed&1),d6!=0};
        Check(startup.BeginFresh(seed)&&body.BeginFresh(1,0,0,startup.entityInitSeed,0,1)==NEB_OK,"player-state-producer-prefix");
        std::vector<unsigned int> owners;for(int bank=0;bank<4;++bank)for(unsigned int i=0;i<startup.banks[bank].size();++i)owners.push_back(startup.banks[bank][i].state);
        Check(startup.banks[NPB_TRINKET][97].state==trinket97,"player-state-instruction-derived-child97-prefix");
        unsigned int bodyOwner=body.random.state;
        Check(state.BeginFresh(start,startup,body)&&state.ready&&state.playerType==0&&state.maxRedHearts==6&&state.redHearts==6&&state.soulHearts==0&&
            state.keys==0&&state.bombs==1&&state.coins==0&&state.blackHeartMask==0&&state.FullHealth()&&!state.LowHealth(),"player-state-original-player0-health-resources");
        Check(body.entityCollisionClass==4&&body.renderDepth==5&&body.hitPoints==1&&body.maxHitPoints==1&&body.random.state==bodyOwner&&body.position.x==0&&body.position.y==0,
            "player-state-specific-Init-overwrites-base-entity-fields-without-gameplay-placement");
        Check(state.native12c8==native12c8&&state.native12ca==0&&startup.banks[NPB_TRINKET][97].state==draw,"player-state-native-trinket97-one-draw-and-unknown-field-range");
        unsigned int index=0;for(int bank=0;bank<4;++bank)for(unsigned int i=0;i<startup.banks[bank].size();++i,++index)
            Check(startup.banks[bank][i].state==((bank==NPB_TRINKET&&i==97)?draw:owners[index])&&startup.banks[bank][i].index==35,"player-state-other-indexed-RNG-owners-unchanged");
        Check(state.collectibleCounts.size()==733&&state.trinketCounts.size()==190&&state.native166c.empty()&&state.native1678.empty()&&state.pendingCollectibles.size()==(d6?1U:0U)&&
            (!d6||state.pendingCollectibles[0]==105)&&state.collectibleCounts[105]==0,"player-state-indexed-inventory-zero-and-genuine-D6-grant-pending");
        for(unsigned int i=0;i<state.collectibleCounts.size();++i)Check(state.collectibleCounts[i]==0,"player-state-no-fabricated-collectible-grants");
        unsigned int hash=state.StateHash(),startupHash=startup.StateHash(),bodyHash=body.StateHash();
        Check(!state.BeginFresh(start,startup,body)&&state.StateHash()==hash&&startup.StateHash()==startupHash&&body.StateHash()==bodyHash,
            "player-state-rejects-replaying-fresh-Init-against-already-initialized-body");
        state.redHearts=1;Check(!state.FullHealth()&&state.LowHealth(),"player-state-health-queries-use-current-owner");
        state.effects.Add(NativePreparedEffect(0,112),-1,false);Check(state.FullHealth(),"player-state-native-lost-effect-presence-overrides-below-maximum");
        state.effects.disabled=true;Check(!state.FullHealth(),"player-state-disabled-effects-do-not-override-health");state.playerType=39;
        Check(state.FullHealth(),"player-state-native-type39-health-query-even-disabled-cache");state.playerType=0;state.effects.disabled=false;
        state.keys=7;state.coins=13;NormalRunFlow* flow=new NormalRunFlow;NativeProfile profile={0};
        Check(flow->Begin(seed,start,profile)&&flow->SupplyPlayerState(state)&&flow->StartInitialFloor()==NFR_OK&&flow->firstPlayerStateOwned&&
            flow->firstPlayerKeys==7&&flow->firstPlayerCoins==13&&flow->firstPlayerRedHearts==1&&flow->firstPlayerSoulHearts==0&&flow->fullHealthPredicate,
            "player-state-real-cache-and-resource-owner-snapshot-survives-floor-initialization");
        Check(!flow->SupplyPlayerState(state),"player-state-generation-snapshot-cannot-change-after-floor-begins");delete flow;++cases;
    }
    fclose(f);Check(cases==16,"player-state-reference-case-count");fprintf(events,"TOTAL\tplayer_state\t%d\t97\n",cases);
}
void OwnedSession() {
    PortNormalRuntime* runtime=new PortNormalRuntime;NativeMt random;random.Seed(17);int cases=0;
    unsigned int initialized=0,ticks=0,d6Stops=0,contentStops=0;
    for(unsigned int seed=1;seed<=8;++seed)for(int mode=0;mode<2;++mode)for(int gates=0;gates<4;++gates){
        PortFrontend ui;ui.Init(false);if(!ui.BindSaveDirectory("build/normal_flow/"))return;PortMenuInput accept={0},empty={0};accept.accept=true;
        ui.Step(accept);ui.Step(empty);Check(ui.scene==UI_SLOTS&&ui.Step(accept)==UI_SELECT_SLOT,"owned-session-title-to-slot-path");
        NativeProfile profile={0};profile.difficulty=mode;profile.awards[86]=(gates&1)!=0;profile.awards[342]=(gates&2)!=0;
        profile.awards[29]=(seed&1)!=0;
        ui.profile=profile;ui.SlotLoaded(true,false);ui.Step(empty);ui.Step(accept);ui.Step(empty);
        Check(ui.scene==UI_CHARACTERS&&ui.characterChoice==1&&ui.Step(accept)==UI_START_CHARACTER,"owned-session-main-new-run-to-character-action");
        PortGameSession session(random,ui);Check(session.StartNormalCharacter(seed,*runtime).result==NPS_WORLD_PENDING&&runtime->world.residentCount==0,"owned-session-start-replaces-old-world-after-valid-begin");
        unsigned int playerSeed=runtime->run.seeds.playerInit,entitySeed=playerSeed;
        for(int draw=0;draw<5;++draw)entitySeed=NativeShift(entitySeed,1,11,16);
        const NativePlayerCreation& creation=runtime->playerCreation;
        Check(creation.constructed&&creation.prefixPrepared&&creation.native1540==0&&creation.native1544==0,"owned-session-construction-and-first-player-identity-before-Init");
        for(int i=0;i<2;++i){const NativePlayerPreview& p=creation.previews[i];
            Check(p.Empty()&&p.native120==0&&p.sprite.asset==NativePreparedSprite(NativePreparedPlayerBody(0)->animationPath)&&
                !p.sprite.mainState.clip&&!p.sprite.overlayState.clip&&!p.sprite.mainState.playing&&p.sprite.mainState.frame==-1,
                "owned-session-nested-preview-load-does-not-play-default",i);
            Check(p.sprite.color[0]==1&&p.sprite.color[1]==1&&p.sprite.color[2]==1&&p.sprite.color[3]==0.5f&&
                p.sprite.color[4]==0.5f&&p.sprite.color[5]==0.5f&&p.sprite.color[6]==0.5f&&p.nativeColorTail[0]==1&&p.nativeColorTail[3]==0,
                "owned-session-original-nested-preview-color",i);
        }
        unsigned int value=123;
        Check(creation.cache.Scalar(0x20c,&value)&&value==0xffffffffU&&creation.cache.Scalar(0x328,&value)&&value==0xffffffffU&&
            !creation.cache.Scalar(0,&value)&&!creation.cache.Scalar(0x5b4,&value),"owned-session-cache-constructor-writes-only-defined-bytes");
        unsigned char byte=123;
        Check(creation.constructorFields.Byte(0x14ac,&byte)&&byte==1&&creation.constructorFields.Byte(0x1734,&byte)&&byte==0&&
            creation.constructorFields.Scalar(0x14c4,&value)&&value==0&&creation.constructorFields.Scalar(0x13ac,&value)&&value==0x40600000U,
            "owned-session-original-prefix-controls-active-slots-and-initial-damage");
        Check(creation.weapon.ready&&creation.weapon.playerToken==0&&creation.weapon.native28==1&&creation.weapon.nativeC==10&&
            creation.weapon.native8==0&&creation.weapon.native3c==0&&creation.weapon.native40==0&&creation.weapon.native44==0,
            "owned-session-required-static-factory-constructs-and-initializes-default-weapon");
        Check(creation.costumesReset&&creation.costumeCapacity==32,"owned-session-reserves-empty-costume-storage");
        for(int i=0;i<15;++i){const NativePlayerCostumeSlot& slot=creation.costumeSlots[i];
            Check(slot.native0==-1&&slot.native4==-1&&slot.native8==-1&&slot.nativeC==((i>=1&&i<=3)||i==11),"owned-session-native-empty-costume-appearance-refresh",i);}
        Check(creation.appearancePrepared&&creation.playerName=="Isaac"&&creation.native17a4==-1&&creation.native17a8==-1,
            "owned-session-original-player-skin-selectors-and-English-localized-name");
        const NativeSprite& appearance=runtime->playerSprites.sprites[0];
        Check(appearance.layerSheets.size()==15&&appearance.LayerOrdinal("ghost")==13&&appearance.LayerOrdinal("Ghost")==-1,
            "owned-session-original-exact-layer-name-lookup");
        for(unsigned int i=0;i<appearance.layerSheets.size();++i){const NativeSpriteLayerSheet& sheet=appearance.layerSheets[i];
            const NativeSpriteSheet* original=appearance.OriginalLayerSheet((int)i);
            Check(original&&sheet.ready&&sheet.path==(i==13?original->path:"resources/gfx/characters/costumes/character_001_isaac.png")&&
                sheet.width==(i==13?original->width:512)&&sheet.height==(i==13?original->height:512),
                "owned-session-replaces-per-layer-skin-and-restores-original-ghost-sheet",i);}
        Check(creation.previews[0].sprite.layerSheets[13].path==creation.previews[0].sprite.OriginalLayerSheet(13)->path&&
            creation.previews[0].sprite.layerSheets[13].ready,"owned-session-body-replacements-do-not-mutate-preview-or-source-asset");
        Check(creation.resetTailPrepared&&creation.native1fb0Owned&&creation.native1fac==0&&creation.native1e04.size()==98&&
            creation.native1e14.size()==15&&creation.native1ec4.size()==733&&creation.native1ed0.size()==190,
            "owned-session-original-reset-tail-indexed-containers-and-null-reference-owner");
        bool resetArraysEmpty=true;
        for(unsigned int i=0;i<creation.native1e04.size();++i)resetArraysEmpty=resetArraysEmpty&&!creation.native1e04[i];
        for(unsigned int i=0;i<creation.native1e14.size();++i)resetArraysEmpty=resetArraysEmpty&&!creation.native1e14[i];
        for(unsigned int i=0;i<creation.native1ec4.size();++i)resetArraysEmpty=resetArraysEmpty&&!creation.native1ec4[i];
        for(unsigned int i=0;i<creation.native1ed0.size();++i)resetArraysEmpty=resetArraysEmpty&&!creation.native1ed0[i];
        Check(resetArraysEmpty,"owned-session-clears-native-indexed-reset-storage");
        Check(creation.constructorFields.Scalar(0x14a8,&value)&&value==0xffff&&creation.constructorFields.Scalar(0x1db8,&value)&&value==0xc61c3c00U&&
            creation.constructorFields.Byte(0x1f68,&byte)&&byte==0xff,"owned-session-reached-reset-tail-values-before-resource-call");
        Check(runtime->playerStartup.ready&&runtime->playerStartup.manager.state==playerSeed&&runtime->playerStartup.manager.index==13&&
            runtime->playerStartup.entityInitSeed==entitySeed,"owned-session-creates-indexed-owners-in-creation-caller");
        NativeRng root;root.Set(playerSeed,5);unsigned int bankSeed=0;root.Next(&bankSeed);root.Next(&bankSeed);
        NativeRng trinket;trinket.Set(bankSeed,7);unsigned int child=0;for(int i=0;i<=97;++i)trinket.Next(&child);
        Check(runtime->playerStartup.banks[NPB_TRINKET][97].state==NativeShift(child,5,9,7)&&runtime->playerState.native12c8==12+NativeShift(child,5,9,7)%9,
            "owned-session-consumes-trinket97-exactly-once-after-weapon-Init");
        Check(runtime->playerState.ready&&runtime->playerState.canShoot&&runtime->run.player.isaacD6==profile.awards[29]&&
            runtime->playerState.maxRedHearts==6&&runtime->playerState.redHearts==6&&runtime->playerState.bombs==1&&runtime->playerState.pendingCollectibles.empty()&&
            runtime->playerState.collectibleCounts[105]==0&&runtime->playerBody.entityCollisionClass==4&&runtime->playerBody.renderDepth==5&&runtime->playerBody.hitPoints==1&&runtime->playerBody.native168&&runtime->playerBody.native16a,
            "owned-session-applies-player-metadata-resources-before-genuine-award29-grant");
        Check(runtime->playerBody.ready&&runtime->playerBody.initialSeed==entitySeed&&runtime->playerBody.random.state==entitySeed&&runtime->playerBody.random.index==30&&
            runtime->playerBody.collision.radius==10&&runtime->playerBody.collision.points.size()==40&&runtime->playerBody.position.x==320&&runtime->playerBody.position.y==400,
            "owned-session-body-geometry-registration-position-before-room-placement");
        const NativeSprite& sprite=runtime->playerSprites.sprites[0];
        Check(runtime->playerSprites.bodyReady&&runtime->playerSprites.startupAssetsReady&&sprite.mainState.clip&&sprite.overlayState.clip&&
            !strcmp(sprite.mainState.clip->name,"WalkDown")&&!strcmp(sprite.overlayState.clip->name,"HeadDown")&&sprite.mainState.frame==0&&sprite.overlayState.frame==0&&
            !sprite.mainState.playing&&!sprite.overlayState.playing,"owned-session-original-body-and-head-state");
        Check(runtime->phase==PNR_GENERATING&&runtime->run.phase==NRF_TOPOLOGY&&runtime->run.steps==0&&
            runtime->world.entrySerial==0&&runtime->world.residentCount==0&&runtime->missingNativePrerequisite==0,
            "owned-session-start-commits-generation-before-world-entry");
        NativeSceneFiles files={indexPath,objectRoot,runtime->run.context.stageType+1,mode,0};
        NormalRunResult advanced=NFR_PENDING;
        for(int step=0;step<1000&&(runtime->phase==PNR_GENERATING||runtime->phase==PNR_ROOM_COMPONENTS);++step){
            NativeActionFrame frame;frame.Clear(runtime->playerCreation.native153c,runtime->playerBody.entityToken,(unsigned int)step);
            frame.SupplyConnection(true);frame.SupplyDrop(false);
            for(int action=0;action<11;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
            for(int action=0;action<8;++action)frame.SupplyValue(action,0);
            NativeFrameActions frameActions(frame);
            advanced=session.AdvanceNormalSetup(*runtime,files,&frameActions);
            files.backdrop=runtime->run.context.stageType+1;
        }
        const bool d6=runtime->run.player.isaacD6;
        unsigned int headTimerX=0,headTimerY=0,headCalls=0;
        const bool headTimers=runtime->playerCreation.constructorFields.Scalar(0x1280,&headTimerX)&&
            runtime->playerCreation.constructorFields.Scalar(0x1284,&headTimerY)&&
            runtime->playerCreation.constructorFields.Scalar(0x1e88,&headCalls);
        if(runtime->phase==PNR_READY)Check(advanced==NFR_PENDING&&runtime->world.residentCount==1&&
            runtime->completePlayerInitializations==1&&runtime->completePlayerTicks>=1&&
            runtime->completeWholeGameplayTicks==runtime->completePlayerTicks&&runtime->missingNativePrerequisite==0&&
            runtime->playerBody.position.x==320&&runtime->playerBody.position.y==380&&headTimers&&
            headTimerX==0xffffffffU&&headTimerY==0xffffffffU&&headCalls==runtime->completePlayerTicks,
            "owned-session-complete-player-and-first-whole-tick");
        else if(d6&&runtime->missingNativePrerequisite==0x713120){++d6Stops;
            Check(advanced==NFR_PENDING&&runtime->phase==PNR_NEED_NATIVE_INIT&&runtime->world.residentCount==1&&
            runtime->completePlayerInitializations==0&&runtime->completePlayerTicks==0,
            "owned-session-earned-award29-stops-at-genuine-D6-grant");}
        else {++contentStops;
            Check(advanced==NFR_PENDING&&runtime->phase!=PNR_ERROR&&runtime->phase!=PNR_SOURCE_ERROR&&
            runtime->completePlayerInitializations==0&&runtime->completePlayerTicks==0&&runtime->missingNativePrerequisite!=0,
            "owned-session-profile-content-stops-before-partial-player");}
        unsigned int startupHash=runtime->playerStartup.StateHash(),bodyHash=runtime->playerBody.StateHash(),spriteHash=runtime->playerSprites.StateHash(),
            playerHash=runtime->playerState.StateHash(),creationHash=creation.StateHash();const PortNormalPhase donePhase=runtime->phase;
        Check(!runtime->playerCreation.PreparePrefix(runtime->run.player,playerSeed,runtime->playerStartup,runtime->playerBody,runtime->playerSprites,runtime->playerState,runtime->playerManager,ui.controller)&&
            runtime->playerStartup.StateHash()==startupHash&&runtime->playerState.StateHash()==playerHash&&runtime->playerCreation.StateHash()==creationHash,
            "owned-session-cannot-run-construction-prefix-twice-on-existing-owner");
        Check(!ui.characterPending&&ui.scene==UI_CHARACTERS&&!ui.hasContinue&&!memcmp(&profile,&ui.profile,sizeof(profile)),
            "owned-session-completed-setup-preserves-profile-and-frontend-owner");
        ui.characterChoice=2;
        Check(session.StartNormalCharacter(seed,*runtime).result==NPS_LOCKED&&runtime->phase==donePhase&&runtime->playerStartup.StateHash()==startupHash&&runtime->playerBody.StateHash()==bodyHash&&
            runtime->playerSprites.StateHash()==spriteHash&&runtime->playerState.StateHash()==playerHash&&runtime->playerCreation.StateHash()==creationHash,
            "owned-session-invalid-start-preserves-all-existing-owners");
        ui.characterChoice=1;
        Check(session.StartNormalCharacter(0,*runtime).result==NPS_BAD_REQUEST&&runtime->phase==donePhase&&runtime->playerStartup.StateHash()==startupHash&&
            runtime->playerBody.StateHash()==bodyHash&&runtime->playerSprites.StateHash()==spriteHash&&runtime->playerState.StateHash()==playerHash&&
            runtime->playerCreation.StateHash()==creationHash,"owned-session-zero-seed-preserves-existing-creation");
        initialized+=runtime->completePlayerInitializations;ticks+=runtime->completePlayerTicks;
        fprintf(events,"SESSION_PREFIX\t%u\t%d\t%d\t%d\t%u\t%u\t%u\n",seed,mode,gates,profile.awards[29]?1:0,runtime->missingNativePrerequisite,startupHash,creation.weapon.StateHash());++cases;
    }
    //Authentic profile cases either complete the initialized player and its
    //first whole collision tick, stop at the genuine award29/D6 grant, or
    //stop at their exact remaining content/asset caller. No partial player
    //tick is accepted, and at least one ordinary profile must complete.
    Check(initialized>0&&ticks>=initialized&&d6Stops>0,"owned-session-complete-player-and-first-whole-tick-by-authentic-profile");
    fprintf(events,"TOTAL\towned_session\t%d\t%u\t%u\t%u\n",cases,ticks,d6Stops,contentStops);
    fprintf(events,"TOTAL\tplayer_initializations\t%u\n",initialized);delete runtime;
}
void PlayerStartup(const char* root) {
    FILE* f=Input(root,"player_startup.tsv");if(!f)return;int cases=0;
    unsigned int seed,entity,hash,sample[12];NativePlayerStartup startup;
    while(fscanf(f,"%u%u%u",&seed,&entity,&hash)==3){
        for(int i=0;i<12;++i)Check(fscanf(f,"%u",sample+i)==1,"player-startup-vector-complete");
        Check(startup.BeginFresh(seed)&&startup.ready&&startup.manager.state==seed&&startup.manager.index==13&&startup.entityInitSeed==entity&&startup.StateHash()==hash,
            "player-startup-exact-native-seed-tree-and-indexed-banks",cases);
        const unsigned int counts[4]={733,190,50,98};
        for(int bank=0;bank<4;++bank){const std::vector<NativeRng>& b=startup.banks[bank];
            Check(b.size()==counts[bank]&&b[0].state==sample[bank*3]&&b[1].state==sample[bank*3+1]&&b.back().state==sample[bank*3+2],"player-startup-bank-first-second-last",bank);
            for(unsigned int i=0;i<b.size();++i)Check(b[i].index==35&&b[i].state!=0,"player-startup-child-owner-index-not-bank-shift");}
        unsigned int before=startup.StateHash();Check(!startup.BeginFresh(0)&&startup.StateHash()==before,"player-startup-invalid-seed-preserves-all-owners");
        unsigned int other[3];for(int bank=1;bank<4;++bank)other[bank-1]=startup.banks[bank][0].state;
        NativeRng& selected=startup.banks[0][0];unsigned int previous=selected.state,draw;
        Check(selected.Next(&draw)&&draw==NativeShift(previous,5,9,7)&&selected.state==draw,"player-startup-owned-child-uses-default35");
        Check(startup.manager.state==seed&&startup.entityInitSeed==entity&&startup.banks[0][1].state==sample[1]&&
            startup.banks[1][0].state==other[0]&&startup.banks[2][0].state==other[1]&&startup.banks[3][0].state==other[2],"player-startup-child-advance-is-isolated-from-other-owners");
        Check(startup.BeginFresh(seed)&&startup.StateHash()==before,"player-startup-fresh-lifecycle-reconstructs-owned-seeds");++cases;
    }
    fclose(f);Check(cases==8,"player-startup-reference-case-count");fprintf(events,"TOTAL\tplayer_startup\t%d\t1071\n",cases);
}
void RetryContinuation() {
    NormalRunFlow* run=new NormalRunFlow;NativeProfile p={0};NativeRunStart player={0,0,false};
    Check(run->Begin(1,player,p)&&run->StartInitialFloor()==NFR_OK,"retry-continuation-real-setup");
    NormalRunFlow* control=new NormalRunFlow(*run);run->floor.attempts=100;
    NormalRunResult actual=run->Step(),expected=control->Step();
    Check(actual==expected&&actual!=NFR_ATTEMPT_GUARD&&run->floor.attempts==101&&run->phase==control->phase&&
        run->floor.plan.topologyStream.state==control->floor.plan.topologyStream.state&&run->topology.rng.state==control->topology.rng.state,
        "generation-attempt101-continues-with-original-owner-order");
    Check(run->phase!=NRF_BLOCKED,"test-budget-does-not-create-native-generation-failure");
    fprintf(events,"TOTAL\tretry_continuation\t1\n");delete control;delete run;
}
void Frontend() {
    NativeMt random;random.Seed(17);NativeMt previous=random;PortFrontend ui;ui.Init(false);ui.OpenCharacters();ui.characterChoice=1;ui.characterMode=0;ui.characterPending=true;
    NativeProfile profile=ui.profile;NormalRunFlow* run=new NormalRunFlow;memset(run,0,sizeof(*run));
    PortSessionStart start=PortGameSession(random,ui).StartNormalCharacter(0x12345678,*run);
    Check(start.result==NPS_WORLD_PENDING&&run->phase==NRF_TOPOLOGY&&run->context.effectiveCurses==0,"frontend-starts-normal-flow-with-proven-first-curse-context");
    Check(ui.scene==UI_CHARACTERS&&ui.characterPending&&!ui.hasContinue,"frontend-holds-world-during-normal-setup");
    for(int step=0;step<1000&&ui.characterPending;++step)PortGameSession(random,ui).AdvanceNormalSetup(*run);
    Check(run->phase==NRF_NEED_ROOM_ENTRY&&ui.scene==UI_CHARACTERS&&!ui.characterPending,"frontend-advances-floor-descriptor-generation");
    NativeWorld* world=new NativeWorld;NativeSceneFiles files={indexPath,objectRoot,1,0};NativeWorldIssue issue;
    Check(PortGameSession(random,ui).PrepareNormalWorld(*run,*world,files,&issue)==NWR_OK&&world->Current()&&world->Current()->variant==2&&world->CurrentDescriptor()->gridIndex==84,"session-selected-start-room-activation");
    Check(world->At(world->currentList)->setupKnown,"session-original-start-native-water-pit-setup");
    NativePlayerInputContext input={0};input.playerType=run->player.player;input.stage=run->context.stage;input.stageType=run->context.stageType;input.dimension=world->layout.currentDimension;
    input.playerToken=0x12345678;input.controlsEnabled=true;NativeActionFrame frame;frame.Clear(0,input.playerToken,7);
    for(int k=0;k<8;++k)frame.SupplyValue(k,k==1?.75f:k==6?.5f:0);frame.SupplyDrop(false);
    NativeFrameActions source(frame);NativePoint movement={0,0},shooting={0,0};NormalRunFlow* beforeInput=new NormalRunFlow(*run);
    Check(PortGameSession(random,ui).EvaluateNormalInput(*run,*world,input,source,false,&movement)==NPI_OK&&movement.x==.75f&&movement.y==0,"session-selected-world-evaluated-movement-input");
    Check(PortGameSession(random,ui).EvaluateNormalInput(*run,*world,input,source,true,&shooting)==NPI_OK&&shooting.x==0&&shooting.y==-.5f,"session-selected-world-evaluated-shooting-input");
    NativePlayerVelocityContext motion={0};motion.input=input;motion.moveSpeed=1;motion.native2ec=1;
    NativePlayerVelocityState velocity={0};MotionProbe evaluated;evaluated.effectOwner=input.playerToken;evaluated.frames[0]=frame;NativePlayerVelocityIssue pending;
    NativePlayerVelocityState expectedVelocity=velocity;MotionProbe direct;direct.effectOwner=input.playerToken;direct.frames[0]=frame;
    Check(NativePlayerVelocityCore(motion,direct,direct,&expectedVelocity)==NPV_RETURNED&&
        PortGameSession(random,ui).EvaluateNormalVelocity(*run,*world,motion,evaluated,evaluated,&velocity,&pending)==NPV_RETURNED&&pending.nextAddress==0x72c3c4&&
        !memcmp(&velocity,&expectedVelocity,sizeof(velocity))&&velocity.velocity.x>0&&velocity.velocity.y==0&&evaluated.hash==direct.hash,"session-selected-world-velocity-completes-native-effect-tail");
    NativePlayerVelocityState savedVelocity=velocity;MotionProbe invalidMotion;motion.input.stageType=5;
    Check(PortGameSession(random,ui).EvaluateNormalVelocity(*run,*world,motion,invalidMotion,invalidMotion,&velocity,&pending)==NPV_BAD_REQUEST&&invalidMotion.count==0&&
        !memcmp(&velocity,&savedVelocity,sizeof(velocity)),"session-velocity-rejects-wrong-world-before-actions-and-effects");motion.input=input;
    input.dimension=(input.dimension+1)%3;InputProbe wrong(frame);NativePoint unchanged={123,456};
    Check(PortGameSession(random,ui).EvaluateNormalInput(*run,*world,input,wrong,false,&unchanged)==NPI_BAD_REQUEST&&wrong.count==0&&unchanged.x==123&&unchanged.y==456,"session-input-world-context-mismatch-before-query");
    input.dimension=world->layout.currentDimension;unsigned int originalSeed=world->layout.rooms[world->currentList].placementSeed;
    world->layout.rooms[world->currentList].placementSeed^=1;InputProbe stale(frame);
    Check(PortGameSession(random,ui).EvaluateNormalInput(*run,*world,input,stale,false,&unchanged)==NPI_BAD_REQUEST&&stale.count==0&&unchanged.x==123&&unchanged.y==456,"session-input-selected-world-owner-mismatch-before-query");
    world->layout.rooms[world->currentList].placementSeed=originalSeed;
    Check(!memcmp(beforeInput,run,sizeof(*run)),"session-input-does-not-consume-generation-owners-or-change-progression");delete beforeInput;
    Check(ui.scene==UI_CHARACTERS,"room-component-construction-does-not-claim-complete-native-entry");
    Check(!memcmp(&random,&previous,sizeof(random))&&!memcmp(&profile,&ui.profile,sizeof(profile)),"normal-setup-preserves-explicit-menu-random-owner-and-profile");
    delete world;delete run;
}
}
int RunNormalFlowChecks(const char* input,const char* output,int seeds) {
    events=fopen(output,"wb");if(!events)return 2;
    Catalog(input);RngReferences(input);RoomEntry(input);Selectors(input);Floors(input);MidSpecials(input);SecretGeometry(input);SecretOwners();Offgrid(input);LevelDoors(input);DoorActors(input);RoomSpace(input);PlayerInput(input);PlayerVelocity(input);WorldStorage();InitialStages();InitialCaller();StaticWalls(input);PlayerStartup(input);EntityBodies(input);PlayerSprites(input);PlayerAuxSprites(input);PlayerEffects(input);PlayerState(input);OwnedSession();RetryContinuation();Frontend();
    char trace[768];sprintf(trace,"%s.flows.tsv",output);Flows(seeds,trace);
    fprintf(events,"TOTAL\tconstruction_contexts\t%d\t%d\n",contextualConstructions,missingDependencies);
    fprintf(events,"TOTAL\trecord_cache\t%d\t%u\n",rawReads,cachedRecordBytes);recordCache.clear();
    fprintf(events,"TOTAL\tchecks\t%d\t%d\t%d\t%d\t%d\n",checks,failures,rawReads,constructed,constructionGaps);fclose(events);
    printf("Normal flow: %d checks, %d failures; %d selected raw reads, %d constructions, %d construction gaps.\n",checks,failures,rawReads,constructed,constructionGaps);
    return failures?1:0;
}
#if defined(_XBOX)
// Same checks/engine, fixed target transport. Packet staging/target compilation
// and execution are reserved for an explicit user request; not host evidence.
VOID __cdecl main() {
    int result=RunNormalFlowChecks("game:\\port\\normal_flow\\data","game:\\port\\normal_flow\\events.tsv",128);
    FILE* status=fopen("game:\\port\\normal_flow\\status.txt","wb");if(status){fprintf(status,"%d\n",result);fclose(status);}
    for(;;)Sleep(1000);
}
#else
int main(int argc,char** argv) {return argc==4?RunNormalFlowChecks(argv[1],argv[2],atoi(argv[3])):2;}
#endif
