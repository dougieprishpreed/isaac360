#ifndef GENERIC_NATIVE_ROOMS_H
#define GENERIC_NATIVE_ROOMS_H
static const int kNativeRoomCapacity=32768;
enum NativeCatalog { NC_NORMAL=1 };
static const int kNormalDefinitionBase=0x10000000,kNormalSourceBase=0x10000;
struct NativeRoomDef {
    int source,stage,ordinal,type; unsigned int variant,subtype; int difficulty,shape;
    unsigned int doors; int width,height; float weight; unsigned int offset,bytes;
};
struct NativeStageDef { int id,source; };
struct NativeRoomSourceDef {const char* request;const char* resource;int package;unsigned char sha256[32];};
struct NativeRoomQuery {
    int stage,type,shape; unsigned int minVariant,maxVariant;
    int minDifficulty,maxDifficulty; unsigned int requiredDoors; int subtype;
};
enum NativeRoomResult { NR_OK,NR_BAD_REQUEST,NR_NO_MATCH,NR_UNKNOWN_DOORS };
struct NativeRoomPool {
    float weights[kNativeRoomCapacity];
    int catalog;
    void Reset(int catalog=NC_NORMAL);
    float Weight(int definition) const;
    // Ordinary pool only: no challenge-stage merge, alternate-content flags,
    // native loader hooks or mod callbacks. Unsupported gameplay is NOT filtered.
    NativeRoomResult SelectOrdinary(unsigned int seed,const NativeRoomQuery& query,bool deplete,int* selected,int* draws=0);
};
int NativeRoomCount(int catalog=NC_NORMAL);
int NativeRoomId(int catalog,int ordinal);
int NativeRoomCatalog(int definition);
int NativeRoomPoolIndex(int definition);
const NativeRoomDef* NativeRoom(int index);
const NativeRoomDef* NativeFindRoom(const char* resource,int ordinal,int catalog=NC_NORMAL);
const char* NativeRoomSource(int source);
const NativeRoomSourceDef* NativeRoomSourceBinding(int source);
unsigned int NativeCatalogFingerprint(int catalog);
int NativeStageSource(int stage,int catalog=NC_NORMAL);
//007d4960 ordinary loaded-pool lookup: first type/variant match, no RNG or
// depletion. Prepared order is retained; native loader order remains a gate.
NativeRoomResult NativeFindRoomVariant(int stage,int type,unsigned int variant,int* selected,int catalog=NC_NORMAL);
// Native007d5180: stage/type -> room-config stage. selectionMode=-1 uses
// nativeMode2/3, 0 forces ordinary, 1 forces alternate. No unlock/seed draws.
bool NativeRoomConfigStage(int stage,int stageType,int selectionMode,int nativeMode,int* output);
// Metadata filter007d5000, with no sampling/depletion or gameplay filtering.
NativeRoomResult NativeRoomHistogram(const NativeRoomQuery& query,int* count,int shapes[13],int catalog=NC_NORMAL);
#endif
