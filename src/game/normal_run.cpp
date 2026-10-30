#include "normal_run.h"
#include <string.h>
namespace {
bool Byte(const NativeProfile& p,int offset) {return p.Award(offset-0x44);}
NativeBossIdentityContext BossContext(const NormalRunFlow& run) {
    NativeBossIdentityContext c;memset(&c,0,sizeof(c));c.stage=run.context.stage;c.stageType=run.context.stageType;c.nativeMode=run.context.nativeMode;
    c.profile66=Byte(run.profile,0x66);c.profile86=Byte(run.profile,0x86);c.profile49=Byte(run.profile,0x49);
    const NativeBossIdentityData* data=NativePreparedBossIdentityData(12);
    for(int i=0;i<data->count;++i)if(data->entries[i].id<kNativeBossIds)c.unlocked[data->entries[i].id]=run.profile.Award(data->entries[i].achievement);
    return c;
}
NativeMidSpecialContext MidContext(const NormalRunFlow& run) {
    NativeMidSpecialContext c;memset(&c,0,sizeof(c));c.placement=run.placement;c.stage=run.context.stage;c.stageType=run.context.stageType;
    c.firstPlayerKeys=run.firstPlayerKeys;c.firstPlayerCoins=run.firstPlayerCoins;c.fullHealthPredicate=run.fullHealthPredicate;
    c.levelChallengeSubtype=NativeNormalChallengeSubtype(c.stage);c.profile48=Byte(run.profile,0x48);c.profile65=Byte(run.profile,0x65);
    //006b3140 clears both planetarium counters at New Run; stage1 has no
    //skipped treasure floors. Current player0 inventory has no chance trinkets.
    c.planetariumChance=0.01f;c.profile1da=Byte(run.profile,0x1da);
    //00700030/00700160: ordinary first chapter, no challenge/run override,
    //default configuration byte88=0 (0042042c), stage<8, config stage!=36.
    //00703050 is false before effective stage6. Native mode is0/1.
    c.eligibleRoom19=!(run.runFlagsHigh&0x10000);
    c.lowHealthPlayer0=(run.firstPlayerRedHearts<2&&run.firstPlayerSoulHearts<1)||(run.firstPlayerRedHearts<1&&run.firstPlayerSoulHearts<3);
    return c; // Current supported player0 has no qualifying009098b0(2) owner.
}
NativeSecretContext SecretContext(const NormalRunFlow& run) {
    NativeSecretContext c;memset(&c,0,sizeof(c));c.placement=run.placement;c.stage=run.context.stage;c.startGridIndex=NativeCellIndex(run.topology.rooms[0].origin);return c;
}
}
bool NormalRunFlow::Begin(unsigned int seed,const NativeRunStart& start,const NativeProfile& p) {
    if(!NativeRunStartValid(start)||!seed)return false;
    NativeSeeds next;if(!next.SetStart(seed))return false;
    const NativeBossIdentityData* data=NativePreparedBossIdentityData(12);if(!data)return false;
    NativeBossIdentityManager nextBosses;if(!NativeInitializeRunBosses(next,*data,&nextBosses))return false;
    const NativePlayerDef* playerData=NativePlayer(start.player);if(!playerData)return false;
    seeds=next;player=start;profile=p;pool.Reset(NC_NORMAL);layout.Reset();bosses=nextBosses;
    //6b30a9 ->924f30: the second 30-word statistics array at+c0 is cleared
    //at New Run.7a3c53 updates it on the first visit to each descriptor.
    memset(roomTypeVisits,0,sizeof(roomTypeVisits));
    memset(&context,0,sizeof(context));memset(&floor,0,sizeof(floor));memset(&topology,0,sizeof(topology));
    memset(&bossSequence,0,sizeof(bossSequence));memset(&bossEligibility,0,sizeof(bossEligibility));memset(&placement,0,sizeof(placement));memset(&midReport,0,sizeof(midReport));
    firstPlayerKeys=playerData->keys;firstPlayerCoins=playerData->coins;fullHealthPredicate=false;minibossPlaced=false;runFlagsLow=runFlagsHigh=retainedRoomSeed=0;
    firstPlayerRedHearts=playerData->hp;firstPlayerSoulHearts=playerData->armor;firstPlayerStateOwned=false;
    memset(&secretReport,0,sizeof(secretReport));memset(&ordinaryReport,0,sizeof(ordinaryReport));
    memset(&offgridReport,0,sizeof(offgridReport));
    levelSeed=0;currentGrid=previousGrid=-1;
    phase=NRF_NEED_FLOOR_CONTEXT;result=NFR_PENDING;lastDefinition=-1;lastCode=steps=rejections=0;return true;
}
bool NormalRunFlow::SupplyPlayerState(const NativePlayerState& state) {
    if(phase!=NRF_NEED_FLOOR_CONTEXT||!state.ready||state.playerType!=player.player)return false;
    firstPlayerKeys=state.keys;firstPlayerCoins=state.coins;firstPlayerRedHearts=state.redHearts;firstPlayerSoulHearts=state.soulHearts;
    fullHealthPredicate=state.FullHealth();firstPlayerStateOwned=true;return true;
}
bool NormalRunFlow::InitialStageType(int* stageType) const {
    if(!stageType||phase!=NRF_NEED_FLOOR_CONTEXT||!seeds.Valid())return false;
    unsigned int seed=seeds.stage[1];int type=!(seed&1)&&Byte(profile,0x9a)?1:0;
    if(seed%3==0&&Byte(profile,0x19a))type=2;
    *stageType=type;return true;
}
NormalRunResult NormalRunFlow::SupplyInitialFloor(unsigned int curses) {
    int type;if(!InitialStageType(&type))return NFR_BAD_REQUEST;
    NativeFloorContext c={1,type,player.mode,0,curses,0,0,false,false,NC_NORMAL};return SupplyFloorContext(c);
}
NormalRunResult NormalRunFlow::StartInitialFloor() {
    if(phase!=NRF_NEED_FLOOR_CONTEXT)return NFR_BAD_REQUEST;
    //006fa1f4..021d forces denominator0 on stage1 unless byte65 is set.
    //006fa267 clears base curses. New-run006b2ff0 clears the global added mask;
    //normal starting player0/D6 has no curse-removal item or seed-effect masks.
    if(Byte(profile,0x65))return result=NFR_PENDING;
    return SupplyInitialFloor(0);
}
NormalRunResult NormalRunFlow::SupplyFloorContext(const NativeFloorContext& c,const NativeRng* evaluatedCurseOwner) {
    if(phase!=NRF_NEED_FLOOR_CONTEXT||c.catalog!=NC_NORMAL||c.nativeMode!=player.mode)return NFR_BAD_REQUEST;
    // First ordinary chapter without scripted/configured-room/item overrides.
    // Stage type and effective curses are explicit caller inputs, not inferred
    // from a discovered file or a fabricated zero-valued native service.
    if(c.stage!=1||c.stageType<0||c.stageType>2||c.challenge||c.gameFlags||c.runCounter||c.held599||c.reservationProfileGate)return NFR_BAD_REQUEST;
    NativeLevelSeeds level;if(!NativeInitNormalLevelSeeds(seeds,c.stage,c.stageType,&level))return NFR_BAD_REQUEST;
    if(evaluatedCurseOwner) {if(!evaluatedCurseOwner->state||evaluatedCurseOwner->index!=35)return NFR_BAD_REQUEST;level.setup=*evaluatedCurseOwner;}
    else if(c.effectiveCurses||Byte(profile,0x65))return NFR_BAD_REQUEST;
    NativeFloorResult r=floor.Begin(level,c);if(r!=NF_OK)return Block(NFR_MISSING_DATA,r);
    context=c;memset(&placement,0,sizeof(placement));placement.roomConfigStage=floor.plan.configStage;placement.nativeMode=c.nativeMode;
    //The owned session already supplied the actual numeric/cache snapshot.
    //The standalone generation component retains its metadata-derived fixture
    //fallback. It does not claim initialized player execution.
    if(!firstPlayerStateOwned){const NativePlayerDef* initial=NativePlayer(player.player);firstPlayerKeys=initial->keys;
        fullHealthPredicate=initial->hp+initial->armor>=initial->hp;}
    memset(&bossEligibility,0,sizeof(bossEligibility));
    bossEligibility.subtype20=Byte(profile,0x54);bossEligibility.subtype21=Byte(profile,0x55);bossEligibility.subtype19=Byte(profile,0x56);
    bossEligibility.subtype42=Byte(profile,0x88);bossEligibility.subtypes57to66=Byte(profile,0x19e);bossEligibility.subtypes67to72=Byte(profile,0x19f);
    phase=NRF_TOPOLOGY;return result=NFR_OK;
}
NormalRunResult NormalRunFlow::Block(NormalRunResult why,int code) {phase=NRF_BLOCKED;lastCode=code;return result=why;}
NormalRunResult NormalRunFlow::Reject(int code) {
    if(!floor.RejectContent(&topology))return Block(NFR_BAD_REQUEST,code);
    lastCode=code;++rejections;phase=NRF_TOPOLOGY;return result=NFR_NATIVE_RETRY;
}
NormalRunResult NormalRunFlow::Step() {
    ++steps;
    if(phase==NRF_NEED_FLOOR_CONTEXT||phase==NRF_NEED_ROOM_ENTRY)return result=NFR_PENDING;
    if(phase==NRF_BLOCKED)return result;
    if(phase==NRF_TOPOLOGY) {
        layout.Reset();minibossPlaced=false;NativeFloorResult r=NativeStepFloorWithBosses(floor,bosses,&topology);
        if(r==NF_RETRY)return result=NFR_NATIVE_RETRY;
        if(r!=NF_CONTENT_READY)return Block(r==NF_ATTEMPT_LIMIT?NFR_ATTEMPT_GUARD:NFR_BAD_REQUEST,r);
        NativeBossRequest b;memset(&b,0,sizeof(b));b.nativeMode=context.nativeMode;b.effectiveConfigStage=floor.plan.configStage;b.effectiveCurses=context.effectiveCurses;b.eligibility=bossEligibility;
        NativeBossSequenceResult begun=bossSequence.Begin(context.stage,false,b,placement);
        if(begun!=NBS_OK)return Block(NFR_BAD_REQUEST,begun);
        phase=NRF_BOSS;return result=NFR_OK;
    }
    if(phase==NRF_BOSS) {
        NativeManagedBossReport report;memset(&report,0,sizeof(report));NativeBossIdentityContext c=BossContext(*this);c.stage=bossSequence.IdentityStage();
        NativeBossSequenceResult r=NativeAdvanceManagedBoss(bossSequence,bosses,c,topology,pool,floor.plan.streams.layout,layout,&report);
        lastDefinition=report.placement.selection.definition;
        if(r==NBS_NEED_IDENTITY)return result=NFR_OK;
        if(r!=NBS_OK) {
            if(r!=NBS_PLACEMENT_FAILED)return Block(NFR_BAD_REQUEST,r);
            if(report.placement.geometryResult==NSP_UNRESOLVED_DOOR)return Block(NFR_NATIVE_UNRESOLVED,r);
            if(report.placement.selectionResult!=NB_OK)return Block(NFR_MISSING_DATA,r);
            return Reject(r);
        }
        phase=NRF_TYPE8;return result=NFR_OK;
    }
    if(phase==NRF_TYPE8) {
        NativePostBossContext c;memset(&c,0,sizeof(c));c.placement=placement;NativePostBossReport report;
        NativePostBossResult r=NativePlacePostBossType8(topology,pool,floor.plan.streams.layout,layout,c,&report);
        if(report.placed)lastDefinition=report.entries[report.placed-1].definition;
        if(r==NPB_FIRST_GEOMETRY_FAILED)return Reject(r);
        if(r!=NPB_OK)return Block(r==NPB_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        retainedRoomSeed=report.nextSelectionSeed;phase=NRF_TYPE2_SHOP;return result=NFR_OK;
    }
    if(phase==NRF_TYPE2_SHOP) {
        // Historical helper name: NativePlaceTreasure selects TYPE2 shops.
        NativeTreasureContext c;memset(&c,0,sizeof(c));c.placement=placement;c.stage=context.stage;c.stageType=context.stageType;
        c.profileDb=Byte(profile,0xdb);c.profileDc=Byte(profile,0xdc);c.profileDd=Byte(profile,0xdd);c.profileDe=Byte(profile,0xde);
        NativeTreasureReport report;NativeTreasureResult r=NativePlaceTreasure(topology,pool,floor.plan.streams.layout,layout,c,&report);lastDefinition=report.definition;
        if(r==NTR_GEOMETRY_FAILED)return Reject(r);
        if(r!=NTR_OK)return Block(r==NTR_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        phase=NRF_TYPE4_TREASURE;return result=NFR_OK;
    }
    if(phase==NRF_TYPE4_TREASURE) {
        // Historical helper name: NativePlaceShops selects TYPE4 treasure rooms.
        NativeShopContext c;memset(&c,0,sizeof(c));c.placement=placement;c.stage=context.stage;c.stageType=context.stageType;c.curses=context.effectiveCurses;c.trinket82Multiplier=1;
        NativeShopReport report;NativeShopResult r=NativePlaceShops(topology,pool,floor.plan.streams.layout,layout,c,&report);
        if(report.placed)lastDefinition=report.entries[report.placed-1].definition;
        if(r==NSH_GEOMETRY_FAILED)return Reject(r);
        if(r!=NSH_OK)return Block(r==NSH_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        phase=NRF_OPTIONAL;return result=NFR_OK;
    }
    if(phase==NRF_OPTIONAL) {
        NativeOptionalContext c;memset(&c,0,sizeof(c));c.placement=placement;c.stage=context.stage;
        c.hasFirstPlayer=true;c.firstPlayer12a4=firstPlayerKeys;c.predicate908b40=fullHealthPredicate;
        NativeOptionalReport report;NativeOptionalResult r=NativePlaceOptionalRoom(topology,pool,floor.plan.streams.layout,layout,c,&report);lastDefinition=report.definition;
        if(r!=NOC_OK)return Block(r==NOC_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        phase=NRF_PROFILE_ROOM;return result=NFR_OK;
    }
    if(phase==NRF_PROFILE_ROOM) {
        NativeProfileRoom12Context c;memset(&c,0,sizeof(c));c.placement=placement;c.stage=context.stage;
        c.profileDb=Byte(profile,0xdb);c.profileDc=Byte(profile,0xdc);c.profileDd=Byte(profile,0xdd);c.profileDe=Byte(profile,0xde);
        NativeProfileRoom12Report report;NativeProfileRoom12Result r=NativePlaceProfileRoom12(topology,pool,floor.plan.streams.layout,layout,c,&report);lastDefinition=report.definition;
        if(r!=NP12_OK)return Block(r==NP12_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        phase=NRF_TYPE10;return result=NFR_OK;
    }
    if(phase==NRF_TYPE10) {
        NativeType10Context c;memset(&c,0,sizeof(c));c.placement=placement;c.stage=context.stage;
        NativeType10Report report;NativeOptionalResult r=NativePlaceType10Rooms(topology,pool,floor.plan.streams.layout,layout,c,&report);
        if(report.placed)lastDefinition=report.entries[report.requested-1].definition;
        if(r!=NOC_OK)return Block(r==NOC_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        phase=NRF_MINIBOSS;return result=NFR_OK;
    }
    if(phase==NRF_MINIBOSS||phase==NRF_CHALLENGE_ROOM||phase==NRF_ARCADE_TYPE20||phase==NRF_PLANETARIUM||phase==NRF_BEDROOM) {
        NormalRunPhase current=phase;NativeMidSpecialContext c=MidContext(*this);NativeMidSpecialResult r;
        if(current==NRF_MINIBOSS)r=NativePlaceMiniboss(topology,pool,floor.plan.streams.layout,layout,c,&midReport);
        else if(current==NRF_CHALLENGE_ROOM)r=NativePlaceChallengeRoom(topology,pool,floor.plan.streams.layout,layout,c,&midReport);
        else if(current==NRF_ARCADE_TYPE20)r=NativePlaceArcadeOrType20(topology,pool,floor.plan.streams.layout,layout,c,&midReport);
        else if(current==NRF_PLANETARIUM)r=NativePlacePlanetarium(topology,pool,floor.plan.streams.layout,layout,c,&midReport);
        else r=NativePlaceBedroom(topology,pool,floor.plan.streams.layout,layout,c,&midReport);
        lastDefinition=midReport.definition;
        if(r!=NMS_OK)return Block(r==NMS_UNRESOLVED_GEOMETRY?NFR_NATIVE_UNRESOLVED:NFR_MISSING_DATA,r);
        runFlagsLow|=midReport.flagsLowOr;runFlagsHigh|=midReport.flagsHighOr;
        if(current==NRF_MINIBOSS&&midReport.action==NOA_PLACED)minibossPlaced=true;
        phase=current==NRF_MINIBOSS?NRF_CHALLENGE_ROOM:current==NRF_CHALLENGE_ROOM?NRF_ARCADE_TYPE20:current==NRF_ARCADE_TYPE20?NRF_PLANETARIUM:current==NRF_PLANETARIUM?NRF_BEDROOM:NRF_SECRET;
        //006f23c8..006f25b5: first chapter types0..2 cannot satisfy
        //00703560/7035c0/703620. No override room or replacement budget.
        return result=NFR_OK;
    }
    if(phase==NRF_SECRET||phase==NRF_TYPE29) {
        NormalRunPhase current=phase;NativeSecretContext c=SecretContext(*this);
        NativeSecretResult r=current==NRF_SECRET?NativePlaceSecretRooms(topology,pool,layout,retainedRoomSeed,c,&secretReport):NativePlaceType29(topology,pool,layout,retainedRoomSeed,c,&secretReport);
        if(secretReport.placed)lastDefinition=secretReport.entries[secretReport.requested-1].definition;
        if(r!=NSR_OK)return Block(NFR_MISSING_DATA,r);
        //006f273c stage11 override is inapplicable to this chapter.
        phase=current==NRF_SECRET?NRF_TYPE29:NRF_ORDINARY;return result=NFR_OK;
    }
    if(phase==NRF_ORDINARY) {
        NativeOrdinaryContext c;memset(&c,0,sizeof(c));c.stage=context.stage;c.stageType=context.stageType;c.configStage=floor.plan.configStage;
        c.startGridIndex=NativeCellIndex(topology.rooms[0].origin);c.minDifficulty=floor.plan.minDifficulty;c.maxDifficulty=floor.plan.maxDifficulty;
        c.effectiveCurses=context.effectiveCurses;c.placement=placement;
        NativeOrdinaryResult r=NativePlaceOrdinary(topology,pool,floor.plan.streams.layout,layout,c,&ordinaryReport);
        if(r!=NOP_OK)return Block(NFR_MISSING_DATA,r);
        if(ordinaryReport.placed)lastDefinition=ordinaryReport.entries[ordinaryReport.placed-1].definition;
        phase=NRF_OFFGRID;return result=NFR_OK;
    }
    if(phase==NRF_OFFGRID) {
        NativeOffgridContext c={0};c.stage=context.stage;c.stageType=context.stageType;c.nativeMode=context.nativeMode;c.configStage=floor.plan.configStage;
        c.profile152=profile.Award(152);c.profile153=profile.Award(153);c.profile154=profile.Award(154);
        NativeOffgridResult r=NativePrepareOffgridRooms(pool,floor.plan.streams.layout,layout,c,&offgridReport);
        if(r!=NOG_OK)return Block(NFR_MISSING_DATA,r);
        //006f6ec1..006f6f7e: alternate chapter side effects do not apply to
        //first-stage types0..2.006f6f84 now commits the accepted boss identities.
        if(!NativeAcceptFloorWithBosses(floor,bosses,&topology))return Block(NFR_BAD_REQUEST,-2);
        phase=NRF_LEVEL_FINISH;return result=NFR_OK;
    }
    if(phase==NRF_LEVEL_FINISH) {
        //006fa669/006f3ad0 updates grid descriptors and all18 negative slots.
        if(!layout.FinalizeDoors())return Block(NFR_BAD_REQUEST,-3);
        floor.plan.streams.layout.Next(&levelSeed); //006fa670..006fa6c4.
        //Stage12 postprocessing and high flag10000 do not apply to the
        //supported first-stage context.006faa5f selects the original start.
        currentGrid=previousGrid=NativeCellIndex(topology.rooms[0].origin);
        phase=NRF_NEED_ROOM_ENTRY;return result=NFR_PENDING;
    }
    return Block(NFR_BAD_REQUEST,-1);
}
