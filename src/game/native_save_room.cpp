#include "native_save_writer.h"
bool NativeSaveWriter::Entity(const NativePersistentFields& f){
    //90c0c0 common record, including the deliberately noncontiguous order.
    //Only the initial no-extra-payload/no-child branch is captured here.
    unsigned int extra,child;
    if(!f.Get(0x54,4,&extra)||!f.Get(0x5c,4,&child)||extra||child)return Fail(0x90c0c0);
    if(!Fields(f,0,6,0x90c0de)||!Field(f,0x50,1,0x90c1ab)||!Fields(f,0x18,2,0x90c1ce)||!Field(f,0x22,1,0x90c212)||!Fields(f,0x24,8,0x90c235)||!Field(f,0x23,1,0x90c34d)||!Fields(f,0x44,2,0x90c370)||!Field(f,0x20,2,0x90c3bc)||!Field(f,0x4c,4,0x90c3df))return false;
    Value(0,1); //Both actual nullable owned records above are absent.
    return valid;
}
bool NativeSaveWriter::Room(const NativeDescriptorPersistent& descriptor){
    const NativePersistentFields& f=descriptor.fields;const NativeRoomPersistent& room=descriptor.room;
    //90b3ff..43c initializes every three-word config identity to0.6fc0c0
    //overwrites it for an actual original definition; missing definitions
    //retain that native Reset record rather than a host pointer.
    for(int i=0;i<3;++i)Value(descriptor.definition[i],4);
    if(!Field(f,0,2,0x90c49a)||!Field(f,4,2,0x90c4c5)||!Field(f,8,2,0x90c4f0)||!Field(f,12,1,0x90c51b)||!Field(f,0x18,4,0x90c545)||!Field(f,0x1c,32,0x90c568)||!Fields(f,0x3c,3,0x90c585)||!Field(f,0x4a,2,0x90c5f4)||!Field(f,0x48,2,0x90c61a)||!Field(f,0x4c,4,0x90c640)||!Fields(f,0x54,5,0x90c665))return false;
    if(room.defaultGridCount!=448||(room.captured&&room.grids.size()!=448))return Fail(0x90c730,0x68);
    for(unsigned int i=0;i<448;++i){
        if(!room.captured){Value(0,2);continue;} //6aedb0's actual default type0.
        const NativePersistentFields& grid=room.grids[i];unsigned int type;
        if(!grid.Get(0,4,&type))return Fail(0x90c730,0x68);
        Value(type,2);
        if(type&&(!Fields(grid,4,2,0x90c75c)||!Field(grid,12,2,0x90c7a7)||!Field(grid,16,4,0x90c7cb)||!Field(grid,28,4,0x90c7ed)||!Field(grid,20,4,0x90c80f)))return false;
    }
    Value((unsigned int)room.entities.size(),4);
    for(unsigned int i=0;i<room.entities.size();++i)if(!Entity(room.entities[i]))return false;
    //The84 tree and88 entity vector are distinct constructor-owned lists.
    //Initial capture admits no nonempty secondary history. Their source
    //counts must be addressed even when there are no emitted records.
    if(!Field(f,0x84,4,0x90c8c3))return false;unsigned int count;
    if(!f.Get(0x84,4,&count)||count)return Fail(0x90c8f4,0x84);
    if(!f.Get(0x88,4,&count)||count)return Fail(0x90c94b,0x88);
    unsigned int end;if(!f.Get(0x8c,4,&end)||end!=count)return Fail(0x90c94b,0x8c);
    Value(0,4);
    return Field(f,0xae,2,0x90c9a0)&&Field(f,0xac,2,0x90c9c8)&&Field(f,0x94,16,0x90c9f0)&&Field(f,0xa4,8,0x90ca0f)&&Field(f,0xb4,4,0x90ca2e)&&Field(f,0x52,2,0x90ca52);
}
