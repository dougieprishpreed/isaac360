"""Bind one verified host run and a compact documentation checkpoint."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,re,shutil
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    from audit_port import main as audit
    audit()
    pointer=json.loads((ROOT/'runtime/port/normal_flow.json').read_text());run=json.loads((ROOT/pointer['run']).read_text())
    assert run['checks'][1]==0 and run['flows']==[run['seeds']*18,run['seeds']*18,0,0,0]
    assert run['room_spawns']==[97252] and run['room_markers']==[6660] and run['room_setup']==[15360]    assert run['door_actors']==[12288] and run['door_variants']==[98304] and run['door_restore']==[1120] and run['door_mt']==[45]    assert run['room_space']==[66568] and run['room_collision']==[2240] and run['room_collision_positions']==[5376]    assert run['room_bounds']==[384]    assert run['player_input']==[45440] and run['player_input_frames']==[1024]    assert run['player_velocity']==[18432] and run['player_velocity_frames']==[2048]    assert run['owned_session']==[64,0] and run['static_walls']==[18,1] and run['retry_continuation']==[1]    assert run['construction_contexts'][0]>run['checks'][2] and run['construction_contexts'][1]>0    assert run['generation_regression']['matched'] and run['generation_regression']['compared_contexts']==min(128,run['seeds'])*18    assert run['player_startup']==[8,1071]    assert run['entity_body']==[16,36]    assert run['player_sprites']==[152,6848]    assert run['player_state']==[16,97] and run['player_effects']==[955,12]    assert run['player_aux_sprites']==[9,93,2811]
    raw=(ROOT/'logs/normal-flow-build.log').read_bytes()
    build=raw.decode('utf-16') if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else raw.decode('utf-8-sig')
    assert not re.search(r'\b(?:warning|error)\s+[A-Z]\d+',build,re.I)
    coverage=dict(format='normal-game-acceptance-v15',execution=pointer,checks=run['checks'],first_floor_contexts=run['flows'][0],
        room_spawns=run['room_spawns'],markers=run['room_markers'],room_setup=run['room_setup'],world_capacity=run['world_capacity'],world_entries=run['world_entries'],        door_actors=run['door_actors'],door_variants=run['door_variants'],door_restore=run['door_restore'],door_mt=run['door_mt'],        room_space=run['room_space'],room_collision=run['room_collision'],room_collision_positions=run['room_collision_positions'],room_bounds=run['room_bounds'],        player_input=run['player_input'],player_input_frames=run['player_input_frames'],        player_velocity=run['player_velocity'],player_velocity_frames=run['player_velocity_frames'],        owned_session=run['owned_session'],static_walls=run['static_walls'],construction_contexts=run['construction_contexts'],retry_continuation=run['retry_continuation'],        player_startup=run['player_startup'],        entity_body=run['entity_body'],        player_sprites=run['player_sprites'],        player_state=run['player_state'],player_effects=run['player_effects'],        player_aux_sprites=run['player_aux_sprites'],
        full_original_runs=0,alpha_rc=False,native_Init_complete=False,dynamic_saves_complete=False,
        acceptance_dimensions=dict(boot='No current target execution.',rendering='Original frontend retained. Body/head and ten original auxiliary Sprite instances prepare through the shared session, including inherited XML assets absent from the normal cache. Native viewport-scale and layer blend bindings await real renderer resolution; GPU drawing, costumes and current target rendering remain untested/open.',controls='Fresh-player RNG banks, numeric health/resources, zero inventory and temporary-effect cache are owned before floor setup; generation consumes their snapshot. Native input getters and velocity stage through0072b71b checked. Full object/base Init, grants/stats/costumes/effect callbacks, action producers, update tail, hardware mapping and position/collision integration remain open. Owned session executes zero complete player ticks.',combat='Selected ordinary static walls supply original collision fields; remaining factories, weapon firing and live combat remain open.',progression='Owned frontend/session path automatically constructs the selected starting-room component after generation, then stops before full native player/entry initialization. Full stages/runs remain open.'),
        audit=dict(path='runtime/port/development_audit.json',sha256=sha(ROOT/'runtime/port/development_audit.json')))
    (ROOT/'runtime/port/normal_acceptance.json').write_text(json.dumps(coverage,indent=2)+'\n')
    out=ROOT/'logs'/('normal_setup_milestone_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));out.mkdir()
    paths=list((ROOT/'docs').glob('*.md'))+[ROOT/'AGENTS.md',ROOT/'README.md',ROOT/'runtime/port/normal_acceptance.json',ROOT/'runtime/port/development_audit.json']    paths+=[ROOT/'runtime/port/normal_cache_equivalence.json',ROOT/'runtime/port/generated_bootstrap_check.json']
    files=[]
    for p in sorted(paths):
        dest=out/'snapshot'/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        files.append(dict(path=p.relative_to(ROOT).as_posix(),frozen=dest.relative_to(out).as_posix(),sha256=sha(p)))
    result=dict(format='normal-generation-checkpoint-v3',execution=pointer,acceptance=coverage,files=files,
        scope='Source, executable and input snapshots are bound once in the execution packet. This compact checkpoint freezes the current guides; no duplicated parent trees.')
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    for e in files:assert sha(out/e['frozen'])==e['sha256']
    (ROOT/'runtime/port/normal_generation_milestone.json').write_text(json.dumps(dict(path=(out/'results.json').relative_to(ROOT).as_posix(),sha256=sha(out/'results.json')),indent=2)+'\n')
    candidates=sorted(p for p in (ROOT/'logs').glob('normal_setup_milestone_*') if p.is_dir())
    for stale in candidates[:-2]:
        assert stale.resolve().parent==(ROOT/'logs').resolve() and not stale.is_symlink()
        shutil.rmtree(stale)
    print('Private checkpoint:',out.relative_to(ROOT),'- compact guides plus verified host execution; no target build/run.')
if __name__=='__main__':main()
