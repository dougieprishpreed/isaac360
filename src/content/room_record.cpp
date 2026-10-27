#include "room_record.h"
namespace {
unsigned int Le(const unsigned char* p,int bytes) { unsigned int v=0; for(int i=0;i<bytes;++i) v|=(unsigned int)p[i]<<(i*8); return v; }
int Signed16(const unsigned char* p) { unsigned int n=Le(p,2); return n<32768?(int)n:(int)n-65536; }
struct Reader {
    const unsigned char* data; size_t size,pos; bool ok;
    const unsigned char* Take(size_t n) {
        if(!ok || n>size-pos) { ok=false; return 0; }
        const unsigned char* p=data+pos; pos+=n; return p;
    }
    unsigned int Read(int n) { const unsigned char* p=Take(n); return p?Le(p,n):0; }
};
}
bool RoomRecordStack::Option(unsigned int index,RoomRecordOption* out) const {
    if(!out || !options || index>=count) return false;
    const unsigned char* p=options+index*10;
    RoomRecordOption result={Le(p,2),Le(p+2,2),Le(p+4,2),Le(p+6,4)}; *out=result; return true;
}
bool RoomRecordCursor::Next(RoomRecordStack* out) {
    if(!out || !left || remaining<5 || !data) return false;
    unsigned int n=data[4]; size_t bytes=5+n*10;
    if(bytes>remaining) return false;
    RoomRecordStack result={Signed16(data),Signed16(data+2),n,data+5};
    data+=bytes; remaining-=bytes; --left; *out=result; return true;
}
RoomRecordResult RoomRecord::Open(const unsigned char* data,size_t size,RoomRecordFormat kind) {
    if(!data || (kind!=RRF_LEGACY && kind!=RRF_STB1 && kind!=RRF_STB2)) return RRR_BAD_FORMAT;
    RoomRecord result; Reader r={data,size,0,true};
    result.format=kind; result.type=r.Read(4); result.variant=r.Read(4);
    result.subtype=kind==RRF_LEGACY?0:r.Read(4); result.difficulty=r.Read(1); result.nameBytes=r.Read(2);
    result.name=r.Take(result.nameBytes); result.weightBits=r.Read(4);
    result.width=r.Read(1); result.height=r.Read(1); result.shape=kind==RRF_LEGACY?0:r.Read(1);
    result.doorCount=r.Read(1); result.stackCount=r.Read(2); result.extra=kind==RRF_STB2?r.Take(9):0;
    result.doors=r.Take(result.doorCount*5); result.stacks=data+r.pos;
    for(unsigned int i=0;r.ok && i<result.stackCount;++i) {
        r.Take(4); unsigned int options=r.Read(1); r.Take(options*10);
    }
    if(!r.ok) return RRR_TRUNCATED;
    if(r.pos!=size) return RRR_TRAILING;
    result.stackBytes=size-(size_t)(result.stacks-data); *this=result; return RRR_OK;
}
bool RoomRecord::Door(unsigned int index,RoomRecordDoor* out) const {
    if(!out || index>=doorCount) return false;
    const unsigned char* p=doors+index*5; RoomRecordDoor result={Signed16(p),Signed16(p+2),p[4]}; *out=result; return true;
}
RoomRecordCursor RoomRecord::Stacks() const { RoomRecordCursor cursor={stacks,stackBytes,stackCount}; return cursor; }
