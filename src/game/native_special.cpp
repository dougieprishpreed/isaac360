#include "native_topology.h"
#include <string.h>
namespace {
const int direction[4][2]={{-1,0},{0,-1},{1,0},{0,1}};
NativeCell Add(NativeCell a,int x,int y) { a.x+=x; a.y+=y; return a; }
bool Valid(const NativeTopology& g,int shape,unsigned int doors) {
    if(shape<1 || shape>12 || doors>255 || g.count<1 || g.count>169 || g.deadEndCount<0 || g.otherCount<0 || g.deadEndCount+g.otherCount>g.count) return false;
    for(int i=0;i<169;++i) if(g.occupied[i]< -1 || g.occupied[i]>=g.count) return false;
    for(int i=0;i<g.deadEndCount;++i) if(g.deadEnds[i]<0 || g.deadEnds[i]>=g.count) return false;
    for(int i=0;i<g.count;++i) {
        const NativeTopologyRoom& r=g.rooms[i]; NativeCell cells[4]; int n=NativeShapeFootprint(r.shape,cells);
        if(!n || r.index!=i || (i && (r.direction<0 || r.direction>3))) return false;
        for(int k=0;k<n;++k) {
            int at=NativeCellIndex(Add(r.origin,cells[k].x,cells[k].y)); if(at<0 || g.occupied[at]!=i) return false;
        }
        for(int d=0;d<8;++d) if(r.neighbours[d]< -1 || r.neighbours[d]>=g.count) return false;
    }
    return true;
}
void RemoveLeaf(NativeTopology& g,int position) {
    memmove(g.deadEnds+position,g.deadEnds+position+1,(g.deadEndCount-position-1)*sizeof(int)); --g.deadEndCount;
}
bool StraightRun(const NativeTopology& g,int id) {
    const NativeTopologyRoom& r=g.rooms[id]; NativeCell p=r.connection;
    for(int step=0;step<3;++step) {
        p=Add(p,direction[r.direction][0],direction[r.direction][1]); int at=NativeCellIndex(p);
        if(at<0 || (g.occupied[at]>=0 && g.occupied[at]!=id)) return false;
        for(int d=0;d<4;++d) {
            at=NativeCellIndex(Add(p,direction[d][0],direction[d][1]));
            if(at>=0 && g.occupied[at]>=0 && g.occupied[at]!=id) return false;
        }
    }
    return true;
}
}
NativeSpecialResult NativeTopology::TryResizeEndRoom(int index,int shape,unsigned int doors,int* candidate) {
    if(!Valid(*this,shape,doors) || index<=0 || index>=count) return NSP_BAD_REQUEST;
    NativeTopologyRoom& room=rooms[index]; int d=room.direction;
    int connection=NativeCellIndex(room.connection);
    if(connection<0 || occupied[connection]!=index) return NSP_BAD_REQUEST;
    bool horizontal=d==0 || d==2;
    NativeCell extended=Add(room.connection,d<2?direction[d][0]:0,d<2?direction[d][1]:0);
    NativeCell shifted=Add(extended,horizontal?0:-1,horizontal?-1:0);
    NativeCell across=Add(room.connection,horizontal?0:-1,horizontal?-1:0);
    const int ls[4][4]={{12,10,11,9},{12,11,10,9},{11,9,10,12},{10,9,11,12}};
    const int shapes[14]={1,horizontal?6:4,horizontal?4:6,horizontal?4:6,8,8,
        ls[d][0],ls[d][1],ls[d][2],ls[d][2],ls[d][3],ls[d][3],horizontal?2:3,horizontal?7:5};
    NativeCell origins[14]={room.connection,extended,room.connection,across,extended,shifted,
        extended,shifted,extended,shifted,extended,shifted,room.connection,extended};
    int opposite=(d+2)&3;
    const int required[13]={opposite,opposite,opposite+4,opposite,opposite+4,opposite,opposite+4,
        opposite,opposite+4,opposite,opposite+4,opposite,opposite};
    for(int c=0;c<14;++c) {
        if(shapes[c]!=shape) continue;
        // Saved[esp+e0+c*4],c13, aliases cookie[esp+114]. Do not replace it
        // with an assumed doorway or mask the cookie into range.
        if(c==13) return NSP_UNRESOLVED_DOOR;
        if(!(doors&(1U<<required[c]))) continue;
        NativeCell footprint[4]; int n=NativeShapeFootprint(shape,footprint); bool fits=true;
        for(int k=0;k<n;++k) {
            int at=NativeCellIndex(Add(origins[c],footprint[k].x,footprint[k].y));
            if(at<0 || (occupied[at]>=0 && occupied[at]!=index)) fits=false;
        }
        unsigned int neighbours=0;
        for(int slot=0;slot<8;++slot) {
            int at=NativeCellIndex(NativeShapeDoor(origins[c],shape,slot)); if(at<0 || occupied[at]<0 || occupied[at]==index) continue;
            // Native byte+44 exempts this doorway from the count, but not the
            // required-door test. Count door hits, including repeated owners.
            if(!rooms[occupied[at]].nativeFlag44) ++neighbours;
            if(!(doors&(1U<<slot))) fits=false;
        }
        if(!fits || neighbours>=2) continue;
        int previous[8],oldCount=0;
        for(int slot=0;slot<8;++slot) {
            int id=room.neighbours[slot]; if(id<0) continue;
            bool seen=false; for(int k=0;k<oldCount;++k) if(previous[k]==id) seen=true;
            if(!seen) previous[oldCount++]=id;
        }
        // Native neighbours are a sorted set. Only these old neighbours and
        // the resized room are refreshed; a global leaf rebuild is different.
        for(int i=0;i<oldCount;++i) for(int j=i+1;j<oldCount;++j) if(previous[j]<previous[i]) { int v=previous[i]; previous[i]=previous[j]; previous[j]=v; }
        NativeCell old[4]; int oldSize=NativeShapeFootprint(room.shape,old);
        for(int k=0;k<oldSize;++k) occupied[NativeCellIndex(Add(room.origin,old[k].x,old[k].y))]=-1;
        for(int k=0;k<n;++k) occupied[NativeCellIndex(Add(origins[c],footprint[k].x,footprint[k].y))]=index;
        room.origin=origins[c]; room.shape=shape;
        for(int k=0;k<oldCount;++k) RefreshRoom(previous[k]);
        RefreshRoom(index); if(candidate) *candidate=c; return NSP_OK;
    }
    return NSP_NO_MATCH;
}
NativeSpecialResult NativeTopology::PickEndRoom(int shape,unsigned int doors,int* index) {
    if(!index || !Valid(*this,shape,doors)) return NSP_BAD_REQUEST;
    for(int i=0;i<deadEndCount;++i) {
        int id=deadEnds[i]; NativeSpecialResult result=TryResizeEndRoom(id,shape,doors);
        if(result==NSP_OK) { RemoveLeaf(*this,i); *index=id; return NSP_OK; }
        if(result!=NSP_NO_MATCH) return result;
    }
    return NSP_NO_MATCH;
}
NativeSpecialResult NativeTopology::PrepareBossRoom(int shape,unsigned int doors) {
    for(int i=0;i<deadEndCount;++i) {
        int id=deadEnds[i]; if(rooms[id].depth<=1 || (config.loopA && !StraightRun(*this,id))) continue;
        NativeSpecialResult result=TryResizeEndRoom(id,shape,doors);
        if(result==NSP_OK) { bossPending=id; RemoveLeaf(*this,i); return NSP_OK; }
        if(result!=NSP_NO_MATCH) return result;
    }
    return NSP_NO_MATCH;
}
NativeSpecialResult NativeTopology::PickBossRoom(int shape,unsigned int doors,bool forceNew,int* index) {
    if(!index || !Valid(*this,shape,doors) || bossPending< -1 || bossPending>=count || bossPlacements<0 || !rng.state || rng.index>80) return NSP_BAD_REQUEST;
    if(!config.loopA || bossPlacements<1 || forceNew) {
        NativeSpecialResult prepared=PrepareBossRoom(shape,doors);
        if(prepared!=NSP_OK && prepared!=NSP_NO_MATCH) return prepared;
    }
    if(bossPending<0) return NSP_NO_MATCH;
    int id=bossPending; NativeSpecialResult result=TryResizeEndRoom(id,shape,doors); if(result!=NSP_OK) return result;
    ++bossPlacements; bossPending=-1;
    if(config.loopA && bossPlacements<2) {
        NativeTopologyRoom options[kNativeCandidateCapacity]; int n=Candidates(id,false,options); Shuffle(options,n);
        for(int k=0;k<n;++k) {
            const NativeTopologyRoom& next=options[k];
            if(!CanPlace(next.origin,next.shape) || CountNeighbours(next.connection)>=2 || !(doors&(1U<<next.parentSlot))) continue;
            bossPending=Place(next); rooms[bossPending].deadEnd=true; RefreshRoom(bossPending);
            rooms[id].doors|=1U<<next.parentSlot; break;
        }
    }
    *index=id; return NSP_OK;
}
NativeSpecialResult NativeTopology::ReserveRoom(int index) {
    if(index<0 || index>=count || !Valid(*this,1,0)) return NSP_BAD_REQUEST;
    NativeCell cells[4]; int n=NativeShapeFootprint(rooms[index].shape,cells);
    for(int i=0;i<n;++i) for(int k=-1;k<4;++k) {
        NativeCell p=Add(rooms[index].origin,cells[i].x,cells[i].y);
        if(k>=0) p=Add(p,direction[k][0],direction[k][1]);
        int at=NativeCellIndex(p); if(at>=0) config.reserved[at]=1;
    }
    return NSP_OK;
}
