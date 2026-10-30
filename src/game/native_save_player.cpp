#include "native_save_writer.h"
#include <algorithm>
namespace {
const std::vector<unsigned int>* WordOwner(const NativePlayerPersistent& p,unsigned int at){
    std::map<unsigned int,std::vector<unsigned int> >::const_iterator i=p.words.find(at);return i==p.words.end()?0:&i->second;
}
bool Words(NativeSaveWriter& w,const NativePlayerPersistent& p,unsigned int at,unsigned int pc,unsigned int countBytes=4){
    const std::vector<unsigned int>* owner=WordOwner(p,at);if(!owner)return w.Fail(pc,at);w.Words(*owner,countBytes);return w.valid;
}
bool Empty(NativeSaveWriter& w,const NativePlayerPersistent& p,unsigned int at,unsigned int pc,unsigned int countBytes=4){
    const std::vector<unsigned int>* owner=WordOwner(p,at);if(!owner||!owner->empty())return w.Fail(pc,at);
    w.Value((unsigned int)owner->size(),countBytes);return w.valid;
}
bool Bytes(NativeSaveWriter& w,const NativePlayerPersistent& p,unsigned int at,unsigned int count,unsigned int pc){
    std::map<unsigned int,std::vector<unsigned char> >::const_iterator i=p.bytes.find(at);
    if(i==p.bytes.end()||i->second.size()!=count)return w.Fail(pc,at);
    for(unsigned int n=0;n<count;++n)w.Value(i->second[n],1);return w.valid;
}
bool Records(NativeSaveWriter& w,const NativePersistentFields& f,unsigned int at,unsigned int count,unsigned int pc){
    for(unsigned int i=0;i<count;++i)if(!w.Field(f,at+8*i+4,1,pc)||!w.Field(f,at+8*i,4,pc))return false;
    return w.valid;
}
bool Pocket(NativeSaveWriter& w,const NativePersistentFields& f,unsigned int at){
    //913020: seven narrowed words and two flags; native struct padding is
    //never a file byte. The last four words follow the packed flag byte.
    for(unsigned int i=0;i<3;++i)if(!w.Field(f,at+4*i,2,0x913040+0x2b*i))return false;
    const unsigned int flags[]={at+12,at+13};if(!w.PackedFlags(f,flags,2,0x9130d6))return false;
    for(unsigned int i=0;i<4;++i)if(!w.Field(f,at+16+4*i,2,0x913100+0x2b*i))return false;
    return w.valid;
}
bool BoundedByte(NativeSaveWriter& w,const NativePersistentFields& f,unsigned int at,unsigned int pc){
    unsigned int n;if(!f.Get(at,4,&n)){
        //The native unused1cdc counter is narrowed only for the file. It has
        //no gameplay reader. Canonicalize its unspecified output byte without
        //changing the live/captured indeterminate counter.
        if(at!=0x438)return w.Fail(pc,at);
        for(unsigned int i=0;i<4;++i)if(!f.IsUnwritten(at+i))return w.Fail(pc,at+i);
        ++w.indeterminateBytes;w.Value(0,1);return w.valid;
    }
    int value=(int)n;w.Value((unsigned int)std::max(0,std::min(255,value)),1);return w.valid;
}
}
bool NativeSaveWriter::Player(const NativePlayerPersistent& player){
    if(!valid||!player.captured||!player.effects.empty()||!player.persistentEffects.empty())return Fail(0x916040);
    const NativePersistentFields& f=player.fields;
    if(!Fields(f,0,11,0x91605a)||!Field(f,0x2c,2,0x9161e6)||!Field(f,0x2e,4,0x916230))return false;
    const unsigned int flags[]={0x32,0x528,0x529};if(!PackedFlags(f,flags,3,0x9162a7))return false;
    if(!Field(f,0x34,6,0x9162c9)||!Fields(f,0x3c,2,0x916338)||!Fields(f,0x364,2,0x916383)||!Fields(f,0x44,16,0x9163ce))return false;
    if(!Fields(f,0x84,20,0x91663c)||!Fields(f,0xd4,6,0x916704)||!Records(*this,f,0xec,4,0x916818)||!Fields(f,0x10c,15,0x916868)||!Field(f,0x148,4,0x91688d))return false;
    if(!::Words(*this,player,0x14c,0x9168ac)||!::Words(*this,player,0x158,0x9168ba)||!::Words(*this,player,0x164,0x9168c8)||!Fields(f,0x170,2,0x9168dc))return false;
    std::map<unsigned int,std::vector<unsigned char> >::const_iterator trinkets=player.bytes.find(0x178);
    if(trinkets==player.bytes.end())return Fail(0x916907,0x178);this->Bytes(trinkets->second);
    if(!Field(f,0x1c0,4,0x916977))return false;unsigned int count;
    if(!f.Get(0x1c0,4,&count)||count||!WordOwner(player,0x1c4)||!WordOwner(player,0x1c4)->empty())return Fail(0x91699b,0x1c0);
    for(unsigned int at=0x184;at<=0x1b4;at+=12)if(!::Words(*this,player,at,0x916a17))return false;
    if(!Field(f,0x1dc,4,0x916a63)||!Field(f,0x1f4,4,0x916a8a)||!Fields(f,0x1e0,5,0x916ab9)||!Field(f,0x1f8,12,0x916ae9)||!Fields(f,0x204,5,0x916b87)||!Field(f,0x21c,4,0x916c55))return false;
    if(!f.Get(0x21c,4,&count)||count)return Fail(0x916c72,0x21c);
    if(!Empty(*this,player,0x1d0,0x916dd5)||!Fields(f,0x338,11,0x916e5a)||!Field(f,0x36c,4,0x91701a))return false;
    if(!f.Get(0x36c,4,&count)||count||!WordOwner(player,0x370)||!WordOwner(player,0x370)->empty())return Fail(0x91703d,0x36c);
    if(!Empty(*this,player,0x37c,0x91728e,1)||!Field(f,0x388,1,0x9172e1))return false;
    if(!f.Get(0x388,1,&count)||count)return Fail(0x917305,0x388);
    if(!Field(f,0x3a4,2,0x91731c)||!Field(f,0x41c,4,0x917344)||!Field(f,0x420,2,0x917372)||!Field(f,0x424,2,0x91739f)||!Fields(f,0x428,4,0x9173c5)||!Field(f,0x444,24,0x917461)||!Field(f,0x45c,4,0x91749f))return false;
    if(!BoundedByte(*this,f,0x438,0x9174ef)||!BoundedByte(*this,f,0x440,0x917541)||!BoundedByte(*this,f,0x43c,0x917593)||!Fields(f,0x460,7,0x9175b9)||!Empty(*this,player,0x50c,0x9176f4,2))return false;
    if(!Fields(f,0x47c,2,0x9177f9)||!Field(f,0x484,2,0x91784e)||!Fields(f,0x488,5,0x917874))return false;
    for(unsigned int at=0x49c;at<=0x4ac;at+=16){std::map<unsigned int,std::vector<bool> >::const_iterator b=player.bits.find(at);
        if(b==player.bits.end())return Fail(0x917938,at);Bits(b->second);}
    if(!::Bytes(*this,player,0x4bc,8,0x917961)||!Field(f,0x4c8,4,0x917994)||!::Bytes(*this,player,0x4d0,6,0x9179c4))return false;
    Value(20,1);if(!::Bytes(*this,player,0x4dc,20,0x917a29)||!Field(f,0x4e8,1,0x917a5f)||!Field(f,0x4cc,4,0x917a86)||!Fields(f,0x4ec,2,0x917aad)||!Field(f,0x51c,4,0x917aff)||!Field(f,0x520,4,0x917b26)||!Field(f,0x508,4,0x917b4d)||!Field(f,0x524,4,0x917b74))return false;
    if(!Pocket(*this,f,0x52c)||!Records(*this,f,0x54c,4,0x917bc3)||!Fields(f,0x56c,2,0x917c1c)||!Field(f,0x574,1,0x917c3b)||!Records(*this,f,0x578,4,0x917c74)||!Fields(f,0x598,2,0x917ccd)||!Fields(f,0x5ac,2,0x917cec)||!Field(f,0x518,4,0x917d3e))return false;
    if(!::Words(*this,player,0x4f4,0x917d74,1)||!Fields(f,0x500,2,0x917ddd)||!Field(f,0x5a0,1,0x917e2b))return false;
    if(!f.Get(0x5a0,1,&count))return Fail(0x917e3d,0x5a0);
    if(count!=255&&!Fields(f,0x5a4,2,0x917e57))return false;
    if(!Empty(*this,player,0x3a8,0x917ec4))return false;
    unsigned int costumeCount=0;
    for(int i=15;i>=0;--i){unsigned int n;if(!f.Get(0x3b4+4*i,4,&n))return Fail(0x918004,0x3b4+4*i);if((int)n>=0){costumeCount=i+1;break;}}
    Value(costumeCount,1);for(unsigned int i=0;i<costumeCount;++i)if(!Field(f,0x3b4+4*i,2,0x918082))return false;
    if(!::Words(*this,player,0x410,0x9180c4,1)||!Field(f,0x5b8,4,0x91814b))return false;
    if(!f.Get(0x5b8,4,&count)||count||!WordOwner(player,0x5b4)||!WordOwner(player,0x5b4)->empty())return Fail(0x91816b,0x5b8);
    return valid;
}
