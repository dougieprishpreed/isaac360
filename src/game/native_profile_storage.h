#ifndef GENERIC_NATIVE_PROFILE_STORAGE_H
#define GENERIC_NATIVE_PROFILE_STORAGE_H
#include "native_player.h"
#include <map>
#include <string>
class NativeSaveWriter;
//PersistentGameData's remaining owned data. Awards and event counters live in
//NativeProfile; these are the other86f570 Reset fields and four actual trees.
class NativeProfileStorage {
public:
    bool dirty,readOnly,nativeF1c;
    unsigned int nativeF14,nativeF18;
    unsigned int nativeD58[14],nativeE30[27],nativeE9c[2];
    bool collected[733],nativeD90[7],nativeD97[104],nativeDff[46],nativeEa4[79];
    std::map<unsigned int,unsigned int> trees[4]; //ef4,efc,f04,f0c.
    bool backupWritten,primaryWritten;
    NativeProfileStorage();
    void ResetFresh();
    bool SaveAfterNewRun(const NativeProfile&,unsigned int gameChecksum,const std::string& directory,int slot,unsigned int* next);
};
#endif
