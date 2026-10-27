#include "native_rng.h"
#include "native_rng_data.h"
#include <string.h>
void NativeMt::Seed(unsigned int seed) {
    words[0]=seed;for(unsigned int i=1;i<624;++i) words[i]=(words[i-1]^(words[i-1]>>30))*1812433253U+i;index=624;
}
unsigned int NativeMt::Next() {
    if(index>=624) {
        for(int i=0;i<624;++i) {
            unsigned int n=(words[i]&0x80000000U)|(words[(i+1)%624]&0x7fffffffU);
            words[i]=words[(i+397)%624]^(n>>1)^((n&1)?0x9908b0dfU:0);
        }
        index=0;
    }
    unsigned int n=words[index++];n^=n>>11;n^=(n<<7)&0x9d2c5680U;n^=(n<<15)&0xefc60000U;return n^(n>>18);
}
bool NativeMt::Valid() const {
    if(index<0 || index>624) return false;
    unsigned int any=0;for(int i=0;i<624;++i) any|=words[i];return any!=0;
}
void NativeRng::Default() { state=0xaa17414fU; index=35; }
unsigned int NativeShift(unsigned int seed,unsigned int right,unsigned int left,unsigned int finalRight) {
    // x86 masks variable shift counts to five bits.
    seed^=seed>>(right&31); seed^=seed<<(left&31); return seed^(seed>>(finalRight&31));
}
bool NativeRng::Set(unsigned int seed,unsigned int shiftIndex) {
    if(!seed || shiftIndex>=81) return false;
    state=seed; index=shiftIndex; return true;
}
bool NativeRng::Next(unsigned int* value) {
    if(!value || !state || index>=81) return false;
    const unsigned int* s=kNativeShifts[index]; state=NativeShift(state,s[0],s[1],s[2]); *value=state; return true;
}
bool NativeRng::Int(unsigned int bound,unsigned int* value) {
    unsigned int n; if(!value || !Next(&n)) return false;
    *value=bound?n%bound:0; return true; // Native still advances at bound zero.
}
float NativeRandomFloat(unsigned int value) {
    float scale; memcpy(&scale,&kNativeFloatScaleBits,4);
    // Native converts unsigned via signed double +2^32, rounds to float, then
    // MULSS by 0x2f7ffffe. Preserve both roundings, including the high-bit cases.
    volatile float integer=(float)(double)value;
    volatile float result=integer*scale; return result;
}
bool NativeRng::Float(float* value) { unsigned int n; if(!value || !Next(&n)) return false; *value=NativeRandomFloat(n); return true; }
namespace {
unsigned int Checksum(unsigned int seed) {
    unsigned int sum=0;
    while(seed) { sum=(sum+(seed&255))&255; sum=((sum<<1)|(sum>>7))&255; seed>>=5; }
    return sum;
}
}
bool NativeSeedEncode(unsigned int seed,char* text,unsigned int capacity) {
    if(!seed || !text || capacity<10) return false;
    unsigned int mixed=seed^0x0fef7ffdU,check=Checksum(seed);
    unsigned int digits[8]={mixed>>27,(mixed>>22)&31,(mixed>>17)&31,(mixed>>12)&31,(mixed>>7)&31,(mixed>>2)&31,((mixed&3)<<3)|(check>>5),check&31};
    for(int i=0;i<9;++i) text[i]=i==4?' ':kNativeSeedAlphabet[digits[i<4?i:i-1]];
    text[9]=0; return true;
}
bool NativeSeedDecode(const char* text,unsigned int* seed) {
    if(!text || !seed || strlen(text)!=9 || text[4]!=' ') return false;
    unsigned int digits[8];
    for(int i=0;i<8;++i) {
        const char* at=strchr(kNativeSeedAlphabet,text[i<4?i:i+1]);
        if(!at || !*at) return false; digits[i]=(unsigned int)(at-kNativeSeedAlphabet);
    }
    unsigned int mixed=0; for(int i=0;i<6;++i) mixed=(mixed<<5)|digits[i];
    unsigned int result=((mixed<<2)|(digits[6]>>3))^0x0fef7ffdU;
    if(!result || Checksum(result)!=(((digits[6]&7)<<5)|digits[7])) return false;
    *seed=result; return true;
}
bool NativeSeeds::SetStart(unsigned int seed) {
    if(!seed) return false;
    NativeSeeds result; result.start=seed; NativeRng rng; rng.Set(seed,27);
    for(int i=0;i<14;++i) rng.Next(&result.stage[i]);
    rng.Next(&result.playerInit); result.sequence=rng.state; *this=result; return true;
}
bool NativeSeeds::ForgetStage(int stageIndex) {
    if(stageIndex<0 || stageIndex>=14 || !stage[stageIndex]) return false;
    stage[stageIndex]=NativeShift(stage[stageIndex],5,15,17); return true;
}
bool NativeSeeds::NextSequence(unsigned int* value) {
    if(!value || !start || !sequence) return false;
    sequence=NativeShift(sequence,3,23,25); *value=sequence; return true;
}
bool NativeSeeds::Valid() const { if(!start || !sequence || !playerInit) return false; for(int i=0;i<14;++i) if(!stage[i]) return false; return true; }
bool NativeInitLevelSeeds(const NativeSeeds& seeds,int stage,int stageType,NativeLevelSeeds* output) {
    int index=stage+((stageType==4 || stageType==5)?1:0);
    if(!output || !seeds.Valid() || stage<0 || stageType<0 || stageType>5 || index>=14) return false;
    NativeLevelSeeds result;memset(&result,0,sizeof(result)); result.layout.Set(seeds.stage[index],35); result.secondary.Set(seeds.stage[index],2);
    unsigned int discard; result.layout.Next(&discard); *output=result; return true;
}
bool NativeInitRoomSeeds(unsigned int placementSeed,NativeRoomSeeds* output) {
    if(!output || !placementSeed) return false;
    NativeRng rng; rng.Set(placementSeed,12);return NativeAdvanceRoomSeeds(rng,output);
}
bool NativeAdvanceRoomSeeds(NativeRng& rng,NativeRoomSeeds* output) {
    if(!output||!rng.state||rng.index>=81)return false;
    NativeRoomSeeds result;rng.Next(&result.spawn);rng.Next(&result.award);rng.Next(&result.decoration);
    result.auxiliary=NativeShift(result.decoration,11,7,12);*output=result;return true;
}
bool NativeInitNormalLevelSeeds(const NativeSeeds& seeds,int stage,int stageType,NativeLevelSeeds* output) {
    if(!output)return false;
    NativeLevelSeeds next;if(!NativeInitLevelSeeds(seeds,stage,stageType,&next))return false;
    unsigned int ignored;if(!next.layout.Next(&ignored))return false;next.setup=next.layout;*output=next;return true;
}
bool NativeNormalChallengeSubtype(int stage) {return stage==2||stage==4||stage==6||stage==8;}
