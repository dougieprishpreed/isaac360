"""Record the focused host execution's actual scope, without accepting gameplay."""
from pathlib import Path
import argparse,hashlib,json,re,datetime
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--seed',type=int,required=True);p.add_argument('--affinity',required=True);p.add_argument('--restart',action='store_true');p.add_argument('--repaired-report',action='store_true');a=p.parse_args()
    raw=(ROOT/'build/gameplay/smoke.log').read_bytes();log=raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
    session=re.search(r'^SESSION .*next=([0-9a-f]+) players=(\d+) ticks=(\d+)',log,re.M)
    if not session:raise ValueError('Missing actual shared-session result.')
    gameplay=re.search(r'^GAMEPLAY complete=(\d+) playerTicks=(\d+) wholeTicks=(\d+) collisionStop=\(([^,]+),([^)]+)\) stable=(\d+)',log,re.M)
    ticks=int(gameplay.group(2)) if gameplay else int(session[3])
    whole=int(gameplay.group(3)) if gameplay else 0
    complete=int(gameplay.group(1)) if gameplay else int(session[2])
    tail=re.search(r'^TAIL ticks=(\d+) trailSegments=(\d+) renderCallbacks=(\d+) fields=12c0/2cc/12bc/1ca4 present=(\d)(\d)(\d)(\d)',log,re.M)
    head=re.search(r'^HEAD_STATE timers=(-?\d+)/(-?\d+) calls=(\d+)/(\d+) direction=(\d+)',log,re.M)
    projectiles=re.search(r'^PROJECTILES ticks=(\d+) spawned=(\d+) wallHits=(\d+) expired=(\d+) active=(\d+) cooldown=([^\r\n]+)',log,re.M)
    sim=re.search(r'^SIM_STATE clear=(\d+) enemyKills=(\d+) doorsOpened=(\d+) enemies=(\d+)',log,re.M)
    encounter=re.search(r'^ENCOUNTER seed=(\d+) room=(\d+) enemies=(\d+) kills=(\d+) remaining=(\d+) clear=(\d+) doors=(\d+) open=(\d+) doorsOpened=(\d+) projectiles=(\d+)',log,re.M)
    transition=re.search(r'^TRANSITION seed=(\d+) from=(-?\d+) slot=(\d+) to=(-?\d+) definition=(-?\d+) room=(\d+) enemies=(\d+) transitions=(\d+)',log,re.M)
    death=re.search(r'^DEATH seed=(\d+) room=(\d+) hits=(\d+) hp=(-?\d+)/(-?\d+) soul=(-?\d+) dead=(\d+) ticks=(\d+) enemies=(\d+)',log,re.M)
    descend=re.search(r'^DESCEND from=(\d+) to=(\d+) room=(-?\d+) definition=(-?\d+) hp=(-?\d+)/(-?\d+) soul=(-?\d+)',log,re.M)
    floorPath=re.search(r'^FLOOR_PATH seed=(\d+) rooms=(\d+) boss=(-?\d+) route=([0-9,]+)',log,re.M)
    floorRooms=list(re.finditer(r'^FLOOR_ROOM step=(\d+) grid=(-?\d+) def=(-?\d+) type=(-?\d+) variant=(\d+) enemies=(\d+) kills=(\d+) remaining=(\d+) clear=(\d+) hp=(-?\d+)/(-?\d+) ticks=(\d+)',log,re.M))
    floorBoss=re.search(r'^FLOOR_BOSS identity=(-?\d+) award=(-?\d+) room=(-?\d+) variant=(\d+) entryEnemies=(\d+) kills=(\d+)',log,re.M)
    floorTrap=re.search(r'^FLOOR_TRAPDOOR state=(\d+) descend=(\d+) stage=(\d+) grid=(-?\d+) hp=(-?\d+)/(-?\d+) soul=(-?\d+) kills=(\d+)',log,re.M)
    floorContinue=re.search(r'^FLOOR_CONTINUE stage=(\d+) grid=(-?\d+) hp=(-?\d+)/(-?\d+) soul=(-?\d+) bombs=(-?\d+) keys=(-?\d+) coins=(-?\d+)',log,re.M)
    minimapInitial=re.search(r'^MINIMAP_INITIAL grid=(-?\d+) small=(\d+)/(\d+) marker=(-?[0-9.]+),(-?[0-9.]+) large=(\d+)/(\d+) marker=(-?[0-9.]+),(-?[0-9.]+) discovered=(\d+) refreshed=(\d+)',log,re.M)
    minimapRooms=[]
    for m in re.finditer(r'^MINIMAP_ROOM step=(\d+) grid=(-?\d+) small=(\d+) marker=(-?[0-9.]+),(-?[0-9.]+) large=(\d+) marker=(-?[0-9.]+),(-?[0-9.]+) discovered=(\d+) visited=(\d+) clear=(\d+) display=(\d+) activations=(\d+) refreshed=(\d+)',log,re.M):
        minimapRooms.append({'step':int(m.group(1)),'grid':int(m.group(2)),'small_draws':int(m.group(3)),
            'small_marker':[float(m.group(4)),float(m.group(5))],'large_draws':int(m.group(6)),
            'large_marker':[float(m.group(7)),float(m.group(8))],'discovered':int(m.group(9)),
            'visited':int(m.group(10)),'clear':bool(int(m.group(11))),'display':int(m.group(12)),
            'activations':int(m.group(13)),'refreshes':int(m.group(14))})
    minimapTransitions=[]
    for m in re.finditer(r'^MINIMAP_TRANSITION step=(\d+) fromGrid=(-?\d+) toGrid=(-?\d+) refreshed=(\d+) discovered=(\d+) small=(-?[0-9.]+),(-?[0-9.]+) large=(-?[0-9.]+),(-?[0-9.]+)',log,re.M):
        minimapTransitions.append({'step':int(m.group(1)),'from_grid':int(m.group(2)),'to_grid':int(m.group(3)),
            'refreshes':int(m.group(4)),'discovered':int(m.group(5)),'small_marker':[float(m.group(6)),float(m.group(7))],
            'large_marker':[float(m.group(8)),float(m.group(9))]})
    minimapReentries={}
    for m in re.finditer(r'^MINIMAP_REENTRY direction=(back|forward) grid=(-?\d+)(?: visited=(\d+) clear=(\d+) display=(\d+) activations=(\d+)| roomRetained=(\d+) doorsRetained=(\d+) perimeterRetained=(\d+) activations=(\d+)) rngOneDraw=(\d+) small=(-?[0-9.]+),(-?[0-9.]+) large=(-?[0-9.]+),(-?[0-9.]+) refreshed=(\d+)',log,re.M):
        minimapReentries[m.group(1)]={'grid':int(m.group(2)),'activations':int(m.group(6) or m.group(10)),
            'rng_one_draw':bool(int(m.group(11))),'small_marker':[float(m.group(12)),float(m.group(13))],
            'large_marker':[float(m.group(14)),float(m.group(15))],'refreshes':int(m.group(16))}
    minimapFloor=re.search(r'^MINIMAP_FLOOR stage=(\d+) grid=(-?\d+) refreshed=(\d+) discovered=(\d+) small=(-?[0-9.]+),(-?[0-9.]+) large=(-?[0-9.]+),(-?[0-9.]+)',log,re.M)
    generatedPickupPath=re.search(r'^GENERATED_PICKUP_PATH seed=(\d+) rooms=(\d+) route=([0-9,]+)',log,re.M)
    generatedPickupRoom=re.search(r'^GENERATED_PICKUP_ROOM seeds=(\d+)/(\d+)/(\d+)/(\d+) key=(\d+) bomb=(\d+)',log,re.M)
    generatedPickupBombs=list(re.finditer(r'^GENERATED_PICKUP_BOMB (first|second) removed=(\d+) inside=(\d+) retainedOutOfRadius=(\d+)(?: bombPickup=(\d+) bombs=(\d+))? hp=(-?\d+)/(-?\d+) hits=(\d+)',log,re.M))
    generatedPickupBombRecords=[{'phase':m.group(1),'removed':int(m.group(2)),'inside':int(m.group(3)),
        'retained_out_of_radius':int(m.group(4)),'bomb_pickup':bool(int(m.group(5))) if m.group(5) else None,
        'bombs_after':int(m.group(6)) if m.group(6) else None,'hp':int(m.group(7)),'max_hp':int(m.group(8)),
        'player_hits':int(m.group(9))} for m in generatedPickupBombs]
    generatedPickupBlasts=[]
    for m in re.finditer(r'^GENERATED_PICKUP_BLAST (first|second) pos=\((-?[0-9.]+),(-?[0-9.]+)\) radius=(-?[0-9.]+) inside=(\d+) outside=(\d+)(?: playerCell=(-?\d+))?(?: cells=([0-9,]*))?',log,re.M):
        generatedPickupBlasts.append({'phase':m.group(1),'position':[float(m.group(2)),float(m.group(3))],
            'radius':float(m.group(4)),'inside':int(m.group(5)),'outside':int(m.group(6)),
            'player_cell':int(m.group(7)) if m.group(7) else None,
            'cells':[int(v) for v in m.group(8).split(',')] if m.group(8) else []})
    generatedPickupKey=re.search(r'^GENERATED_PICKUP_KEY cell=(\d+) keys=(-?\d+) bombs=(-?\d+) ticks=(\d+) globalMTUnchanged=(\d+) hp=(-?\d+)/(-?\d+) hits=(\d+)',log,re.M)
    generatedPickupReentry=re.search(r'^GENERATED_PICKUP_REENTRY consumed=(\d+)/(\d+) active=(\d+) activations=(\d+) noRecollect=(\d+)',log,re.M)
    generatedPickupNative=re.search(r'^GENERATED_PICKUP_NATIVE saved=(\d+) captured=(\d+) restored=(\d+) resources=(\d+) consumedAbsent=(\d+) active=(\d+) duplicates=(\d+) contactRadius=(\d+) noRestoredContact=(\d+) capture=([0-9a-f]+) gap=(\S+)',log,re.M)
    generatedPickupControl=re.search(r'^GENERATED_PICKUP_CONTROL saved=(\d+) captured=(\d+) restored=(\d+) records=(\d+) present=(\d+) capture=([0-9a-f]+) entities=(\d+) savedPickups=(\d+) currentGrid=(-?\d+)',log,re.M)
    generatedPickupControlD8=dict((int(m.group(1)),m.group(2)) for m in re.finditer(r'^GENERATED_PICKUP_CONTROL_RECORD_D8 index=(\d+) bits=([0-9a-f]+)',log,re.M))
    generatedPickupControlRecords=[]
    for m in re.finditer(r'^GENERATED_PICKUP_CONTROL_RECORD index=(\d+) type=(\d+) variant=(\d+) subtype=(\d+) grid=(\d+) current=(\d+) initial=(\d+) fields=([0-9/]+) pos=\((-?[0-9.]+),(-?[0-9.]+)\)',log,re.M):
        generatedPickupControlRecords.append({'index':int(m.group(1)),'type':int(m.group(2)),'variant':int(m.group(3)),
            'subtype':int(m.group(4)),'grid':int(m.group(5)),'current_seed':int(m.group(6)),
            'initial_seed':int(m.group(7)),'fields':[int(v) for v in m.group(8).split('/')],
            'position':[float(m.group(9)),float(m.group(10))],
            'native_d8_bits':generatedPickupControlD8.get(int(m.group(1)))})
    generatedPickupRestoredContact=re.search(r'^GENERATED_PICKUP_RESTORED_CONTACT cell=(\d+) bombRadius=(-?[0-9.]+) bombWithin=(\d+) keyCell=(-?\d+) roomOk=(\d+)',log,re.M)
    codecFixture=re.search(r'^CODEC_FIXTURE pickup470=([0-9a-f]+) pickup4e0=([0-9a-f]+) d8=([0-9a-f]+) fullword=(\d+) invalidCrcRejected=(\d+) unsupportedExtraRejected=(\d+)',log,re.M)
    gridCodecFixture=re.search(r'^GRID_CODEC_FIXTURE type4=(\d+) type6=(\d+) type15=(\d+) unsupported=(\d+) postInitCounts=(\d+)/(\d+)/(\d+) frame=(\d+)/(\d+)/(\d+) tails=([0-9a-f]+)/([0-9a-f]+)/([0-9a-f]+)',log,re.M)
    ambientSummaries=dict((m.group(1),{'variant33':int(m.group(2)),'variant68':int(m.group(3)),
        'init_draws':int(m.group(4)),'post_draws':int(m.group(5))}) for m in
        re.finditer(r'^GENERATED_PICKUP_AMBIENT scope=(\S+) variant33=(\d+) variant68=(\d+) initDraws=(\d+) postDraws=(\d+)',log,re.M))
    ambientRecords=[]
    for m in re.finditer(r'^GENERATED_PICKUP_AMBIENT_RECORD scope=(\S+) index=(\d+) variant=(\d+) wire20=([0-9a-f]+) native27c=(-?\d+) frameCount=(-?\d+) initDraws=(\d+) postDraws=(\d+)',log,re.M):
        ambientRecords.append({'scope':m.group(1),'index':int(m.group(2)),'variant':int(m.group(3)),
            'wire20':m.group(4),'native27c':int(m.group(5)),'frame_count':int(m.group(6)),
            'init_draws':int(m.group(7)),'post_draws':int(m.group(8))})
    generatedPickupComplete=re.search(r'^GENERATED_PICKUP_COMPLETE bomb=(\d+) key=(\d+) reentry=(\d+) native=(\d+)',log,re.M)
    generatedPickupRestoredState=re.search(r'^GENERATED_PICKUP_RESTORED_STATE grids=(\d+) records=(\d+) actors=(\d+) space=(\d+) frame=(\d+) path=(\d+)',log,re.M)
    generatedPickupNoncurrent=re.search(r'^GENERATED_PICKUP_NONCURRENT restored=(\d+) grids=(\d+) actors=(\d+) space=(\d+) path=(\d+) pickups=(\d+) untouchedControl=(\d+) untouchedEntry=(\d+) resources=(\d+) keyCell=(\d+) bombCell=(\d+) frame=(\d+)',log,re.M)
    pendingRestart=re.search(r'^PENDING_RESTART failedStop=([0-9a-f]+) failedCleared=(\d+) failedVirgin=(\d+) successVirgin=(\d+)',log,re.M)
    item=re.search(r'^ITEM pool=(\d+) id=(\d+) collected=(\d+) owned=(\d+) poolCounter=(\d+)(?: effect_hp=(\d+)/(\d+) effect_keys=(\d+))?',log,re.M)
    bomb=re.search(r'^BOMB placed=(\d+) explosions=(\d+) enemyHits=(\d+) bombsLeft=(-?\d+) enemies=(\d+)',log,re.M)
    chars=re.search(r'^CHARACTERS ok=(\d+) fail=(\d+)',log,re.M)
    hard=re.search(r'^HARD mode=(\d+) phase=(\d+) player=(\d+) hp=(-?\d+)/(-?\d+) slots=(-?\d+)(?: savedMode=(\d+))?',log,re.M)
    stat=re.search(r'^STAT fd=(-?[0-9.]+) damage=(-?[0-9.]+) speed=(-?[0-9.]+) luck=(-?[0-9.]+)',log,re.M)
    statOrder=re.search(r'^STAT order=(\d+) resource_hp=(-?\d+)/(-?\d+) cache_damage=(-?[0-9.]+) cache_speed=(-?[0-9.]+)',log,re.M)
    conditional=re.search(r'^CONDITIONAL crown=(\d+) disabled=(\d+) lust=(\d+) gust=(\d+) dprince=(\d+) blood_oath=(\d+) hp=(-?\d+)/(-?\d+) oath_damage=(-?[0-9.]+) oath_speed=(-?[0-9.]+)',log,re.M)
    activeBatch=re.search(r'^ACTIVE_BATCH belial=(\d+) tammy=(\d+) shadows=(\d+) mrboom=(\d+)(?: d8=(\d+))?',log,re.M)
    reroll=re.search(r'^REROLL purity=(\d+) treatment=(\d+) d8=(\d+) missing=(\d+) damage=(-?[0-9.]+) range=(-?[0-9.]+)',log,re.M)
    bossAward=re.search(r'^BOSS_AWARD identity=(\d+) achievement=(\d+) persisted=(\d+)',log,re.M)
    trigger=re.search(r'^NATIVE_CURRENT_TRIGGER perm_damage=(-?[0-9.]+) perm_range=(-?[0-9.]+) purity=(\d+) active=(\d+) stacks=(\d+) crown_disabled=(\d+)',log,re.M)
    bossAttacks=re.search(r'^BOSS_ATTACKS (.+)$',log,re.M)
    bossFlags=bossAttacks.group(1) if bossAttacks else ''
    audio=re.search(r'^AUDIO device=(\d+) sfx=(\d+) loaded=(\d+) music=(\d+) music_frames=(\d+) sfx_frames=(\d+) rms=(-?[0-9.]+) gameplay_sfx=(\d+)',log,re.M)
    musicSet=re.search(r'^MUSICSET intro=(\d+) layer=(\d+) layer_frames=(\d+)',log,re.M)
    blackheart=re.search(r'^BLACKHEART absorbed=(\d+) soul=(\d+) red=(\d+) mask=(\d+)',log,re.M)
    walkanim=re.search(r'^WALKANIM clip=([A-Za-z]+) frame=(-?[0-9.]+) stopped=(\d+)',log,re.M)
    doorvis=re.search(r'^DOORVIS opened=(\d+) locked=(\d+)',log,re.M)
    pocket=re.search(r'^POCKET hp=(-?\d+)/(-?\d+) bombs=(-?\d+) keys=(-?\d+) coins=(-?\d+)',log,re.M)
    active=re.search(r'^ACTIVE id=(\d+) hp=(-?\d+)/(-?\d+)',log,re.M)
    familiar=re.search(r'^FAMILIAR spawned=(\d+) shots=(\d+) hits=(\d+) enemies=(\d+)',log,re.M)
    cont=re.search(r'^CONTINUE stage=(\d+) hp=(-?\d+)/(-?\d+) keys=(-?\d+) card=(-?\d+) pill=(-?\d+) items=(\d+)',log,re.M)
    nativeCont=re.search(r'^NATIVE_CONTINUE seed=(\d+) stage=(\d+) hp=(-?\d+)/(-?\d+)',log,re.M)
    nativeCurrent=re.search(r'^NATIVE_CURRENT stage=(\d+) hp=(-?\d+)/(-?\d+) soul=(-?\d+) keys=(-?\d+) bombs=(-?\d+) coins=(-?\d+) active=(-?\d+) card=(-?\d+) pill=(-?\d+) poolCounter=(\d+)(?: pos=\((-?[0-9.]+),(-?[0-9.]+)\) fd=(-?[0-9.]+) dmg=(-?[0-9.]+) spd=(-?[0-9.]+) luck=(-?[0-9.]+)(?: effects=(-?\d+))?)?',log,re.M)
    nativeEffect=re.search(r'^NATIVE_CURRENT_EFFECT count=(\d+) timer=(-?\d+)',log,re.M)
    selectedIntent=re.search(r'^SELECTED_RUN_INTENT newWithSave=(\d+) seed=(\d+) player=(-?\d+) continueSlot=(-?\d+) mode=(\d+) seed=(\d+) stage=(\d+) staleActions=(\d+) preloadValue=(\d+) preloadPressed=(\d+) preloadDrop=(\d+) preloadHeld=(\d+) preloadConnected=(\d+) preloadOwner=(\d+)/(\d+) restoredOwner=(\d+)/(\d+) coldTrack=(\d+) native364=(-?\d+) stingerQueued=(\d+) stinger364=(-?\d+) coldSlot=(\d+) savedMode=(\d+) retained=1 second=1 invalidNoFallback=(\d+) consumedTwice=(\d+)/(\d+)',log,re.M)
    greed=re.search(r'^GREED start wave=(\d+)/(\d+) room=(\d+) type=(\d+) variant=(\d+) enemies=(\d+)',log,re.M)
    greedDone=re.search(r'^GREED complete waves=(\d+) next=(\d+) room=(\d+)',log,re.M)
    greedBoss=re.search(r'^GREED boss room=(\d+) type=(\d+) variant=(\d+) enemies=(\d+) ultraGreed=(\d+)',log,re.M)
    greedBossShots=re.search(r'^GREED boss attack shots=(\d+)',log,re.M)
    greedContinue=re.search(r'^GREED continue wave=(\d+) room=(\d+) count=(\d+)',log,re.M)
    greedNative=re.search(r'^GREED native continue wave=(\d+) room=(\d+) count=(\d+)(?: mode=(\d+))?',log,re.M)
    deathflow=re.search(r'^DEATH_FLOW dyingTicks=(\d+) gameOver=(\d+) restartResult=(\d+) restartPhase=(\d+) restartHP=(-?\d+)/(-?\d+)',log,re.M)
    lock=re.search(r'^LOCK treasureSlot=(\d+) noKeyClosed=(\d+) keyOpened=(\d+) keysLeft=(-?\d+) doorsUnlocked=(\d+) bossOpenWithoutDefeat=(\d+) bossDefeated=(\d+)',log,re.M)
    pickupNativePassed=bool(generatedPickupNative and
        all(int(generatedPickupNative.group(i)) for i in (1,2,3,4,5,8,9)) and
        int(generatedPickupNative.group(6))==0 and int(generatedPickupNative.group(7))==0 and
        generatedPickupNative.group(10)=='00000000' and generatedPickupNative.group(11)=='none')
    pickupNoncurrentPassed=bool(generatedPickupNoncurrent and
        all(int(generatedPickupNoncurrent.group(i)) for i in (1,3,4,5,7,8,9)) and
        int(generatedPickupNoncurrent.group(2))>0 and int(generatedPickupNoncurrent.group(6))==0 and
        int(generatedPickupNoncurrent.group(10))==42 and int(generatedPickupNoncurrent.group(11))==92)
    gridCodecPassed=bool(gridCodecFixture and all(int(gridCodecFixture.group(i)) for i in range(1,5)) and
        [int(gridCodecFixture.group(i)) for i in range(5,8)]==[6,3,2] and
        [int(gridCodecFixture.group(i)) for i in range(8,11)]==[17,18,19] and
        [gridCodecFixture.group(i) for i in (11,12,13)]==['16a9de81','7a11ee00','16a9de81'])
    startAmbient=[r for r in ambientRecords if r['scope']=='noncurrent-start']
    start33=[r for r in startAmbient if r['variant']==33]
    start68=[r for r in startAmbient if r['variant']==68]
    ambientLifecyclePassed=bool(start33 and start68 and all(
        r['wire20']=='ffff' and r['native27c']==-1 and r['frame_count']==0 and
        r['init_draws']==0 and r['post_draws']==2 for r in start33) and all(
        r['wire20']=='ffff' and r['native27c']==-1 and r['frame_count']==0 and
        r['init_draws']==2 and 3<=r['post_draws']<=4 for r in start68) and
        ambientSummaries.get('noncurrent-start',{}).get('variant33',0)>0 and
        ambientSummaries.get('noncurrent-start',{}).get('variant68',0)>0)
    pickupPassed=bool(generatedPickupComplete and generatedPickupPath and pickupNativePassed and
        all(int(generatedPickupComplete.group(i)) for i in range(1,5)) and pickupNoncurrentPassed and
        gridCodecPassed and ambientLifecyclePassed)
    pendingRestartPassed=bool(pendingRestart and pendingRestart.group(1)=='0079daf0' and
        all(int(pendingRestart.group(i)) for i in (2,3,4)))
    selectedIntentPassed=bool(selectedIntent and
        int(selectedIntent.group(1))==1 and int(selectedIntent.group(2))==424242 and
        int(selectedIntent.group(3))>=0 and int(selectedIntent.group(4))==1 and
        int(selectedIntent.group(5))==0 and int(selectedIntent.group(6))==1 and
        int(selectedIntent.group(7))==3 and int(selectedIntent.group(8))==0 and
        int(selectedIntent.group(9))==0 and int(selectedIntent.group(10))==0 and
        int(selectedIntent.group(11))==0 and int(selectedIntent.group(12))==0 and
        int(selectedIntent.group(13))==0 and
        int(selectedIntent.group(14))==int(selectedIntent.group(16))==0 and
        int(selectedIntent.group(15))==int(selectedIntent.group(17))==0 and
        int(selectedIntent.group(18)) not in (0,64,95) and int(selectedIntent.group(19))==-100 and
        int(selectedIntent.group(20))>0 and int(selectedIntent.group(21))==0 and
        int(selectedIntent.group(22))==1 and int(selectedIntent.group(23))==0 and
        int(selectedIntent.group(24))==1 and int(selectedIntent.group(25))==0 and
        int(selectedIntent.group(26))==0 and
        bool(hard and hard.group(7)=='1') and bool(greedNative and greedNative.group(4)=='2'))
    focusedComplete=bool(session and gameplay and selectedIntentPassed and
        (not a.restart or pendingRestartPassed))
    if a.restart and not pendingRestartPassed:
        raise ValueError('Missing or failed pending-restore restart evidence.')
    paths=set()
    for folder in ('src/game','src/content','build/generated'):
        paths.update(f for f in (ROOT/folder).rglob('*') if f.suffix in ('.cpp','.h','.inc'))
    paths.update(ROOT/f for f in ('src/simulation/gameplay_smoke.cpp','src/simulation/generated_pickup_probe.inc','tools/build_gameplay_smoke.ps1','tools/build_gameplay_smoke.cmd',
        'tools/prepare_player_runtime.py','tools/prepare_native_persistence.py','tools/prepare_room_runtime.py','tools/prepare_greed_catalog.py','tools/report_gameplay_smoke.py',
        'build/gameplay/gameplay_smoke.exe','build/gameplay/build.log','build/gameplay/smoke.log'))
    reports=('source_data_check.json','original_configuration.json','normal_player_creation_reference.json','native_player_runtime_reference.json',
        'native_item_pool_reference.json','native_nightmare_reference.json','native_persistence_reference.json','greed_catalog.json')
    paths.update(ROOT/'runtime/port'/f for f in reports)
    packet={'format':'focused-gameplay-host-v2','status':'passed' if focusedComplete else 'incomplete',
        'complete':focusedComplete,'completion_scope':'focused Windows host checkpoint only',
        'recorded_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'command':f'powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_gameplay_smoke.ps1 -Seed {a.seed}'+(' -Restart' if a.restart else ''),
        'execution':{'platform':'Windows x86 host','toolchain':'Visual Studio 2010','worker_count':1,'inherited_processor_affinity':a.affinity,'paced':False,'rendered':False,
            'driver_exit_code':1 if a.repaired_report else 0,'build_exit_code':0,'host_exit_code':0,'report_repaired_after_run':a.repaired_report},
        'observed':{'next_native':session[1],'complete_players':complete,'player_update_collision_ticks':ticks,
            'whole_gameplay_ticks':whole,'head_timers':[int(head.group(1)),int(head.group(2))] if head else None,
            'head_calls':int(head.group(3)) if head else None,'head_direction':int(head.group(5)) if head else None,
            'projectile_ticks':int(projectiles.group(1)) if projectiles else 0,
            'projectiles_spawned':int(projectiles.group(2)) if projectiles else 0,
            'projectile_wall_hits':int(projectiles.group(3)) if projectiles else 0,
            'projectile_expired':int(projectiles.group(4)) if projectiles else 0,
            'projectiles_active':int(projectiles.group(5)) if projectiles else 0,
            'room_clear':bool(int(sim.group(1))) if sim else False,
            'doors_opened':int(sim.group(3)) if sim else 0,
            'encounter_enemies':int(encounter.group(3)) if encounter else 0,
            'encounter_kills':int(encounter.group(4)) if encounter else 0,
            'encounter_remaining':int(encounter.group(5)) if encounter else -1,
            'encounter_clear':bool(int(encounter.group(6))) if encounter else False,
            'transition_from':int(transition.group(2)) if transition else -1,
            'transition_to':int(transition.group(4)) if transition else -1,
            'transition_definition':int(transition.group(5)) if transition else -1,
            'transition_enemies':int(transition.group(7)) if transition else 0,
            'transition_count':int(transition.group(8)) if transition else 0,
            'player_hits':int(death.group(3)) if death else 0,
            'player_dead':bool(int(death.group(7))) if death else False,
            'death_ticks':int(death.group(8)) if death else 0,
            'death_enemies_remaining':int(death.group(9)) if death else -1,
            'lock_no_key_closed':bool(int(lock.group(2))) if lock else False,
            'lock_key_opened':bool(int(lock.group(3))) if lock else False,
            'lock_keys_left':int(lock.group(4)) if lock else -1,
            'boss_door_open_without_defeat':bool(int(lock.group(6))) if lock else False,
            'boss_defeated_flag':bool(int(lock.group(7))) if lock else False,
            'descend_from':int(descend.group(1)) if descend else 0,
            'descend_to':int(descend.group(2)) if descend else 0,
            'descend_room':int(descend.group(3)) if descend else -1,
            'descend_definition':int(descend.group(4)) if descend else -1,
            'floor_rooms':int(floorPath.group(2)) if floorPath else 0,
            'floor_boss_list':int(floorPath.group(3)) if floorPath else -1,
            'floor_route':[int(v) for v in floorPath.group(4).split(',')] if floorPath and floorPath.group(4) else [],
            'floor_room_grids':[int(r.group(2)) for r in floorRooms],
            'floor_room_enemies':[int(r.group(6)) for r in floorRooms],
            'floor_room_kills':[int(r.group(7)) for r in floorRooms],
            'floor_rooms_cleared':sum(1 for r in floorRooms if int(r.group(9)) and int(r.group(8))==0),
            'floor_boss_identity':int(floorBoss.group(1)) if floorBoss else -1,
            'floor_boss_kills':int(floorBoss.group(6)) if floorBoss else 0,
            'floor_trapdoor_state':int(floorTrap.group(1)) if floorTrap else 0,
            'floor_descend_stage':int(floorTrap.group(3)) if floorTrap else 0,
            'floor_descend_grid':int(floorTrap.group(4)) if floorTrap else -1,
            'floor_descend_kills':int(floorTrap.group(8)) if floorTrap else 0,
            'floor_continue_stage':int(floorContinue.group(1)) if floorContinue else 0,
            'floor_continue_grid':int(floorContinue.group(2)) if floorContinue else -1,
            'floor_continue_hp':int(floorContinue.group(3)) if floorContinue else -1,
            'minimap_initial':({'grid':int(minimapInitial.group(1)),'small_draws':int(minimapInitial.group(2)),
                'small_marker':[float(minimapInitial.group(4)),float(minimapInitial.group(5))],
                'large_draws':int(minimapInitial.group(6)),'large_marker':[float(minimapInitial.group(8)),float(minimapInitial.group(9))],
                'discovered':int(minimapInitial.group(10)),'refreshes':int(minimapInitial.group(11))} if minimapInitial else None),
            'minimap_rooms':minimapRooms,'minimap_transitions':minimapTransitions,'minimap_reentries':minimapReentries,
            'minimap_floor':({'stage':int(minimapFloor.group(1)),'grid':int(minimapFloor.group(2)),
                'refreshes':int(minimapFloor.group(3)),'discovered':int(minimapFloor.group(4)),
                'small_marker':[float(minimapFloor.group(5)),float(minimapFloor.group(6))],
                'large_marker':[float(minimapFloor.group(7)),float(minimapFloor.group(8))]} if minimapFloor else None),
            'generated_pickup':({'seed':int(generatedPickupPath.group(1)),'rooms':int(generatedPickupPath.group(2)),
                'route':[int(v) for v in generatedPickupPath.group(3).split(',')],
                'seeds':[int(generatedPickupRoom.group(i)) for i in (1,2,3,4)] if generatedPickupRoom else [],
                'key_grid':int(generatedPickupRoom.group(5)) if generatedPickupRoom else -1,
                'bomb_grid':int(generatedPickupRoom.group(6)) if generatedPickupRoom else -1,
                'blasts':generatedPickupBlasts,
                'bombs':generatedPickupBombRecords,
                'key':({'grid':int(generatedPickupKey.group(1)),'keys':int(generatedPickupKey.group(2)),
                    'bombs':int(generatedPickupKey.group(3)),'ticks':int(generatedPickupKey.group(4)),
                    'global_mt_unchanged':bool(int(generatedPickupKey.group(5))),
                    'hp':int(generatedPickupKey.group(6)),'max_hp':int(generatedPickupKey.group(7)),
                    'player_hits':int(generatedPickupKey.group(8))} if generatedPickupKey else None),
                'reentry':({'consumed':int(generatedPickupReentry.group(1)),'total':int(generatedPickupReentry.group(2)),
                    'active':int(generatedPickupReentry.group(3)),'activations':int(generatedPickupReentry.group(4)),
                    'no_recollect':bool(int(generatedPickupReentry.group(5)))} if generatedPickupReentry else None),
                'control':({'saved':bool(int(generatedPickupControl.group(1))),
                    'captured':bool(int(generatedPickupControl.group(2))),
                    'restored':bool(int(generatedPickupControl.group(3))),
                    'records_match':bool(int(generatedPickupControl.group(4))),
                    'present':bool(int(generatedPickupControl.group(5))),'capture':generatedPickupControl.group(6),
                    'recorded_entities':int(generatedPickupControl.group(7)),
                    'saved_pickups':int(generatedPickupControl.group(8)),
                    'current_grid':int(generatedPickupControl.group(9))} if generatedPickupControl else None),
                'control_records':generatedPickupControlRecords,
                'native':({'saved':bool(int(generatedPickupNative.group(1))),
                    'captured':bool(int(generatedPickupNative.group(2))),
                    'restored':bool(int(generatedPickupNative.group(3))),
                    'resources':bool(int(generatedPickupNative.group(4))),
                    'consumed_absent':bool(int(generatedPickupNative.group(5))),
                    'active_pickups':int(generatedPickupNative.group(6)),
                    'duplicates':int(generatedPickupNative.group(7)),
                    'contact_radius_verified':bool(int(generatedPickupNative.group(8))),
                    'no_restored_contact':bool(int(generatedPickupNative.group(9))),
                    'capture':generatedPickupNative.group(10),'gap':generatedPickupNative.group(11)} if generatedPickupNative else None),
                'restored_contact':({'cell':int(generatedPickupRestoredContact.group(1)),
                    'bomb_radius':float(generatedPickupRestoredContact.group(2)),
                    'bomb_within_radius':bool(int(generatedPickupRestoredContact.group(3))),
                    'key_cell':int(generatedPickupRestoredContact.group(4)),
                    'room_identity':bool(int(generatedPickupRestoredContact.group(5)))} if generatedPickupRestoredContact else None),
                'restored_state':({'static_grids':int(generatedPickupRestoredState.group(1)),
                    'records':int(generatedPickupRestoredState.group(2)),
                    'actors':bool(int(generatedPickupRestoredState.group(3))),
                    'space':bool(int(generatedPickupRestoredState.group(4))),
                    'frame':int(generatedPickupRestoredState.group(5)),
                    'path':bool(int(generatedPickupRestoredState.group(6)))} if generatedPickupRestoredState else None),
                'noncurrent':({'restored':bool(int(generatedPickupNoncurrent.group(1))),
                    'static_grids':int(generatedPickupNoncurrent.group(2)),
                    'actors':bool(int(generatedPickupNoncurrent.group(3))),
                    'space':bool(int(generatedPickupNoncurrent.group(4))),
                    'path':bool(int(generatedPickupNoncurrent.group(5))),
                    'active_pickups':int(generatedPickupNoncurrent.group(6)),
                    'untouched_room_control':bool(int(generatedPickupNoncurrent.group(7))),
                    'actual_untouched_entry':bool(int(generatedPickupNoncurrent.group(8))),
                    'resources':bool(int(generatedPickupNoncurrent.group(9))),
                    'key_cell':int(generatedPickupNoncurrent.group(10)),
                    'bomb_cell':int(generatedPickupNoncurrent.group(11)),
                    'frame':int(generatedPickupNoncurrent.group(12))} if generatedPickupNoncurrent else None),
                'ambient_summaries':ambientSummaries,
                'ambient_records':ambientRecords,
                'status':'passed' if pickupPassed else 'incomplete',
                'complete':pickupPassed} if generatedPickupPath else {'status':'not_reached','complete':False}),
            'pending_restart':({'failed_stop':pendingRestart.group(1),
                'failed_context_cleared':bool(int(pendingRestart.group(2))),
                'failed_continue_restart_virgin':bool(int(pendingRestart.group(3))),
                'successful_continue_restart_virgin':bool(int(pendingRestart.group(4)))} if pendingRestart else None),
            'native_pickup_codec_fixture':({'pickup470':codecFixture.group(1),
                'pickup4e0':codecFixture.group(2),'native_d8':codecFixture.group(3),
                'full_word':bool(int(codecFixture.group(4))),
                'invalid_crc_rejected_without_mutation':bool(int(codecFixture.group(5))),
                'unsupported_extra_rejected_without_mutation':bool(int(codecFixture.group(6)))} if codecFixture else None),
            'native_grid_codec_fixture':({'type4':bool(int(gridCodecFixture.group(1))),
                'type6':bool(int(gridCodecFixture.group(2))),
                'type15':bool(int(gridCodecFixture.group(3))),
                'unsupported_mapped_classes_rejected':bool(int(gridCodecFixture.group(4))),
                'post_init_counts':[int(gridCodecFixture.group(i)) for i in (5,6,7)],
                'frames':[int(gridCodecFixture.group(i)) for i in (8,9,10)],
                'tails':[gridCodecFixture.group(i) for i in (11,12,13)],
                'complete':gridCodecPassed} if gridCodecFixture else None),
            'item_pool':int(item.group(1)) if item else -1,
            'item_id':int(item.group(2)) if item else 0,
            'item_collected':int(item.group(3)) if item else 0,
            'item_owned':bool(int(item.group(4))) if item else False,
            'item_pool_counter':int(item.group(5)) if item else 0,
            'item_effect_hp':int(item.group(6)) if item and item.group(6) else 0,
            'item_effect_keys':int(item.group(8)) if item and item.group(8) else 0,
            'bomb_placed':int(bomb.group(1)) if bomb else 0,
            'bomb_explosions':int(bomb.group(2)) if bomb else 0,
            'bomb_enemy_hits':int(bomb.group(3)) if bomb else 0,
            'bomb_bombs_left':int(bomb.group(4)) if bomb else -1,
            'bomb_enemies_left':int(bomb.group(5)) if bomb else -1,
            'characters_ok':int(chars.group(1)) if chars else 0,
            'characters_fail':int(chars.group(2)) if chars else -1,
            'hard_mode':int(hard.group(1)) if hard else -1,
            'hard_continue_mode':int(hard.group(7)) if hard and hard.group(7) else -1,
            'hard_ready':(int(hard.group(2))==5) if hard else False,
            'hard_hp':int(hard.group(4)) if hard else -1,
            'stat_fire_delay':float(stat.group(1)) if stat else 0.0,
            'stat_damage':float(stat.group(2)) if stat else 0.0,
            'stat_speed':float(stat.group(3)) if stat else 0.0,
            'stat_luck':float(stat.group(4)) if stat else 0.0,
            'stat_order_independent':int(statOrder.group(1)) if statOrder else 0,
            'stat_resource_hp':int(statOrder.group(2)) if statOrder else -1,
            'stat_cache_damage':float(statOrder.group(4)) if statOrder else 0.0,
            'stat_cache_speed':float(statOrder.group(5)) if statOrder else 0.0,
            'conditional_crown':int(conditional.group(1)) if conditional else 0,
            'conditional_crown_disabled':int(conditional.group(2)) if conditional else 0,
            'conditional_lust':int(conditional.group(3)) if conditional else 0,
            'conditional_gust':int(conditional.group(4)) if conditional else 0,
            'conditional_dprince':int(conditional.group(5)) if conditional else 0,
            'conditional_blood_oath':int(conditional.group(6)) if conditional else 0,
            'conditional_oath_damage':float(conditional.group(9)) if conditional else 0.0,
            'conditional_oath_speed':float(conditional.group(10)) if conditional else 0.0,
            'active_batch_belial':int(activeBatch.group(1)) if activeBatch else 0,
            'active_batch_tammy':int(activeBatch.group(2)) if activeBatch else 0,
            'active_batch_shadows':int(activeBatch.group(3)) if activeBatch else 0,
            'active_batch_mrboom':int(activeBatch.group(4)) if activeBatch else 0,
            'active_batch_d8':int(activeBatch.group(5)) if activeBatch and activeBatch.group(5) else 0,
            'reroll_purity':int(reroll.group(1)) if reroll else 0,
            'reroll_treatment':int(reroll.group(2)) if reroll else 0,
            'reroll_d8':int(reroll.group(3)) if reroll else 0,
            'reroll_missing_no':int(reroll.group(4)) if reroll else 0,
            'boss_award_identity':int(bossAward.group(1)) if bossAward else 0,
            'boss_award_achievement':int(bossAward.group(2)) if bossAward else 0,
            'boss_award_persisted':int(bossAward.group(3)) if bossAward else 0,
            'native_trigger_perm_damage':float(trigger.group(1)) if trigger else 0.0,
            'native_trigger_perm_range':float(trigger.group(2)) if trigger else 0.0,
            'native_trigger_purity':int(trigger.group(3)) if trigger else 0,
            'native_trigger_active':int(trigger.group(4)) if trigger else 0,
            'native_trigger_stacks':int(trigger.group(5)) if trigger else 0,
            'native_trigger_crown_disabled':int(trigger.group(6)) if trigger else 0,
            'boss_attack_gurdy':1 if 'gurdy=1' in bossFlags else 0,
            'boss_attack_pin':1 if 'pin=1' in bossFlags else 0,
            'boss_attack_peep':1 if 'peep=1' in bossFlags else 0,
            'boss_attack_fistula':1 if 'fistula=1' in bossFlags else 0,
            'boss_attack_megamaw':1 if 'megamaw=1' in bossFlags else 0,
            'boss_attack_monstro':1 if 'monstro=1' in bossFlags else 0,
            'boss_attack_larry':1 if 'larry=1' in bossFlags else 0,
            'boss_attack_duke':1 if 'duke=1' in bossFlags else 0,
            'boss_attack_haunt':1 if 'haunt=1' in bossFlags else 0,
            'boss_attack_gemini':1 if 'gemini=1' in bossFlags else 0,
            'boss_attack_chub':1 if 'chub=1' in bossFlags else 0,
            'boss_attack_gurdyjr':1 if 'gurdyjr=1' in bossFlags else 0,
            'boss_attack_megafatty':1 if 'megafatty=1' in bossFlags else 0,
            'boss_attack_bumbino':1 if 'bumbino=1' in bossFlags else 0,
            'boss_attack_plum':1 if 'plum=1' in bossFlags else 0,
            'boss_attack_loki':1 if 'loki=1' in bossFlags else 0,
            'death_fx':1 if 'deathfx=1' in bossFlags else 0,
            'audio_device':int(audio.group(1)) if audio else 0,
            'audio_sfx':int(audio.group(2)) if audio else 0,
            'audio_loaded':int(audio.group(3)) if audio else 0,
            'audio_music':int(audio.group(4)) if audio else 0,
            'audio_music_frames':int(audio.group(5)) if audio else 0,
            'audio_sfx_frames':int(audio.group(6)) if audio else 0,
            'audio_rms':float(audio.group(7)) if audio else 0.0,
            'audio_gameplay_sfx':int(audio.group(8)) if audio else 0,
            'music_set_intro':int(musicSet.group(1)) if musicSet else 0,
            'music_set_layer':int(musicSet.group(2)) if musicSet else 0,
            'music_set_layer_frames':int(musicSet.group(3)) if musicSet else 0,
            'blackheart_absorbed':int(blackheart.group(1)) if blackheart else 0,
            'blackheart_soul':int(blackheart.group(2)) if blackheart else -1,
            'blackheart_red':int(blackheart.group(3)) if blackheart else -1,
            'walkanim_clip':walkanim.group(1) if walkanim else '',
            'walkanim_frame':float(walkanim.group(2)) if walkanim else 0.0,
            'walkanim_stopped':int(walkanim.group(3)) if walkanim else 0,
            'doorvis_opened':int(doorvis.group(1)) if doorvis else 0,
            'doorvis_locked':int(doorvis.group(2)) if doorvis else 0,
            'pocket_hp':int(pocket.group(1)) if pocket else -1,
            'pocket_max_hp':int(pocket.group(2)) if pocket else -1,
            'pocket_bombs':int(pocket.group(3)) if pocket else -1,
            'active_id':int(active.group(1)) if active else 0,
            'active_hp':int(active.group(2)) if active else -1,
            'familiar_spawned':int(familiar.group(1)) if familiar else 0,
            'familiar_shots':int(familiar.group(2)) if familiar else 0,
            'familiar_hits':int(familiar.group(3)) if familiar else 0,
            'continue_stage':int(cont.group(1)) if cont else -1,
            'continue_hp':int(cont.group(2)) if cont else -1,
            'continue_keys':int(cont.group(4)) if cont else -1,
            'native_continue_seed':int(nativeCont.group(1)) if nativeCont else 0,
            'native_continue_stage':int(nativeCont.group(2)) if nativeCont else -1,
            'adapter_session_intent':({'fresh_new_run_with_save':bool(int(selectedIntent.group(1))),
                'fresh_seed':int(selectedIntent.group(2)),'fresh_player':int(selectedIntent.group(3)),
                'continue_slot':int(selectedIntent.group(4)),'continue_mode':int(selectedIntent.group(5)),
                'continue_seed':int(selectedIntent.group(6)),'continue_stage':int(selectedIntent.group(7)),
                'stale_preload_action_calls':int(selectedIntent.group(8)),
                'preload_value_calls':int(selectedIntent.group(9)),
                'preload_pressed_calls':int(selectedIntent.group(10)),
                'preload_drop_calls':int(selectedIntent.group(11)),
                'preload_held_calls':int(selectedIntent.group(12)),
                'preload_connected_calls':int(selectedIntent.group(13)),
                'preload_frame_controller':int(selectedIntent.group(14)),
                'preload_frame_player_token':int(selectedIntent.group(15)),
                'restored_controller':int(selectedIntent.group(16)),
                'restored_player_token':int(selectedIntent.group(17)),
                'cold_continue_track':int(selectedIntent.group(18)),
                'native364':int(selectedIntent.group(19)),
                'cold_continue_slot':int(selectedIntent.group(22)),
                'native_file_retained':True,'second_cold_continue':True,
                'invalid_continue_without_fallback':bool(int(selectedIntent.group(24))),
                'consumed_room_pickups_after_two_continues':[int(selectedIntent.group(25)),int(selectedIntent.group(26))],
                'complete':selectedIntentPassed} if selectedIntent else
                {'fresh_new_run_with_save':False,'fresh_seed':0,'fresh_player':-1,
                 'continue_slot':-1,'continue_mode':-1,'continue_seed':0,'continue_stage':0,
                 'stale_preload_action_calls':-1,'preload_value_calls':-1,
                 'preload_pressed_calls':-1,'preload_drop_calls':-1,'preload_held_calls':-1,
                 'preload_connected_calls':-1,'preload_frame_controller':-1,
                 'preload_frame_player_token':-1,'restored_controller':-1,'restored_player_token':-1,
                 'cold_continue_track':-1,'native364':0,'cold_continue_slot':-1,
                 'native_file_retained':False,'second_cold_continue':False,
                 'invalid_continue_without_fallback':False,'complete':False}),
            'native_current_stage':int(nativeCurrent.group(1)) if nativeCurrent else -1,
            'native_current_hp':int(nativeCurrent.group(2)) if nativeCurrent else -1,
            'native_current_keys':int(nativeCurrent.group(5)) if nativeCurrent else -1,
            'native_current_active':int(nativeCurrent.group(8)) if nativeCurrent else 0,
            'native_current_card':int(nativeCurrent.group(9)) if nativeCurrent else 0,
            'native_current_pill':int(nativeCurrent.group(10)) if nativeCurrent else -1,
            'native_current_pool_counter':int(nativeCurrent.group(11)) if nativeCurrent else 0,
            'native_current_x':float(nativeCurrent.group(12)) if nativeCurrent and nativeCurrent.group(12) else 0.0,
            'native_current_y':float(nativeCurrent.group(13)) if nativeCurrent and nativeCurrent.group(13) else 0.0,
            'native_current_fire_delay':float(nativeCurrent.group(14)) if nativeCurrent and nativeCurrent.group(14) else 0.0,
            'native_current_damage':float(nativeCurrent.group(15)) if nativeCurrent and nativeCurrent.group(15) else 0.0,
            'native_current_effects':int(nativeCurrent.group(18)) if nativeCurrent and nativeCurrent.group(18) else 0,
            'native_current_effect_timer':int(nativeEffect.group(2)) if nativeEffect else 0,
            'greed_wave':int(greed.group(1)) if greed else 0,
            'greed_waves':int(greed.group(2)) if greed else 0,
            'greed_room_type':int(greed.group(4)) if greed else -1,
            'greed_enemies':int(greed.group(6)) if greed else 0,
            'greed_complete':bool(greedDone),
            'greed_boss_room':int(greedBoss.group(1)) if greedBoss else -1,
            'greed_boss_ultra':int(greedBoss.group(5)) if greedBoss else 0,
            'greed_boss_shots':int(greedBossShots.group(1)) if greedBossShots else 0,
            'greed_continue_wave':int(greedContinue.group(1)) if greedContinue else -1,
            'greed_continue_room':int(greedContinue.group(2)) if greedContinue else -1,
            'native_greed_continue_wave':int(greedNative.group(1)) if greedNative else -1,
            'native_greed_continue_room':int(greedNative.group(2)) if greedNative else -1,
            'native_greed_continue_mode':int(greedNative.group(4)) if greedNative and greedNative.group(4) else -1,
            'dying_ticks':int(deathflow.group(1)) if deathflow else 0,
            'game_over':bool(int(deathflow.group(2))) if deathflow else False,
            'restart_ready':bool(deathflow and int(deathflow.group(4))==5) if deathflow else False,
            'collision_stop':('%.9g'%float(gameplay.group(4)),'%.9g'%float(gameplay.group(5))) if gameplay else None,
            'collision_stable':bool(int(gameplay.group(6))) if gameplay else False,
            'checkpoint_returned':'CHECKPOINT buffer=' in log,'resident_restart_checked':'RESTART invalidRetained=1' in log,
            'bounded_player_tail_executed':bool(tail),'tail_ticks':int(tail.group(1)) if tail else 0,
            'tail_trail_segments':int(tail.group(2)) if tail else 0,
            'tail_render_callbacks':int(tail.group(3)) if tail else 0,
            'tail_reached_fields_present':[bool(int(tail.group(i))) for i in (4,5,6,7)] if tail else []},
        'scope':('Focused owned session execution: normal New Run reaches an initialized player and repeated whole simulation ticks in its generated starting room. '
            'A cold selected-slot regression begins at music track0 without a character stinger, keeps a valid native save present in the fresh slot while UI_START_CHARACTER starts the selected fresh seed/player, preserves a separate valid canonical save in Continue slot1, routes UI_CONTINUE_RUN through the same LocalRunFilenames native path, and performs two cold Continues from the unchanged canonical file. '
            'The native load path rebuilds the restored player identity before room setup, so the pre-load caller frame with its stale controller/player token performs zero owned input queries; setup uses the frame constructed from the restored creation, and the next live gameplay frame is built with the restored owners. Both the original cold and restored controller/player tokens are zero, with no neutralized or substituted action source. '
            'The non-stinger room transition selects original791210 with the evaluated 0.08 interpolation rate, while the call path retains the original791110 immediate volume selector for its one-rate branch and leaves 95/64 stingers queued. '
            'The saved nativeMode field from game+243358 drives normal/hard context and greed metadata selects modes2/3 before world entry; normal mode0, hard mode1 and greed mode2 Continue cases are checked. Invalid selected-slot Continue is rejected at CRC/prevalidation without New Run fallback or runtime mutation. '
            'Each tick completes 414dc0 actor/player update, 6712a0 collision, the disabled 8d8470 particle gate, the 8798f0 room-effect cache Update and the primary projectile lifecycle using the evaluated shot vector and fireDelay/shotSpeed/damage/range stats; host evidence observes spawn, wall collision and range expiry. '
            'A stage-owned generated Gaper room constructs its actual spawn plan, advances the enemy, kills it with projectiles, sets room clear and opens a normal door. A cleared normal door accepts the player, constructs the exact linked resident (rocks and enemies), places the player at the reciprocal door and continues whole ticks. '
            'A BFS over the generated layout door graph then clears three ordinary rooms with tears, enters each linked resident through its native door actor, kills the selected boss (identity from the generated room subtype), verifies the boss death opens the trapdoor, descends through trapdoor contact to stage2 with resources preserved, and continues the descended save at the same floor/grid/resources. '
            'Both minimap draw lists are rebuilt through the entered descriptor and real door graph; the seed1 route records marker movement and discovery retention across 84,72,74,61, plus a 72->84->72 reentry that retains room/door/perimeter storage and consumes only the existing one-draw room transition lifecycle. Saved type6 entity and grid18 icon branches were not reached. '
            'A seed12352 generated key30/bomb40 route uses genuine movement/action8 ticks; the maintained 90-radius bomb system removes the four then six in-radius grids and retains 31 then25 out-of-radius grids, then real contact consumes bomb/key. '
            'Control and consumed current-room type5 records round-trip through the native GSR0142 room-entity layout with exact source-backed fields; consumed owners remain absent after two canonical Continue loads and a real in-radius restored contact cannot recollect. '
            'A separate generated-save CODEC fixture verifies native470, the full 4-byte native4e0, nativeD8, and fail-closed handling of invalid CRC and unsupported extra payloads without starting a New Run. A separate grid fixture verifies native post-Init counts/frames and class/tail behavior for reached saved classes4/5/6/11/13/15/25/26 and rejects genuinely unknown mappings. '
            'A generated Gaper encounter damages and kills the player through six contact hits; player HP reaches zero, a 60-tick dying state reaches game over, and restart rebuilds a ready player at full health. Treasure doors stay closed without a key, consume one key on contact, and boss doors open only after the boss-defeated flag. '
            'Native 72c430 executes the reached no-item timer/counter/direction state; 736d70 executes a bounded post-755870 tail through the 73891f state checks, effect-cache update, reached 747490/74ffa0 clean owner checks, no-item sprite anchors, first 7697a0 trail block and 7f0e50 no-callback return. '
            'The reached save reconstructs current and previously visited room grid plus reached type5 state, preserves surviving cells, retains cleared cells as absent as required by the reached test scenario, and traverses through restored geometry by input. Its current starting room restores reached type1000 ambient variants33 and68 through the 79dab0->6bb0c0->79d0b0 order: variant33 consumes two global-MT bounds draws at79d359/79d37e, while variant68 runs vtable+0xc5b0d50 immediately; serialized word+20 restores only native+27c, while frameCount remains the separate66f544/674ef3 owner. '
            'Door common tail and class4/5/6/11/13/15/25/26 grid lifecycle fields are preserved across capture and restore. An unvisited descriptor is retained as a negative control, and a real unvisited generated room entry remains generated rather than blank. Valid unsupported-actor saves return 0079daf0 after pending-context installation; failed-Continue and successful-Continue restarts both clear pending state and traverse to a virgin key/bomb room. '
            'Deferred presentation/room visuals: 747490/74ffa0 item/effect branches, full 739451/73d612 consumers, remaining 7ae620 visual services, GPU rendering/audio. Deferred gameplay: remaining enemy variants/AI/death FX, 72c430 item/controlled branches, 713120 D6 grant, profile content stops, target adapters, target execution.'),
        'sha256':{f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(paths)}}
    (ROOT/'build/gameplay/result.json').write_text(json.dumps(packet,indent=2)+'\n')
if __name__=='__main__':main()
