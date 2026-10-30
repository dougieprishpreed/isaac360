#include "native_save_writer.h"
namespace {
unsigned int ArithmeticRight(unsigned int n){return (n>>1)|(n&0x80000000U);}
unsigned int CrcEntry(unsigned int n){
    //649db0 uses SHR for the first bit and SAR for the other seven.
    //Do not replace this native table with a library's standard CRC32 table.
    n=(n>>1)^((n&1)?0xedb88320U:0);
    for(int i=1;i<8;++i)n=ArithmeticRight(n)^((n&1)?0xedb88320U:0);
    return n;
}
}
NativeSaveWriter::NativeSaveWriter():checksum(0xfedcba76U),nextAddress(0),missingOffset(0),indeterminateBytes(0),valid(true){}
bool NativeSaveWriter::Fail(unsigned int pc,unsigned int offset){
    if(valid){nextAddress=pc;missingOffset=offset;}valid=false;return false;
}
void NativeSaveWriter::ChecksumByte(unsigned char byte){
    unsigned int n=~checksum;n=(n>>8)^CrcEntry((n&255)^byte);checksum=~n;
}
void NativeSaveWriter::Value(unsigned int value,unsigned int bytes){
    if(!valid)return;if(bytes>4){Fail(0x649d30);return;}
    for(unsigned int i=0;i<bytes;++i){unsigned char b=(unsigned char)(value>>(i*8));data.push_back(b);ChecksumByte(b);}
}
bool NativeSaveWriter::Field(const NativePersistentFields& fields,unsigned int at,unsigned int count,unsigned int pc){
    if(!valid)return false;
    //Explicitly indeterminate native payload has no unique reference byte.
    //Encode its opaque representation canonically while retaining the source
    //UNWRITTEN mark. This never defines a gameplay field or supplies a value
    //to a branch/count query; all such queries still require Get to succeed.
    //An absent byte without that proven mark is a missing capture, not padding.
    for(unsigned int i=0;i<count;++i){unsigned int n;
        if(!fields.Get(at+i,1,&n)){
            if(!fields.IsUnwritten(at+i))return Fail(pc,at+i);
            n=0;++indeterminateBytes;
        }
        Value(n,1);
    }
    return true;
}
bool NativeSaveWriter::Fields(const NativePersistentFields& fields,unsigned int first,unsigned int count,unsigned int pc){
    return Field(fields,first,count*4,pc);
}
bool NativeSaveWriter::PackedFlags(const NativePersistentFields& fields,const unsigned int* offsets,unsigned int count,unsigned int pc){
    if(!valid||count>8)return Fail(pc);unsigned int packed=0;
    for(unsigned int i=0;i<count;++i){unsigned int n;if(!fields.Get(offsets[i],1,&n))return Fail(pc,offsets[i]);if(n)packed|=1U<<i;}
    Value(packed,1);return true;
}
void NativeSaveWriter::Words(const std::vector<unsigned int>& words,unsigned int countBytes){
    if((countBytes==1&&words.size()>255)||(countBytes==2&&words.size()>65535)){Fail(0x918e50);return;}
    Value((unsigned int)words.size(),countBytes);for(unsigned int i=0;i<words.size();++i)Value(words[i],4);
}
void NativeSaveWriter::Bytes(const std::vector<unsigned char>& bytes){
    Value((unsigned int)bytes.size(),4);for(unsigned int i=0;i<bytes.size();++i)Value(bytes[i],1);
}
void NativeSaveWriter::Bits(const std::vector<bool>& bits){
    //90bdb0: native ushort length, low bit first, partial final byte retained.
    if(bits.size()>65535){Fail(0x90bdb0);return;}Value((unsigned int)bits.size(),2);
    for(unsigned int first=0;first<bits.size();first+=8){unsigned int n=0;
        for(unsigned int bit=0;bit<8&&first+bit<bits.size();++bit)if(bits[first+bit])n|=1U<<bit;
        Value(n,1);
    }
}
