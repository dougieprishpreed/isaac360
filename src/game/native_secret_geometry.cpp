#include "native_topology.h"
namespace {
const int cardinal[4][2]={{-1,0},{0,-1},{1,0},{0,1}};
const int ring[8][2]={{-2,0},{-1,-1},{0,-2},{1,-1},{2,0},{1,1},{0,2},{-1,1}};
NativeCell Cell(int x,int y) {NativeCell p={x,y};return p;}
bool Invalid(NativeCell p) {return p.x==-1&&p.y==-1;}
}
int NativeTopology::AddIsolated(int cell,bool distant) {
    NativeTopologyRoom node=NativeTopologyStart(cell%13,cell/13);node.nativeFlag44=true;
    int placed=Place(node);RefreshRoom(placed);
    int n=distant?8:4;
    for(int i=0;i<n;++i) {
        int dx=distant?ring[i][0]:cardinal[i][0],dy=distant?ring[i][1]:cardinal[i][1];
        int at=NativeCellIndex(Cell(node.origin.x+dx,node.origin.y+dy));
        if(at<0||occupied[at]<0)continue;
        int id=occupied[at];
        if(!distant)RefreshRoom(id);
        else {
            //008fb389..008fb424 adds potential doors facing the intervening
            //empty cell, without changing neighbour sets or refreshing lists.
            for(int slot=0;slot<8;++slot) {
                NativeCell door=NativeShapeDoor(rooms[id].origin,rooms[id].shape,slot,false);
                int p=NativeCellIndex(door);if(p<0||occupied[p]>=0)continue;
                int x=door.x-node.origin.x,y=door.y-node.origin.y;if(x<0)x=-x;if(y<0)y=-y;
                if(x+y==1)rooms[id].doors|=1U<<slot;
            }
        }
    }
    return placed;
}
int NativeTopology::PickSecretRoom(const unsigned char* blocked) {
    if(!blocked||!rng.state||rng.index>80||count<1||count>=169)return -1;
    int best=0,candidates[169],size=0;
    for(int cell=0;cell<169;++cell) {
        if(occupied[cell]>=0||blocked[cell]||config.reserved[cell])continue;
        int score=10+Draw()%5,neighbours=0,x=cell%13,y=cell/13;
        for(int d=0;d<4;++d) {
            int at=NativeCellIndex(Cell(x+cardinal[d][0],y+cardinal[d][1]));
            if(at<0||occupied[at]<0)continue;
            const NativeTopologyRoom& r=rooms[occupied[at]];
            //The native check tests the invalid sentinel, not equality with
            //the candidate. Repeated occupied cells remain separate neighbours.
            if(Invalid(NativeShapeDoor(r.origin,r.shape,(d+2)&3,false)))score-=100;
            else ++neighbours;
        }
        if(!neighbours||score<0)continue;
        if(neighbours<3)score-=3;if(neighbours<2)score-=3;
        if(score>best){best=score;size=0;}
        if(score==best)candidates[size++]=cell;
    }
    return size?AddIsolated(candidates[Draw()%size],false):-1;
}
int NativeTopology::PickDistantRoom() {
    if(!rng.state||rng.index>80||count<1||count>=169)return -1;
    int best=0,candidates[169],size=0;
    for(int cell=0;cell<169;++cell) {
        if(occupied[cell]>=0||config.reserved[cell])continue;
        int score=10+Draw()%5,neighbours=0,x=cell%13,y=cell/13;
        for(int d=0;d<4&&score>=0;++d) {
            int at=NativeCellIndex(Cell(x+cardinal[d][0],y+cardinal[d][1]));
            if(at>=0&&(occupied[at]>=0||config.reserved[at]))score-=100;
        }
        for(int d=0;d<8&&score>=0;++d) {
            int at=NativeCellIndex(Cell(x+ring[d][0],y+ring[d][1]));
            if(at<0||occupied[at]<0)continue;
            const NativeTopologyRoom& r=rooms[occupied[at]];
            for(int side=0;side<4;++side)if(cardinal[side][0]*ring[d][0]+cardinal[side][1]*ring[d][1]>0&&
                Invalid(NativeShapeDoor(r.origin,r.shape,(side+2)&3,false))){score-=100;break;}
            ++neighbours;
        }
        if(!neighbours||score<0)continue;
        if(neighbours<3)score-=3;if(neighbours<2)score-=3;
        if(score>best){best=score;size=0;}
        if(score==best)candidates[size++]=cell;
    }
    return size?AddIsolated(candidates[Draw()%size],true):-1;
}
