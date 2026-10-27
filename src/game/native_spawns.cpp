#include "native_spawns.h"
#include <string.h>
namespace {
float Float(unsigned int bits) { float value; memcpy(&value,&bits,4); return value; }
bool NonnegativeFinite(unsigned int bits) { return (bits&0x7fffffffU)<0x7f800000U && (!(bits&0x80000000U) || !(bits&0x7fffffffU)); }
bool Finite(float value) { unsigned int bits; memcpy(&bits,&value,4); return (bits&0x7fffffffU)<0x7f800000U; }
}
NativeSpawnResult NativeSpawnWeight(const RoomRecordStack& stack,float* total) {
    if(!total || stack.count>255 || (stack.count && !stack.options)) return NS_BAD_REQUEST;
    if(!stack.count) return NS_EMPTY;
    float sum=0;
    for(unsigned int i=0;i<stack.count;++i) {
        RoomRecordOption entry; if(!stack.Option(i,&entry) || !NonnegativeFinite(entry.weightBits)) return NS_BAD_WEIGHT;
        volatile float rounded=sum+Float(entry.weightBits); sum=rounded;
        if(!Finite(sum)) return NS_BAD_WEIGHT;
    }
    *total=sum; return NS_OK;
}
NativeSpawnResult NativePickSpawn(const RoomRecordStack& stack,float sample,float total,unsigned int* selected,RoomRecordOption* entry) {
    if(!selected || !Finite(sample) || sample<0 || sample>1 || !Finite(total) || total<0 ||
        stack.count>255 || (stack.count && !stack.options)) return NS_BAD_REQUEST;
    if(!stack.count) return NS_EMPTY;
    // Validate the entire stack first. An unsupported identity is valid data;
    // it is never grounds for removing/replacing an option or drawing again.
    for(unsigned int i=0;i<stack.count;++i) {
        RoomRecordOption option; if(!stack.Option(i,&option) || !NonnegativeFinite(option.weightBits)) return NS_BAD_WEIGHT;
    }
    volatile float limit=sample*total; float sum=0;
    for(unsigned int i=0;i<stack.count;++i) {
        RoomRecordOption option; stack.Option(i,&option);
        volatile float rounded=sum+Float(option.weightBits); sum=rounded;
        if(limit<sum) { *selected=i; if(entry) *entry=option; return NS_OK; }
    }
    // Saved code logs a first-entry warning here; its logger/fault return path
    // is not recovered reliably. Expose failure without inventing a repick or
    // dereferencing an empty array. Valid finite ordinary samples select above.
    return NS_NO_ENTRY;
}
