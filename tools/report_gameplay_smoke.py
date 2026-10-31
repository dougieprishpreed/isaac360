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
    deathflow=re.search(r'^DEATH_FLOW dyingTicks=(\d+) gameOver=(\d+) restartResult=(\d+) restartPhase=(\d+) restartHP=(-?\d+)/(-?\d+)',log,re.M)
    lock=re.search(r'^LOCK treasureSlot=(\d+) noKeyClosed=(\d+) keyOpened=(\d+) keysLeft=(-?\d+) doorsUnlocked=(\d+) bossOpenWithoutDefeat=(\d+) bossDefeated=(\d+)',log,re.M)
    paths=set()
    for folder in ('src/game','src/content','build/generated'):
        paths.update(f for f in (ROOT/folder).rglob('*') if f.suffix in ('.cpp','.h','.inc'))
    paths.update(ROOT/f for f in ('src/simulation/gameplay_smoke.cpp','tools/build_gameplay_smoke.ps1','tools/build_gameplay_smoke.cmd',
        'tools/prepare_player_runtime.py','tools/prepare_native_persistence.py','tools/prepare_room_runtime.py','tools/report_gameplay_smoke.py',
        'build/gameplay/gameplay_smoke.exe','build/gameplay/build.log','build/gameplay/smoke.log'))
    reports=('source_data_check.json','original_configuration.json','normal_player_creation_reference.json','native_player_runtime_reference.json',
        'native_item_pool_reference.json','native_nightmare_reference.json','native_persistence_reference.json')
    paths.update(ROOT/'runtime/port'/f for f in reports)
    packet={'format':'focused-gameplay-host-v2','recorded_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
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
        'scope':'Focused owned session execution: normal New Run reaches an initialized player and repeated whole simulation ticks in its generated starting room. Each tick completes 414dc0 actor/player update, 6712a0 collision, the disabled 8d8470 particle gate, the 8798f0 room-effect cache Update and the primary projectile lifecycle using the evaluated shot vector and fireDelay/shotSpeed/damage/range stats; host evidence observes spawn, wall collision and range expiry. A stage-owned generated Gaper room constructs its actual spawn plan, advances the enemy, kills it with projectiles, sets room clear and opens a normal door. A cleared normal door accepts the player, constructs the exact linked resident (rocks and enemies), places the player at the reciprocal door and continues whole ticks. A generated Gaper encounter damages and kills the player through six contact hits; player HP reaches zero, a 60-tick dying state reaches game over, and restart rebuilds a ready player at full health. Treasure doors stay closed without a key, consume one key on contact, and boss doors open only after the boss-defeated flag. Native 72c430 executes the reached no-item timer/counter/direction state; 736d70 executes a bounded post-755870 tail through the 73891f state checks, effect-cache update, reached 747490/74ffa0 clean owner checks, no-item sprite anchors, first 7697a0 trail block and 7f0e50 no-callback return. Deferred presentation/room visuals: 747490/74ffa0 item/effect branches, full 739451/73d612 consumers, remaining 7ae620 visual services, GPU rendering/audio. Deferred gameplay: remaining enemy variants/AI/death FX, transitions, 72c430 item/controlled branches, 713120 D6 grant, profile content stops, native file save/Continue, target execution.',
        'sha256':{f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(paths)}}
    (ROOT/'build/gameplay/result.json').write_text(json.dumps(packet,indent=2)+'\n')
if __name__=='__main__':main()
