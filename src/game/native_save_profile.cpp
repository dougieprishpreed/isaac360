#include "native_save_writer.h"
#include "native_profile_storage.h"
namespace {
void Tag(NativeSaveWriter& w,unsigned int id,unsigned int bytes,unsigned int count){w.Value(id,4);w.Value(bytes,4);w.Value(count,4);}
void Flags(NativeSaveWriter& w,unsigned int id,const bool* flags,unsigned int count,unsigned int declaredBytes){
    Tag(w,id,declaredBytes,count);for(unsigned int i=0;i<count;++i)w.Value(flags[i]?1:0,1);
}
void Words(NativeSaveWriter& w,unsigned int id,const unsigned int* words,unsigned int count){
    Tag(w,id,4*count,count);for(unsigned int i=0;i<count;++i)w.Value(words[i],4);
}
}
bool NativeSaveWriter::Profile(const NativeProfile& profile,const NativeProfileStorage& storage,unsigned int serial){
    if(!valid||!data.empty())return Fail(0x871650);
    //87165e's16 literal bytes are outside the checksum, as in90ca80.
    const char* header="ISAACNGSAVE09R  ";for(int i=0;i<16;++i)data.push_back((unsigned char)header[i]);
    Value(storage.nativeF14,4);
    Flags(*this,1,profile.awards,638,638);
    ::Words(*this,2,profile.eventCounters,496);::Words(*this,3,storage.nativeD58,14);
    //Native nominal block lengths for these boolean tables are four times
    //their counts, despite8719d0/etc actually writing one byte per element.
    Flags(*this,4,storage.collected,733,733*4);
    Flags(*this,5,storage.nativeD90,7,7*4);Flags(*this,6,storage.nativeD97,104,104*4);
    Flags(*this,7,storage.nativeDff,46,46*4);::Words(*this,8,storage.nativeE30,27);::Words(*this,9,storage.nativeE9c,2);
    Flags(*this,10,storage.nativeEa4,79,79*4);
    unsigned int count=0;for(int i=0;i<4;++i)count+=(unsigned int)storage.trees[i].size();
    Tag(*this,11,count*4,count);Value(4,4);
    const unsigned int order[]={3,1,2,0},id[]={4,2,3,1};
    for(int i=0;i<4;++i){const std::map<unsigned int,unsigned int>& tree=storage.trees[order[i]];
        Value(id[i],4);Value((unsigned int)tree.size()*4,4);
        for(std::map<unsigned int,unsigned int>::const_iterator p=tree.begin();p!=tree.end();++p){Value(p->first,4);Value(p->second,4);}
    }
    Value(serial,4);const unsigned int footer=checksum;
    //8724b8 writes the raw checksum, without the run-file's96696996 XOR.
    for(int i=0;i<4;++i)data.push_back((unsigned char)(footer>>(8*i)));
    return valid;
}
