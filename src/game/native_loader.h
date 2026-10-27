#ifndef GENERIC_NATIVE_LOADER_H
#define GENERIC_NATIVE_LOADER_H
#include "native_rooms.h"
//007d2c40/007d3060: ordinary STB1 loaded-pool order. The selected prepared
// source/mount binding remains explicit; this does not emulate the native VFS.
struct NativeLoadedStageView {
    int stage,first,count,catalog;
    int Definition(int ordinal) const;
    const NativeRoomDef* At(int ordinal) const;
};
NativeRoomResult NativeOpenLoadedStage(int stage,NativeLoadedStageView* output,int catalog=NC_NORMAL);
unsigned int NativeLoadedRoomFlags(int stage,int type,unsigned int variant,unsigned int subtype);
#endif
