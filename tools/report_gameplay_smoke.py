"""Record the focused host execution's actual scope, without accepting gameplay."""
from pathlib import Path
import argparse,hashlib,json,re,datetime
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--seed',type=int,required=True);p.add_argument('--affinity',required=True);p.add_argument('--restart',action='store_true');p.add_argument('--repaired-report',action='store_true');a=p.parse_args()
    raw=(ROOT/'build/gameplay/smoke.log').read_bytes();log=raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
    session=re.search(r'^SESSION .*next=([0-9a-f]+) players=(\d+) ticks=(\d+)',log,re.M)
    if not session:raise ValueError('Missing actual shared-session result.')
    gameplay=re.search(r'^GAMEPLAY complete=(\d+) ticks=(\d+) collisionStop=\(([^,]+),([^)]+)\) stable=(\d+)',log,re.M)
    ticks=int(gameplay.group(2)) if gameplay else int(session[3])
    complete=int(gameplay.group(1)) if gameplay else int(session[2])
    tail=re.search(r'^TAIL ticks=(\d+) trailSegments=(\d+) renderCallbacks=(\d+) fields=12c0/2cc/12bc/1ca4 present=(\d)(\d)(\d)(\d)',log,re.M)
    head=re.search(r'^HEAD_STATE timers=(-?\d+)/(-?\d+) calls=(\d+)/(\d+) direction=(\d+)',log,re.M)
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
            'whole_gameplay_ticks':0,'head_timers':[int(head.group(1)),int(head.group(2))] if head else None,
            'head_calls':int(head.group(3)) if head else None,'head_direction':int(head.group(5)) if head else None,
            'collision_stop':('%.9g'%float(gameplay.group(3)),'%.9g'%float(gameplay.group(4))) if gameplay else None,
            'collision_stable':bool(int(gameplay.group(5))) if gameplay else False,
            'checkpoint_returned':'CHECKPOINT buffer=' in log,'resident_restart_checked':'RESTART invalidRetained=1' in log,
            'bounded_player_tail_executed':bool(tail),'tail_ticks':int(tail.group(1)) if tail else 0,
            'tail_trail_segments':int(tail.group(2)) if tail else 0,
            'tail_render_callbacks':int(tail.group(3)) if tail else 0,
            'tail_reached_fields_present':[bool(int(tail.group(i))) for i in (4,5,6,7)] if tail else []},
        'scope':'Focused owned session execution: normal New Run reaches an initialized player and repeated player update/collision ticks in its generated starting room. Native 72c430 executes the reached no-item timer/counter/direction state; 736d70 executes a bounded post-755870 tail through the 73891f state checks, effect-cache update, reached 747490/74ffa0 clean owner checks, no-item sprite anchors, first 7697a0 trail block and 7f0e50 no-callback return. Not accepted as a whole gameplay tick: the 747490/74ffa0 item/effect branches that write state, the full 739451/73d612 consumers and the 7ae620 post-414dc0 room tick are omitted. Deferred: 72c430 item/controlled branches, 713120 D6 grant, profile content stops, native file save/Continue, rendering/audio, target execution.',
        'sha256':{f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(paths)}}
    (ROOT/'build/gameplay/result.json').write_text(json.dumps(packet,indent=2)+'\n')
if __name__=='__main__':main()
