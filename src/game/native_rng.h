#ifndef GENERIC_NATIVE_RNG_H
#define GENERIC_NATIVE_RNG_H
// Saved x86 behavior translated with explicit 32-bit state and float rounding.
// No global stream: each owner carries its own state and shift index.
struct NativeRng {
    unsigned int state,index;
    void Default();
    bool Set(unsigned int seed,unsigned int shiftIndex);
    bool Next(unsigned int* value);
    bool Int(unsigned int bound,unsigned int* value);
    bool Float(float* value);
};
//006ae7e0/006ae820 global MT arithmetic. Owner/bootstrap are caller supplied.
struct NativeMt {
    unsigned int words[624];int index;
    void Seed(unsigned int seed);
    void ColdStart(); //Original static index625, lazy default seed5489.
    unsigned int Next();
    bool Valid() const;
};
unsigned int NativeShift(unsigned int seed,unsigned int right,unsigned int left,unsigned int finalRight);
float NativeRandomFloat(unsigned int value);
bool NativeSeedEncode(unsigned int seed,char* text,unsigned int capacity);
bool NativeSeedDecode(const char* text,unsigned int* seed);
struct NativeSeeds {
    unsigned int start,sequence,stage[14],playerInit;
    bool seeded,effects[79]; //925cae's flag and constructor-empty effect tree.
    NativeSeeds();
    bool SetStart(unsigned int seed);
    bool ForgetStage(int stageIndex);
    bool NextSequence(unsigned int* value);
    bool Valid() const;
};
struct NativeLevelSeeds {
    NativeRng layout,secondary;
    NativeRng setup; //Normal006fa249 local curse-evaluation clone, passed to006f6340.
};
bool NativeInitLevelSeeds(const NativeSeeds& seeds,int stage,int stageType,NativeLevelSeeds* output);
//006f9d20 pre-log prefix above consumes one layout draw. The NORMAL caller
//continues at006fa027 with a SECOND layout draw before curse evaluation/floor
//generation, then clones layout for curse evaluation. The caller must retain
//that clone's evaluated state. secondary is the independent1/5/19 LEVEL owner,
//not the floor-size owner.
bool NativeInitNormalLevelSeeds(const NativeSeeds& seeds,int stage,int stageType,NativeLevelSeeds* output);
bool NativeNormalChallengeSubtype(int stage); //006fa078..0094, not XL/difficulty.
struct NativeRoomSeeds { unsigned int spawn,award,decoration,auxiliary; };
bool NativeInitRoomSeeds(unsigned int placementSeed,NativeRoomSeeds* output);
//006ee510 accepts an existing owner. Ordinary placement supplies index12;
//off-grid descriptors use the layout owner or an explicit local clone.
bool NativeAdvanceRoomSeeds(NativeRng& owner,NativeRoomSeeds* output);
#endif
