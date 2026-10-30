#include "native_nightmares.h"
#include "native_nightmare_data.h"
#include <algorithm>
const char* NativePreparedNightmareFilename(unsigned int ordinal){
    return ordinal<sizeof(kNativeNightmares)/sizeof(kNativeNightmares[0])?kNativeNightmares[ordinal]:0;
}
bool NativeNightmares::ResetForNewRun(NativeMt& global){
    if(!global.Valid())return false;
    //8a1d58..6e writes14 words from b4a6f0, independently of shuffle size.
    for(int i=0;i<14;++i)stageBackdrops[i]=kNativeNightmareInitialBackdrop;
    //86adc0 rebuilds0..N-1 on each call, rather than reshuffling old order.
    order.clear();for(unsigned int i=0;i<sizeof(kNativeNightmares)/sizeof(kNativeNightmares[0]);++i)order.push_back(i);
    for(unsigned int n=(unsigned int)order.size();n>1;--n)std::swap(order[n-1],order[global.Next()%n]);
    reset=true;return true;
}
bool NativeNightmares::SetInitialStage(int stageType,int nativeMode){
    if(!reset||stageType<0||stageType>2||nativeMode<0||nativeMode>1)return false;
    //6fb7f9 ->86d780: ordinary stage1 takes86d7ec..7fe, then86d88f.
    stageBackdrops[1]=(unsigned int)stageType;return true;
}
void NativeNightmares::Swap(NativeNightmares& other){
    //Unread storage in an unreset owner has no inferred zero value.
    if(reset&&other.reset)for(int i=0;i<14;++i)std::swap(stageBackdrops[i],other.stageBackdrops[i]);
    else if(reset)for(int i=0;i<14;++i)other.stageBackdrops[i]=stageBackdrops[i];
    else if(other.reset)for(int i=0;i<14;++i)stageBackdrops[i]=other.stageBackdrops[i];
    std::swap(reset,other.reset);order.swap(other.order);
}
