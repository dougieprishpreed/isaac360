"""Freeze and execute the normal-flow Windows milestone; never builds a target."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,collections,hashlib,json,shutil,subprocess,time
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def generation_regression(expected_path,actual_path,seeds):
    expected=expected_path.read_text().splitlines();actual=actual_path.read_text().splitlines()
    count=min(len(expected),len(actual));mismatches=[i for i in range(count) if expected[i]!=actual[i]]
    if len(actual)!=seeds*18+1:mismatches.append(-1)
    return dict(compared_contexts=max(0,count-1),matched=not mismatches,mismatch_rows=mismatches,
        scope='All recorded columns/identities/RNG owners against the preserved maintained host baseline, up to128 seeds. Not an original-game oracle.')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seeds',type=int,default=32);args=parser.parse_args()
    out=ROOT/'logs'/('normal_flow_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));out.mkdir()
    paths=[p for folder in ('src','tools','tests') for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in ('.cpp','.h','.hlsl','.vcxproj','.py','.cmd','.ps1','.json','.tsv') and '__pycache__' not in p.parts]
    paths+=list((ROOT/'build/generated').glob('*.h'))+list((ROOT/'build/normal_flow/data').glob('*'))
    paths+=[ROOT/p for p in ('build/normal_flow/normal_flow_check.exe','build/normal_flow/engine.lib','build/normal_flow/target_before.json','logs/normal-flow-build.log')]
    paths+=list((ROOT/'evidence/reference_instructions').glob('*.txt'))
    paths+=list((ROOT/'logs').glob('normal-*-reference.txt'))
    paths.append(ROOT/'runtime/port/normal_room_space_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_input_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_motion_reference.json')
    paths.append(ROOT/'runtime/port/normal_static_grid_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_startup_reference.json')
    paths.append(ROOT/'runtime/port/normal_entity_body_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_sprite_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_state_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_aux_sprite_reference.json')
    paths.append(ROOT/'runtime/port/normal_player_creation_reference.json')
    paths.append(ROOT/'runtime/port/normal_starting_grant_dependency.json')
    paths.append(ROOT/'runtime/port/normal_room_fx_reference.json')
    paths.append(ROOT/'runtime/port/native_player_runtime_reference.json')
    paths.append(ROOT/'runtime/port/native_item_pool_reference.json')
    paths.append(ROOT/'runtime/port/native_nightmare_reference.json')
    paths.append(ROOT/'runtime/port/native_persistence_reference.json')
    paths.append(ROOT/'evidence/reference_instructions/normal-native-checkpoint.txt')
    paths.append(ROOT/'evidence/reference_instructions/player-persistent-transfer.txt')
    paths.append(ROOT/'evidence/reference_instructions/player-persistent-reset.txt')
    paths.append(ROOT/'runtime/port/generated_bootstrap_check.json')
    paths+=[ROOT/p for p in ('runtime/port/normal_catalog.json','runtime/port/native_boss_data.json','runtime/port/normal_level_init_reference.json','runtime/port/normal_mid_special_reference.json','runtime/port/normal_grid_room_reference.json','runtime/port/normal_level_doors_reference.json','runtime/port/normal_room_entry_reference.json','runtime/port/normal_door_actors_reference.json')]
    files=[]
    for p in sorted(set(paths)):
        if not p.is_file():continue
        relative=p.relative_to(ROOT);frozen=out/'snapshot'/relative;frozen.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,frozen)
        files.append(dict(path=relative.as_posix(),sha256=sha(p),frozen=frozen.relative_to(out).as_posix()))
    manifest=json.loads((ROOT/'build/normal_flow/data/manifest.json').read_text())
    for e in manifest['sources']:assert sha(ROOT/e['path'])==e['sha256'],e['path']
    before=json.loads((ROOT/'build/normal_flow/target_before.json').read_text(encoding='utf-8-sig'))
    started=time.perf_counter()
    process=subprocess.run([str(out/'snapshot/build/normal_flow/normal_flow_check.exe'),str(out/'snapshot/build/normal_flow/data'),str(out/'events.tsv'),str(args.seeds)],cwd=ROOT,capture_output=True,text=True)
    duration=time.perf_counter()-started;(out/'stdout.txt').write_text(process.stdout);(out/'stderr.txt').write_text(process.stderr)
    regression=generation_regression(out/'snapshot/build/normal_flow/data/generation_baseline.tsv',out/'events.tsv.flows.tsv',args.seeds)
    counts={};gaps=collections.Counter();failures=[];constructions=[];entries=[];dependencies=[];sessions=[]
    for line in (out/'events.tsv').read_text().splitlines():
        fields=line.split('\t')
        if fields[0]=='TOTAL':counts[fields[1]]=list(map(int,fields[2:]))
        elif fields[0]=='GAP':gaps[fields[1]]+=1
        elif fields[0]=='FAIL':failures.append(fields[1:])
        elif fields[0]=='CONSTRUCT':constructions.append(dict(zip(('context','definition','result','native_mode','backdrop','config_stage','spawn','award','decoration','auxiliary','flags','visits','supported_candidates','missing_candidates'),map(int,fields[1:]))))
        elif fields[0]=='DEPENDENCY':dependencies.append(dict(zip(('context','type','variant','subtype','occurrences'),map(int,fields[1:]))))
        elif fields[0]=='SESSION_COMPONENTS':sessions.append(dict(zip(('seed','mode','profile_stage_gates','stage_type','definition','config_stage','spawn','award'),map(int,fields[1:]))))
        elif fields[0]=='SESSION_PREFIX':sessions.append(dict(zip(('seed','mode','profile_stage_gates','earned_award29','next_native_call','player_rng_hash','weapon_hash'),map(int,fields[1:]))))
        elif fields[0]=='WORLD_ENTRY':entries.append(dict(seed=int(fields[1]),curses=int(fields[2]),list_index=int(fields[3]),grid_index=int(fields[4]),definition=int(fields[5]),world_result=int(fields[6]),constructor_result=int(fields[7]),missing_identity=[int(n) for n in fields[8:11]]))
    if not regression['matched']:failures.append(['generation-regression',regression['mismatch_rows'][:16]])
    exit_code=process.returncode or int(not regression['matched'])
    (out/'selected_constructions.json').write_text(json.dumps(dict(format='normal-selected-constructions-v2',scope='Context0 is the explicitly seeded starting-room component regression; subsequent records are distinct actual selected descriptor contexts: definition, mode, backdrop, config stage, all four seeds, flags and visits. Unique raw-definition reading is separate. Component construction only; no original execution oracle.',records=constructions),separators=(',',':'))+'\n')
    (out/'selected_dependencies.json').write_text(json.dumps(dict(format='normal-selected-dependencies-v1',scope='All unsupported candidates in each original main-pass plan, grouped only for reporting by raw identity. Context IDs join selected_constructions.json; no selection, construction, RNG draw or dynamic child substitution occurs in inspection.',records=dependencies),separators=(',',':'))+'\n')
    (out/'session_components.json').write_text(json.dumps(dict(format='normal-owned-session-prefix-v3',scope='Title/slot/New Run/character actions through constructor-owned Init prefix, default weapon, one trinket97 draw, original player0 layer skin/ghost binding, English name, empty costume refresh and reached reset tail. Stops at70b483 ->76c1d0 before starting-resource side effects/callbacks/registration and generation. Profile cases are maintained clean/earned-award state regressions, not captured user saves. No complete initialized player or tick.',records=sessions),indent=2)+'\n')
    (out/'world_entry_cases.json').write_text(json.dumps(dict(format='normal-world-entry-cases-v1',scope='First four seeds, stage type0/mode0, three explicit curse-owner contexts. Selected grid room component entry/reentry; complete player/door/room-entry services remain open.',records=entries),indent=2)+'\n')
    targets=[]
    for e in before:
        digest=sha(ROOT/e['path']);assert digest==e['sha256'],e['path'];targets.append(dict(path=e['path'],sha256=digest))
    for e in files:assert sha(ROOT/e['path'])==e['sha256'] and sha(out/e['frozen'])==e['sha256'],e['path']
    results=dict(format='normal-flow-host-run-v1',created=datetime.now(timezone.utc).isoformat(),exit_code=exit_code,executable_exit_code=process.returncode,generation_regression=regression,elapsed_seconds=duration,host_single_cpu=True,seeds=args.seeds,
        checks=counts.get('checks'),flows=counts.get('flows'),room_spawns=counts.get('room_spawns'),room_markers=counts.get('room_markers'),room_setup=counts.get('room_setup'),mid_special=counts.get('mid'),secret_geometry=counts.get('secret_geometry'),secret_blacklists=counts.get('secret_blacklists'),offgrid=counts.get('offgrid'),level_doors=counts.get('level_doors'),world_capacity=counts.get('world_capacity'),world_entries=counts.get('world_entries'),initial_caller=counts.get('initial_caller'),gaps=dict(gaps),failures=failures,files=files,reference_sources=manifest['sources'],targets_unchanged=targets,
        door_actors=counts.get('door_actors'),door_variants=counts.get('door_variants'),door_restore=counts.get('door_restore'),door_mt=counts.get('door_mt'),
        room_space=counts.get('room_space'),room_collision=counts.get('room_collision'),room_collision_positions=counts.get('room_collision_positions'),room_bounds=counts.get('room_bounds'),
        player_input=counts.get('player_input'),player_input_frames=counts.get('player_input_frames'),
        player_velocity=counts.get('player_velocity'),player_velocity_frames=counts.get('player_velocity_frames'),
        construction_contexts=counts.get('construction_contexts'),static_walls=counts.get('static_walls'),owned_session=counts.get('owned_session'),retry_continuation=counts.get('retry_continuation'),
        record_cache=counts.get('record_cache'),player_initializations=counts.get('player_initializations'),
        player_startup=counts.get('player_startup'),
        entity_body=counts.get('entity_body'),
        player_sprites=counts.get('player_sprites'),
        player_state=counts.get('player_state'),player_effects=counts.get('player_effects'),
        player_aux_sprites=counts.get('player_aux_sprites'),
        scope=['Normal inherited catalogue and stage-owned identities/depletion; exact raw-record binding.',
            'Independent award2/7/7 clone per spawn pass; all normal catalog records/four explicit award seeds, exact choices/seeds/marker results. Native water/pit first-visit setup uses separate spawn owners.',
            'Normal post-log second layout draw and separate evaluated setup/curse owner; clean first stage before award33 initializes automatically with proven zero curses.',
            'Normal first-floor grid-room placement through bedroom, secret, type29 and ordinary selection; separate arithmetic vectors and explicit evaluated curse-owner fixtures.',
            'Topology RNG scan/tie ownership, selected metadata door blocking, retained secret seed/index1 and independent type29/index71 owner.',
            'Complete first-chapter grid/off-grid descriptor content caller and boss commit; 28 layout draws with separate index19/20 local owners in the off-grid tail.',
            'Native descriptor door finalization, one subsequent layout draw and exact original start identity; full507+18 descriptor room storage with lazy construction and mutation-preserving reentry.',
            'Native door factory/reset, all12 room shapes and256 enabled masks, explicit global MT ownership, restored grid-door state and integer variant selection before asset/animation/interaction services; generated-world first-room binding and retained state.',
            'Initial physical boundaries, thin/L geometry, scalar clamp/wrap/containment and native numeric collision queries; exact selected-room initialization and retained room/door collision fields. No player movement or swept collision acceptance.',
            'Native movement/shooting input getters, paired drop rules, explicit seed-effect/reflection transforms, original action-query order and controller/entity frame ownership; session binding to the actual selected normal world. No movement integration or weapon-firing acceptance.',
            'Native player velocity stage0072ae20..0072b71b: early returns, controls gates, paired resampling, evaluated cache effects, acceleration/damping, native entity fields and persistent owner frames. Remaining update tail, player initialization, position integration, swept collision and firing are open.',
            'Selected1999 ordinary walls construct owned grid15 state and collision4/cost1000 fields with selected seeds and reentry retention. Room16 graphics and automatic boundary-wall global consumers are open.',
            'Both adapters use one owned session creation lifecycle. Title/slot/New Run/character actions execute the native construction/Init prefix, reached player0 appearance and reset tail, then stop at70b483 ->76c1d0 before generation. The extracted floor/world component checks remain separate; no complete initialized player or tick is accepted.',
            'Owned fresh-player startup builds1071 indexed collectible/trinket/pill/card child RNG owners before floor setup. Manager/index13 remains undrawn; independent parent/index5 and bank/index6..9 clones supply default35 children and the fifth parent value for base entity Init. Player0 appearance resolves the original skin, restores the ghost sheet, refreshes empty costume slots and localizes the English Players name without RNG. Remaining Init/resource effects, registry/placement and complete ticks remain open.',
            'The retained fifth startup value feeds an owned physical body: original inherited properties, undrawn entity/index30 RNG, zero position/velocity and even/odd boundary samples. Body fields survive stopped creation calls and cold-prefix restart. This is the numeric base-entity subset; full Sprite/costume setup, player-specific reset, registry/placement and complete Init remain open. Variant1 checks are property fixtures, not local-player acceptance.',
            'The entity animation property feeds14 direct Sprite numeric owners and the original player cache record:38 clips,15 layers,8 null points and4 events. Native stopped WalkDown0/HeadDown0 survive session setup. Component timelines check fractional speeds, event/cursor ownership, loop resets and finishes. Other startup assets/costumes, GPU drawing, interpolation and full player Init/ticks remain open.',
            'Owned player0 numeric Init supplies health/resources, zero indexed inventory vectors, explicit pending original starting grants and the later trinket97 child draw. Source-bound ItemConfig fields feed temporary-effect cache operations with native count/timer/persistence/dirty/callback ordering. The owned normal session now stops before starting resources and does not supply a projected health/resource snapshot to generation; the metadata-derived state remains an explicitly separate extraction component. Full grants, stat evaluation, costume/removal providers, recursive cache mutation and complete Init/ticks remain open.',
            'The original70a344..8db asset block prepares ten auxiliary Sprite instances from nine assets: blood-gush Idle, both crowns and seven charge owners. Five original cache records match the independent XML conversion wordwise; four normal inherited XML cache misses use the saved-reader conversion. Layer1 crown blend preset2 and native viewport-scale dependencies are retained as bindings. Actual renderer-dependent states, GPU drawing, costumes and complete player Init remain open.',
            'Contextual construction uses actual backdrops/config stages/seeds/modes/flags/visits, reports every unsupported selected candidate, and rejects source/request/internal errors as failures. This is not dynamic-content closure.',
            'The imposed100-attempt native failure was removed; a controlled attempt101 continuation retains RNG order. Original diagnostic logger side effects and rare-path whole-layout fidelity remain unresolved.',
            'Whole native room entry/player/door services, dynamic serialization, later-profile curses and live progression remain open. No original full run or target work.'])
    results['outputs']=[dict(path=p.name,sha256=sha(p)) for p in sorted(out.glob('*')) if p.is_file()]
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    report=dict(format='normal-flow-current-v2',run=(out/'results.json').relative_to(ROOT).as_posix(),sha256=sha(out/'results.json'),checks=results['checks'],flows=results['flows'],mid_special=results['mid_special'],initial_caller=results['initial_caller'],owned_session=results['owned_session'],player_initializations=results['player_initializations'],player_startup=results['player_startup'],entity_body=results['entity_body'],player_sprites=results['player_sprites'],player_state=results['player_state'],player_effects=results['player_effects'],player_aux_sprites=results['player_aux_sprites'],construction_contexts=results['construction_contexts'],gaps=results['gaps'],full_original_runs=0,target_work_performed=False)
    (ROOT/'runtime/port/normal_flow.json').write_text(json.dumps(report,indent=2)+'\n')
    print(process.stdout.strip());print('Flow totals [cases, caller pending, geometry unresolved, guard, unexpected]:',results['flows']);print('Evidence:',out.relative_to(ROOT).as_posix()+'/results.json')
    #User-requested bounded retention: latest two complete run packets only.
    candidates=sorted(p for p in (ROOT/'logs').glob('normal_flow_*') if p.is_dir())
    for stale in candidates[:-2]:
        assert stale.resolve().parent==(ROOT/'logs').resolve() and not stale.is_symlink()
        shutil.rmtree(stale)
    return exit_code
if __name__=='__main__':raise SystemExit(main())
