"""Geometry/door-block fixtures from the pinned grid-room instruction range."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def prepare(out,stages,draw):
    ref=json.loads((ROOT/'runtime/port/normal_grid_room_reference.json').read_text())
    c=ref['constants'];card=list(zip(*(iter(c['00b4bb70']+c['00b4a310']),)*2))
    ring=list(zip(*(iter(c['00b4bbc0']+c['00b4bba0']+c['00b4a350']+c['00b4a3d0']),)*2))
    assert card==[(-1,0),(0,-1),(1,0),(0,1)]
    sizes=[None,(1,1),(1,1),(1,1),(1,2),(1,2),(2,1),(2,1),(2,2),(2,2),(2,2),(2,2),(2,2)]
    missing={9:(0,0),10:(1,0),11:(0,1),12:(1,1)}
    def at(x,y):return y*13+x if 0<=x<13 and 0<=y<13 else -1
    def allowed(shape,slot):
        w,h=sizes[shape]
        return not (h==1 and slot in (4,6) or w==1 and slot in (5,7) or shape in (3,5) and slot%2==0 or shape in (2,7) and slot%2==1)
    rows=[]
    for shape in range(1,13):
      for x,y in [(0,0),(0,5),(5,0),(5,5),(11,5),(5,11),(11,11)]:
        w,h=sizes[shape];occupied={at(x+dx,y+dy) for dy in range(h) for dx in range(w) if (dx,dy)!=missing.get(shape)}
        for seed in (1,17,0xffffffff,0x48c0ffee):
          for pattern in range(4):
            blocked={p for p in range(169) if pattern==3 or pattern==1 and p%3==0}
            reserved={p for p in range(169) if pattern==2 and p%5==0}
            for distant in (0,1):
                state=seed;draws=0;scores={}
                for cell in range(169):
                    if cell in occupied or cell in reserved or not distant and cell in blocked:continue
                    state=draw(state,35);draws+=1;score=10+state%5;neighbours=0;px,py=cell%13,cell//13
                    if distant and any(at(px+dx,py+dy) in occupied|reserved for dx,dy in card):continue
                    for d,(dx,dy) in enumerate(ring if distant else card):
                        if at(px+dx,py+dy) not in occupied:continue
                        invalid=any(not allowed(shape,(side+2)%4) for side,(sx,sy) in enumerate(card) if sx*dx+sy*dy>0) if distant else not allowed(shape,(d+2)%4)
                        if invalid:score-=100
                        if distant or not invalid:neighbours+=1
                    if neighbours and score>=0:scores[cell]=score-3*(neighbours<3)-3*(neighbours<2)
                cell=-1
                if scores:
                    best=max(scores.values());ties=[p for p,s in scores.items() if s==best]
                    assert best>0
                    state=draw(state,35);draws+=1;cell=ties[state%len(ties)]
                rows.append((shape,x,y,seed,pattern,distant,cell,state,draws))
    (out/'secret_geometry.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in rows),encoding='ascii')
    blacklists=[]
    for d in stages[0]:
        x,y=5,5;w,h=d['width']//13,d['height']//7;cells=[(x-1,y),(x,y-1),(x+w,y),(x,y+h),(x-1,y+1),(x+1,y-1),(x+w,y+1),(x+1,y+h)]
        blocked=set()
        for slot,(px,py) in enumerate(cells):
            if d['shape']==1 and slot>=4 or d['shape']==4 and slot in (5,7) or d['shape']==6 and slot in (4,6):continue
            if not d['mask']&(1<<slot) or d['type'] in (5,7,8):
                if at(px,py)>=0:blocked.add(at(px,py))
        blacklists.append([d['id']]+[int(p in blocked) for p in range(169)])
    (out/'secret_blacklists.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in blacklists),encoding='ascii')
    return len(rows),len(blacklists)
