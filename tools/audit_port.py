"""Verify the current source/evidence closure and retain full-game gaps explicitly."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    pointer=json.loads((ROOT/'runtime/port/normal_flow.json').read_text());path=ROOT/pointer['run']
    assert sha(path)==pointer['sha256'];run=json.loads(path.read_text())
    assert run['exit_code']==0 and not run['failures'] and run['checks'][1]==0
    for e in run['files']:
        assert sha(ROOT/e['path'])==e['sha256'],e['path']
        assert sha(path.parent/e['frozen'])==e['sha256'],e['path']
    for e in run['reference_sources']:assert sha(ROOT/e['path'])==e['sha256'],e['path']
    for e in run['outputs']:assert sha(path.parent/e['path'])==e['sha256'],e['path']
    index=json.loads((ROOT/'runtime/game/manifest.json').read_text());rooms=json.loads((ROOT/'evidence/rooms.json').read_text())
    source=json.loads((ROOT/'runtime/port/source_data_check.json').read_text())
    assert source['saved_references_match'] and not source['original_program_launched']
    assert sha(ROOT/'runtime/game/file_index.bin')==index['binary_index_sha256']
    assert sha(ROOT/'runtime/game/file_index.jsonl')==index['file_index_sha256']
    contracts=[
        'Remaining original configuration, locale, eligibility and loader branches',
        'Complete native frontend/profile/slot/options/challenge/statistics transitions',
        'Native player initialization, controls, movement, health and inventory',
        'All applicable actor/grid/pickup/item/weapon/familiar/effect factories and behavior',
        'Contextual combat, damage, rewards, doors and room transitions',
        'Complete native Room::Init, visit/clear state and original callbacks',
        'Later-profile curses and all original RNG owners/consumption',
        'All stages/routes/modes/endings/unlocks and local multiplayer',
        'Complete dynamic run/profile/option serialization and platform durability',
        'Original gameplay rendering, animation, HUD, cameras, audio and music',
        'Complete runs without unimplemented reachable content',
        'User-requested Xenia validation and physical console acceptance']
    report=dict(format='port-development-audit-v1',execution=pointer,
        inventory=dict(prepared_files=index['entries']+len(index['loose_files']),room_sources=len(rooms),raw_room_records=sum(e['rooms'] for e in rooms)),
        verified_scope='Owned frontend/session generation and starting-room components, fresh first-player indexed RNG banks including later trinket97 consumption, numeric health/resources and inventory reset with explicit pending grants, temporary-effect cache operations and native health query feeding generation. Physical body, original body/head and ten auxiliary Sprite instances including four normally inherited XML cache misses, selected ordinary static walls/collision, contextual dependency diagnostics, room-space/input/velocity regressions, one complete initialized ordinary player and 563 whole simulation ticks: 414dc0 actor/player update, 6712a0 collision, disabled 8d8470 particle gate, 8798f0 room-effect cache Update, primary projectile spawn/wall/expiry lifecycle, an authentic generated Gaper encounter killed by those projectiles with room-clear/door state change, and interactive normal-door transition into the exact linked resident with player placement and continued ticks, plus the reached 72c430 head timer/counter state and 747490/74ffa0 clean owner checks. The remaining 7ae620 visual room services, full 747490/74ffa0 item/effect branches, full player item/base Init, grants/stats/costume/removal providers, renderer state resolution and gameplay drawing remain open. No whole-run or target parity claim.',
        open_contracts=contracts,full_original_runs=0,alpha_rc=False,new_target_build=False,xenia_run=False,
        input_bindings=[dict(path=p,sha256=sha(ROOT/p)) for p in ('runtime/game/manifest.json','evidence/rooms.json','runtime/port/source_data_check.json','evidence/last_target_observation.json')])
    (ROOT/'runtime/port/development_audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Verified current host evidence; original inventory:',report['inventory'],'; full-game contracts remain open.')
if __name__=='__main__':main()
