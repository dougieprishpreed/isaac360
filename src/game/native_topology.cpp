#include "native_topology.h"
#include <string.h>
namespace {
const int directions[4][2]={{-1,0},{0,-1},{1,0},{0,1}};
NativeCell Cell(int x,int y) { NativeCell p={x,y}; return p; }
bool Equal(NativeCell a,NativeCell b) { return a.x==b.x && a.y==b.y; }
}
NativeTopologyRoom NativeTopologyStart(int x,int y,int shape) {
    NativeTopologyRoom room; memset(&room,0,sizeof(room)); room.origin=Cell(x,y); room.shape=shape;
    room.connection=Cell(-1,-1); room.direction=room.parentSlot=room.index=-1;
    for(int i=0;i<8;++i) room.neighbours[i]=-1;
    return room;
}
unsigned int NativeTopology::Draw() { unsigned int n=0; rng.Next(&n); ++draws; return n; }
bool NativeTopology::CanPlace(NativeCell origin,int shape) const {
    NativeCell cells[4]; int n=NativeShapeFootprint(shape,cells); if(!n) return false;
    int above=count?NativeCellIndex(Cell(rooms[0].origin.x,rooms[0].origin.y-1)):-1;
    for(int i=0;i<n;++i) {
        int at=NativeCellIndex(Cell(origin.x+cells[i].x,origin.y+cells[i].y));
        if(at<0 || (config.reserveAboveStart && at==above) || occupied[at]>=0 || config.reserved[at]) return false;
    }
    return true;
}
int NativeTopology::Place(const NativeTopologyRoom& input) {
    int index=count++; rooms[index]=input; rooms[index].index=index;
    NativeCell cells[4]; int n=NativeShapeFootprint(input.shape,cells);
    for(int i=0;i<n;++i) occupied[NativeCellIndex(Cell(input.origin.x+cells[i].x,input.origin.y+cells[i].y))]=index;
    return index;
}
int NativeTopology::CountNeighbours(NativeCell cell) const {
    int at=NativeCellIndex(cell); if(at<0) return 0;
    int own=occupied[at],n=0;
    for(int d=0;d<4;++d) {
        at=NativeCellIndex(Cell(cell.x+directions[d][0],cell.y+directions[d][1]));
        if(at>=0 && occupied[at]>=0 && occupied[at]!=own) ++n;
    }
    return n; // Native counts adjacent cells, including repeated large rooms.
}
bool NativeTopology::DoorsCompatible(NativeCell origin,int shape) const {
    int w,h; if(!NativeShapeSize(shape,&w,&h)) return false;
    for(int d=0;d<8;++d) {
        NativeCell anchor=NativeShapeDoorAnchor(origin,shape,d);
        int at=NativeCellIndex(NativeShapeDoor(origin,shape,d,true));
        if(at<0 || occupied[at]<0) continue;
        if(Equal(anchor,Cell(-1,-1))) return false;
        const NativeTopologyRoom& neighbour=rooms[occupied[at]]; bool found=false;
        for(int k=0;k<8;++k) if(Equal(NativeShapeDoor(neighbour.origin,neighbour.shape,k),anchor)) { found=true; break; }
        if(!found) return false;
    }
    return true;
}
int NativeTopology::Candidates(int index,bool force,NativeTopologyRoom* out) {
    if(!out || index<0 || index>=count || !rng.state || rng.index>80) return -1;
    const NativeTopologyRoom& from=rooms[index]; int n=0;
    // 008fd270: each admissible, empty ordinary doorway consumes thirteen
    // draws BEFORE testing allowed shape bits, probability and footprint.
    const unsigned int odds[13]={48,24,24,24,24,72,72,36,36,36,36,24,24};
    for(int slot=0;slot<8;++slot) {
        NativeCell door=NativeShapeDoor(from.origin,from.shape,slot); if(NativeCellIndex(door)<0) continue;
        if(!CanPlace(door,1)) continue;
        int d=slot&3; bool horizontal=d==0 || d==2;
        NativeCell base=door;
        if(d<2) { base.x+=directions[d][0]; base.y+=directions[d][1]; }
        NativeCell shifted=Cell(base.x+(horizontal?0:-1),base.y+(horizontal?-1:0));
        NativeCell singleShift=Cell(door.x+(horizontal?0:-1),door.y+(horizontal?-1:0));
        const int lshapes[4][4]={{12,10,11,9},{12,11,10,9},{11,9,10,12},{10,9,11,12}};
        int shapes[13]={horizontal?6:4,horizontal?4:6,horizontal?4:6,8,8,
            lshapes[d][0],lshapes[d][1],lshapes[d][2],lshapes[d][2],lshapes[d][3],lshapes[d][3],horizontal?2:3,horizontal?7:5};
        NativeCell origins[13]={base,door,singleShift,base,shifted,base,shifted,base,shifted,base,shifted,door,base};
        if(config.shapes&2) {
            out[n]=NativeTopologyStart(door.x,door.y); out[n].direction=d; out[n].parentSlot=slot;
            out[n].connection=door; out[n++].depth=from.depth+1;
        }
        for(int k=0;k<13;++k) {
            unsigned int roll=Draw();
            bool eligible=roll%odds[k]==0 || !(config.shapes&2) || force;
            int shape=shapes[k];
            if(!(config.shapes&(1U<<shape)) || !eligible || !CanPlace(origins[k],shape)) continue;
            out[n]=NativeTopologyStart(origins[k].x,origins[k].y,shape);
            out[n].direction=d; out[n].parentSlot=slot; out[n].connection=door; out[n++].depth=from.depth+1;
        }
    }
    return n;
}
void NativeTopology::Shuffle(NativeTopologyRoom* items,int size) {
    for(int i=size-1;i>0;--i) { unsigned int j=Draw()%(i+1); NativeTopologyRoom temp=items[i]; items[i]=items[j]; items[j]=temp; }
}
NativeTopologyResult NativeTopology::Refresh() {
    deadEndCount=otherCount=0;
    for(int i=1;i<count;++i) if(CountNeighbours(rooms[i].connection)<2) rooms[i].deadEnd=true;
    for(int i=1;i<count;++i) {
        NativeTopologyRoom& r=rooms[i];
        if(Equal(r.connection,Cell(-1,-1))) continue;
        if(r.direction<0 || r.direction>3) return NT_INVALID_CONNECTION;
        int d=(r.direction+2)&3;
        int at=NativeCellIndex(Cell(r.connection.x+directions[d][0],r.connection.y+directions[d][1]));
        if(at<0 || occupied[at]<0) return NT_INVALID_CONNECTION;
        rooms[occupied[at]].deadEnd=false;
    }
    for(int i=0;i<count;++i) {
        RefreshRoom(i);
        if(rooms[i].deadEnd) deadEnds[deadEndCount++]=i; else others[otherCount++]=i;
    }
    return NT_OK;
}
void NativeTopology::RefreshRoom(int index) {
    NativeTopologyRoom& room=rooms[index]; room.doors=0; room.neighbourCount=0;
    for(int d=0;d<8;++d) {
        room.neighbours[d]=-1; int at=NativeCellIndex(NativeShapeDoor(room.origin,room.shape,d));
        if(at<0 || occupied[at]<0) continue;
        room.doors|=1U<<d; int id=occupied[at]; room.neighbours[d]=id;
        bool repeated=false;
        for(int k=0;k<d;++k) if(room.neighbours[k]==id) repeated=true;
        if(!repeated) ++room.neighbourCount;
    }
}
void NativeTopology::SortDeadEnds() {
    // 008fa440 only: 008fc400 refresh itself retains placement order.
    // Selection sort uses strictly greater depth, retaining the first tie.
    for(int i=0;i<deadEndCount;++i) {
        int best=i;
        for(int k=i+1;k<deadEndCount;++k) if(rooms[deadEnds[k]].depth>rooms[deadEnds[best]].depth) best=k;
        int temp=deadEnds[i]; deadEnds[i]=deadEnds[best]; deadEnds[best]=temp;
    }
}
int NativeTopology::PlacementOrder(int* indices,int capacity) const {
    int size=otherCount+deadEndCount;
    if(!indices || capacity<size || otherCount<0 || deadEndCount<0 || size>count) return -1;
    for(int i=0;i<otherCount;++i) indices[i]=others[i];
    for(int i=0;i<deadEndCount;++i) indices[otherCount+i]=deadEnds[i];
    return size;
}
int NativeTopology::AddDeadEnd() {
    if(!rng.state || rng.index>80 || count<1 || count>=kNativeTopologyCells) return -1;
    int indices[kNativeTopologyCells],size=count;
    for(int i=0;i<size;++i) indices[i]=i;
    for(int i=size-1;i>0;--i) { unsigned int j=Draw()%(i+1); int temp=indices[i]; indices[i]=indices[j]; indices[j]=temp; }
    for(int i=0;i<size;++i) {
        NativeTopologyRoom candidates[kNativeCandidateCapacity]; int n=Candidates(indices[i],true,candidates);
        for(int k=0;k<n;++k) if(candidates[k].shape==1 && CanPlace(candidates[k].origin,1) && CountNeighbours(candidates[k].connection)<2) {
            int placed=Place(candidates[k]); Refresh(); return placed;
        }
    }
    return -1;
}
int NativeTopology::GrowOne() {
    if(!rng.state || rng.index>80 || count<1 || count>=kNativeTopologyCells) return -1;
    int size=count;
    for(int i=0;i<size;++i) {
        NativeTopologyRoom candidates[kNativeCandidateCapacity]; int n=Candidates(i,true,candidates);
        for(int k=0;k<n;++k) if(CanPlace(candidates[k].origin,candidates[k].shape) && CountNeighbours(candidates[k].connection)<2) return Place(candidates[k]);
    }
    return -1; // Native helper leaves refreshing to its caller.
}
NativeTopologyResult NativeTopology::Generate(const NativeRng& stream,const NativeTopologyConfig& options,
        const NativeTopologyRoom& start,int extraRooms,int minimumDeadEnds) {
    int w,h;
    if(!stream.state || stream.index>80 || extraRooms<0 || extraRooms>=kNativeTopologyCells || minimumDeadEnds<0 ||
        minimumDeadEnds>=kNativeTopologyCells || options.shapes&~0x1fffU || !NativeShapeSize(start.shape,&w,&h)) return NT_BAD_REQUEST;
    NativeCell cells[4]; int n=NativeShapeFootprint(start.shape,cells);
    for(int i=0;i<n;++i) if(NativeCellIndex(Cell(start.origin.x+cells[i].x,start.origin.y+cells[i].y))<0) return NT_BAD_REQUEST;
    rng=stream; config=options; count=loops=deadEndCount=otherCount=repairAttempts=0; draws=0;
    bossPending=-1; bossPlacements=0;
    for(int i=0;i<kNativeTopologyCells;++i) occupied[i]=-1;
    // Native initial placement precedes occupancy/reservation tests.
    Place(start);
    int queue[kNativeTopologyCells],head=0,queued=1; queue[0]=0;
    while(queued && count-1<extraRooms) {
        int index=queue[head]; head=(head+1)%kNativeTopologyCells; --queued;
        NativeTopologyRoom candidates[kNativeCandidateCapacity]; n=Candidates(index,false,candidates); Shuffle(candidates,n);
        unsigned int limit;
        if(index==0) limit=extraRooms<16?(Draw()&1)+2:(unsigned int)n;
        else {
            limit=0; for(int k=0;k<4;++k) limit+=Draw()&1;
            if(limit>(unsigned int)n) limit=n;
        }
        unsigned int added=0;
        // The native routine checks its limit AFTER a successful placement:
        // a zero four-bit sum still permits the first valid candidate.
        for(int k=0;k<n;++k) {
            const NativeTopologyRoom& candidate=candidates[k];
            if(!CanPlace(candidate.origin,candidate.shape)) continue;
            if(CountNeighbours(candidate.connection)>=2) {
                if(!(config.loopA || config.loopB) || Draw()%10!=0) continue;
            }
            if(!DoorsCompatible(candidate.origin,candidate.shape)) continue;
            int placed=Place(candidate);
            if(Draw()%3==0) queue[(head+queued)%kNativeTopologyCells]=placed;
            else { head=(head+kNativeTopologyCells-1)%kNativeTopologyCells; queue[head]=placed; }
            ++queued; ++added;
            if(added>=limit || count-1>=extraRooms) break;
        }
        ++loops;
    }
    NativeTopologyResult refreshed=Refresh(); if(refreshed!=NT_OK) return refreshed;
    SortDeadEnds();
    if(count-1<extraRooms) return NT_EXHAUSTED;
    return deadEndCount<minimumDeadEnds?NT_TOO_FEW_DEAD_ENDS:NT_OK;
}
NativeTopologyResult NativeTopology::GenerateWrapped(const NativeRng& stream,const NativeTopologyConfig& options,
        const NativeTopologyRoom& start,int extraRooms,int minimumDeadEnds) {
    if(minimumDeadEnds<0 || minimumDeadEnds>=kNativeTopologyCells) return NT_BAD_REQUEST;
    NativeTopologyResult result=Generate(stream,options,start,extraRooms,0);
    if(result!=NT_OK && result!=NT_EXHAUSTED) return result;
    for(int i=0;i<5 && deadEndCount<minimumDeadEnds;++i) {
        GrowOne(); ++repairAttempts;
        result=Refresh(); if(result!=NT_OK) return result;
    }
    SortDeadEnds();
    return deadEndCount<minimumDeadEnds?NT_TOO_FEW_DEAD_ENDS:NT_OK;
}
