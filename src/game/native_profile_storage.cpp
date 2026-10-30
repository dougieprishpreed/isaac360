#include "native_profile_storage.h"
#include "native_save_writer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace {
bool Publish(const std::string& path,const NativeSaveWriter& writer){
    FILE* f=fopen(path.c_str(),"wb");if(!f)return false;
    const bool wrote=fwrite(&writer.data[0],1,writer.data.size(),f)==writer.data.size();
    const bool closed=fclose(f)==0;return wrote&&closed;
}
}
NativeProfileStorage::NativeProfileStorage(){ResetFresh();}
void NativeProfileStorage::ResetFresh(){
    //86f330 constructs the trees then86f45f calls86f570(true). The
    //logger's fallthrough86f71b..71f sets ReadOnly=false and f1c=true.
    dirty=readOnly=false;nativeF1c=true;nativeF14=nativeF18=0;
    memset(nativeD58,0,sizeof(nativeD58));memset(nativeE30,0,sizeof(nativeE30));memset(nativeE9c,0,sizeof(nativeE9c));
    memset(collected,0,sizeof(collected));memset(nativeD90,0,sizeof(nativeD90));memset(nativeD97,0,sizeof(nativeD97));
    memset(nativeDff,0,sizeof(nativeDff));memset(nativeEa4,0,sizeof(nativeEa4));
    for(int i=0;i<4;++i)trees[i].clear();backupWritten=primaryWritten=false;
}
bool NativeProfileStorage::SaveAfterNewRun(const NativeProfile& profile,unsigned int gameChecksum,const std::string& directory,int slot,unsigned int* next){
    if(next)*next=0x8a1745;
    if(directory.empty()||slot<0||slot>2)return false;
    //Main20a74 = GameState(f2c)+1fb48, the actual90ca80 checksum.
    dirty=true;nativeF14=gameChecksum;
    if(!nativeF1c){if(next)*next=0;return true;}
    dirty=false; //8a1765, before872640.
    char name[64];sprintf(name,"persistentgamedata%d.dat",slot+1);
    const time_t now=time(0);const struct tm* local=localtime(&now);
    if(!local){if(next)*next=0x87265b;return false;}
    char dated[96];sprintf(dated,"%04d%02d%02d.%s",local->tm_year+1900,local->tm_mon+1,local->tm_mday,name);
    NativeSaveWriter backup;
    if(!backup.Profile(profile,*this,nativeF18+1)){if(next)*next=backup.nextAddress;return false;}
    backupWritten=Publish(directory+dated,backup);
    if(!backupWritten){if(next)*next=0x8726e5;return false;}
    ++nativeF18;
    //The offline adapter has no Steam interface:8a179a ->8727c0.
    NativeSaveWriter primary;
    if(!primary.Profile(profile,*this,nativeF18+1)){if(next)*next=primary.nextAddress;return false;}
    primaryWritten=Publish(directory+name,primary);
    if(!primaryWritten){if(next)*next=0x87280b;return false;}
    ++nativeF18;if(next)*next=0;return true;
}
