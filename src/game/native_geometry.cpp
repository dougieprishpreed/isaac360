#include "native_geometry.h"
namespace {
// Saved .data 00bc6d20, and the initialized footprint pointer table at
// 00bc56f0 / 008fc520. See docs/behavior_evidence.json for hashes.
const int sizes[13][2]={{0,0},{1,1},{1,1},{1,1},{1,2},{1,2},{2,1},{2,1},{2,2},{2,2},{2,2},{2,2},{2,2}};
const int directions[4][2]={{-1,0},{0,-1},{1,0},{0,1}};
NativeCell Cell(int x,int y) { NativeCell p={x,y}; return p; }
}
int NativeCellIndex(NativeCell c) { return c.x>=0 && c.x<13 && c.y>=0 && c.y<13?c.y*13+c.x:-1; }
bool NativeShapeSize(int shape,int* width,int* height) {
    if(shape<1 || shape>12 || !width || !height) return false;
    *width=sizes[shape][0]; *height=sizes[shape][1]; return true;
}
int NativeShapeFootprint(int shape,NativeCell* cells) {
    if(!cells || shape<1 || shape>12) return 0;
    int n=0;
    for(int y=0;y<sizes[shape][1];++y) for(int x=0;x<sizes[shape][0];++x) {
        if((shape==9 && x==0 && y==0) || (shape==10 && x==1 && y==0) ||
           (shape==11 && x==0 && y==1) || (shape==12 && x==1 && y==1)) continue;
        cells[n++]=Cell(x,y);
    }
    return n;
}
bool NativeShapeDoorAllowed(int shape,int slot,bool thin) {
    if(shape<1 || shape>12 || slot<0 || slot>7) return false;
    if(sizes[shape][1]==1 && (slot==4 || slot==6)) return false;
    if(sizes[shape][0]==1 && (slot==5 || slot==7)) return false;
    if(!thin && (shape==3 || shape==5) && !(slot&1)) return false;
    if(!thin && (shape==2 || shape==7) && (slot&1)) return false;
    return true;
}
NativeCell NativeShapeDoor(NativeCell origin,int shape,int slot,bool thin) {
    if(!NativeShapeDoorAllowed(shape,slot,thin)) return Cell(-1,-1);
    int x=0,y=0;
    switch(slot) {
    case 0:x=-1; break;
    case 1:y=-1; break;
    case 2:x=sizes[shape][0]; break;
    case 3:y=sizes[shape][1]; break;
    case 4:x=-1; y=1; break;
    case 5:x=1; y=-1; break;
    case 6:x=sizes[shape][0]; y=1; break;
    case 7:x=1; y=sizes[shape][1]; break;
    }
    // 008fbdb0's four L-shape tables include the missing-corner cell twice.
    if(shape==9 && (slot==0 || slot==1)) x=y=0;
    if(shape==10 && (slot==2 || slot==5)) { x=1; y=0; }
    if(shape==11 && (slot==3 || slot==4)) { x=0; y=1; }
    if(shape==12 && (slot==6 || slot==7)) x=y=1;
    return Cell(origin.x+x,origin.y+y);
}
NativeCell NativeShapeDoorAnchor(NativeCell origin,int shape,int slot) {
    if(!NativeShapeDoorAllowed(shape,slot)) return Cell(-1,-1);
    NativeCell p=NativeShapeDoor(origin,shape,slot);
    p.x-=directions[slot&3][0]; p.y-=directions[slot&3][1]; return p;
}
bool NativeShapeStbDoor(int shape,int slot,NativeCell* cell) {
    if(!cell || !NativeShapeDoorAllowed(shape,slot)) return false;
    NativeCell p=NativeShapeDoorAnchor(Cell(0,0),shape,slot);
    p.x=p.x*13+6; p.y=p.y*7+3;
    switch(slot&3) { case 0:p.x-=7; break; case 1:p.y-=4; break; case 2:p.x+=7; break; case 3:p.y+=4; break; }
    *cell=p; return true;
}
