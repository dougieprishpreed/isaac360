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
    item=re.search(r'^ITEM pool=(\d+) id=(\d+) collected=(\d+) owned=(\d+) poolCounter=(\d+)(?: effect_hp=(\d+)/(\d+) effect_keys=(\d+))?',log,re.M)
    bomb=re.search(r'^BOMB placed=(\d+) explosions=(\d+) enemyHits=(\d+) bombsLeft=(-?\d+) enemies=(\d+)',log,re.M)
    chars=re.search(r'^CHARACTERS ok=(\d+) fail=(\d+)',log,re.M)
    hard=re.search(r'^HARD mode=(\d+) phase=(\d+) player=(\d+) hp=(-?\d+)/(-?\d+)',log,re.M)
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
    greed=re.search(r'^GREED start wave=(\d+)/(\d+) room=(\d+) type=(\d+) variant=(\d+) enemies=(\d+)',log,re.M)
    greedDone=re.search(r'^GREED complete waves=(\d+) next=(\d+) room=(\d+)',log,re.M)
    greedBoss=re.search(r'^GREED boss room=(\d+) type=(\d+) variant=(\d+) enemies=(\d+) ultraGreed=(\d+)',log,re.M)
    greedBossShots=re.search(r'^GREED boss attack shots=(\d+)',log,re.M)
    greedContinue=re.search(r'^GREED continue wave=(\d+) room=(\d+) count=(\d+)',log,re.M)
    greedNative=re.search(r'^GREED native continue wave=(\d+) room=(\d+) count=(\d+)',log,re.M)
    deathflow=re.search(r'^DEATH_FLOW dyingTicks=(\d+) gameOver=(\d+) restartResult=(\d+) restartPhase=(\d+) restartHP=(-?\d+)/(-?\d+)',log,re.M)
    lock=re.search(r'^LOCK treasureSlot=(\d+) noKeyClosed=(\d+) keyOpened=(\d+) keysLeft=(-?\d+) doorsUnlocked=(\d+) bossOpenWithoutDefeat=(\d+) bossDefeated=(\d+)',log,re.M)
    paths=set()
    for folder in ('src/game','src/content','build/generated'):
        paths.update(f for f in (ROOT/folder).rglob('*') if f.suffix in ('.cpp','.h','.inc'))
    paths.update(ROOT/f for f in ('src/simulation/gameplay_smoke.cpp','tools/build_gameplay_smoke.ps1','tools/build_gameplay_smoke.cmd',
        'tools/prepare_player_runtime.py','tools/prepare_native_persistence.py','tools/prepare_room_runtime.py','tools/prepare_greed_catalog.py','tools/report_gameplay_smoke.py',
        'build/gameplay/gameplay_smoke.exe','build/gameplay/build.log','build/gameplay/smoke.log'))
    reports=('source_data_check.json','original_configuration.json','normal_player_creation_reference.json','native_player_runtime_reference.json',
        'native_item_pool_reference.json','native_nightmare_reference.json','native_persistence_reference.json','greed_catalog.json')
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
