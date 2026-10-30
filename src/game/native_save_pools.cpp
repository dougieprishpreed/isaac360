#include "native_save_writer.h"
#include <string.h>
namespace {unsigned int Bits(float f){unsigned int n;memcpy(&n,&f,4);return n;}}
bool NativeSaveWriter::Pools(const NativeItemPoolSnapshot& pools){
    //9119d0, with6ecf40's addressed live-to-persistent transfer. Definitions
    //and eligibility are reconstructed, while weights/counters are written.
    for(unsigned int i=0;i<31;++i){Value((unsigned int)pools.weights[i].size(),4);
        for(unsigned int j=0;j<pools.weights[i].size();++j)Value(::Bits(pools.weights[i][j]),4);}
    for(int i=0;i<31;++i)Value(pools.poolCounters[i],4);
    Value(pools.seed,4);Value(pools.counter,4);Value(pools.random,4);Value(pools.backup,4);
    Value(::Bits(pools.treasureMultiplier),4);Value(::Bits(pools.devilMultiplier),4);
    //These bit-vector owners are expanded to one bool byte per bit in this
    //particular native serializer.90bdb0's packed encoding is a different call.
    Value((unsigned int)pools.seen.size(),4);for(unsigned int i=0;i<pools.seen.size();++i)Value(pools.seen[i]?1:0,1);
    Value((unsigned int)pools.trinketEnabled.size(),4);for(unsigned int i=0;i<pools.trinketEnabled.size();++i)Value(pools.trinketEnabled[i]?1:0,1);
    for(int i=0;i<15;++i)Value(pools.pillEffects[i],4);
    for(int i=0;i<15;++i)Value(pools.pillIdentified[i]?1:0,1);
    for(int i=0;i<62;++i)Value(pools.pocketFlags[i],1);
    return valid;
}
