#include "native_save_reader.h"
#include "native_rng.h"
#include <stdio.h>
#include <string.h>
namespace {
unsigned int ArithmeticRight(unsigned int n){return (n>>1)|(n&0x80000000U);}
unsigned int CrcEntry(unsigned int n){
    n=(n>>1)^((n&1)?0xedb88320U:0);
    for(int i=1;i<8;++i)n=ArithmeticRight(n)^((n&1)?0xedb88320U:0);
    return n;
}
class Cursor {
public:
    const unsigned char* data;size_t size,pos;bool ok;
    Cursor(const unsigned char* d,size_t n):data(d),size(n),pos(16),ok(true){}
    bool Skip(size_t count){
        if(!ok||pos+count>size-4){ok=false;return false;}
        pos+=count;return true;
    }
    bool Read(unsigned int count,unsigned int* value){
        if(!ok||!value||count>4||pos+count>size-4){ok=false;return false;}
        unsigned int n=0;for(unsigned int i=0;i<count;++i)n|=(unsigned int)data[pos+i]<<(8*i);
        pos+=count;*value=n;return true;
    }
    bool Bytes(unsigned int count,std::vector<unsigned char>& out){
        if(!ok||pos+count>size-4){ok=false;return false;}
        out.assign(data+pos,data+pos+count);pos+=count;return true;
    }
};
bool ReadWordCounts(Cursor& c,unsigned int countBytes,std::vector<int>& out){
    unsigned int count=0;if(!c.Read(countBytes,&count))return false;
    if(count>100000)return false;
    out.assign(count,0);
    for(unsigned int i=0;i<count;++i){unsigned int value=0;if(!c.Read(4,&value))return false;out[i]=(int)value;}
    return true;
}
bool ReadWords(Cursor& c,unsigned int countBytes,std::vector<unsigned int>& out,bool retain){
    unsigned int count=0;if(!c.Read(countBytes,&count))return false;
    if(count>100000)return false;
    if(retain)out.assign(count,0);
    for(unsigned int i=0;i<count;++i){unsigned int value=0;if(!c.Read(4,&value))return false;if(retain)out[i]=value;}
    return true;
}
bool SkipWords(Cursor& c,unsigned int countBytes){
    unsigned int count=0;if(!c.Read(countBytes,&count))return false;
    if(count>100000)return false;
    return c.Skip((size_t)count*4);
}
bool ReadBits(Cursor& c){
    unsigned int count=0;if(!c.Read(2,&count)||count>65535)return false;
    return c.Skip((count+7)/8);
}
bool ReadPlayer(Cursor& c,NativeSaveSummary& out){
    unsigned int w[11];for(int i=0;i<11;++i)if(!c.Read(4,&w[i]))return false;
    out.playerId=(int)w[0];out.redHearts=(int)w[1];out.maxRedHearts=(int)w[2];
    out.soulHearts=(int)w[3];out.blackHeartMask=(int)w[4];
    out.coins=(int)w[8];out.keys=(int)w[9];out.bombs=(int)w[10];
    if(!c.Skip(2)||!c.Skip(4)||!c.Skip(1)||!c.Skip(6)||!c.Skip(8)||!c.Skip(8)||!c.Skip(64)||!c.Skip(80)||!c.Skip(24))return false;
    for(int i=0;i<4;++i)if(!c.Skip(5))return false;
    if(!c.Skip(60)||!c.Skip(4))return false;
    if(!ReadWordCounts(c,4,out.collectibles))return false;
    if(!SkipWords(c,4)||!SkipWords(c,4)||!c.Skip(8))return false;
    unsigned int trinketCount=0;if(!c.Read(4,&trinketCount)||trinketCount>100000)return false;
    if(!c.Bytes(trinketCount,out.trinkets))return false;
    if(!c.Skip(4))return false;
    for(int bank=0;bank<5;++bank)if(!SkipWords(c,4))return false;
    unsigned int magic=0,luckBits=0,extras[5];
    if(!c.Read(4,&magic)||!c.Read(4,&luckBits))return false;
    for(int i=0;i<5;++i)if(!c.Read(4,&extras[i]))return false;
    if(magic==0x424F4933U){
        out.hasExtras=true;
        memcpy(&out.luck,&luckBits,4);memcpy(&out.positionX,&extras[0],4);memcpy(&out.positionY,&extras[1],4);
        memcpy(&out.fireDelay,&extras[2],4);memcpy(&out.damage,&extras[3],4);memcpy(&out.moveSpeed,&extras[4],4);
    }
    if(!c.Skip(12)||!c.Skip(20)||!c.Skip(4))return false;
    if(!SkipWords(c,4)||!c.Skip(44)||!c.Skip(4)){return false;}
    if(!c.Skip(1)||!c.Skip(1)||!c.Skip(2)||!c.Skip(4)||!c.Skip(2)||!c.Skip(2)||!c.Skip(16)||!c.Skip(24)||!c.Skip(4))return false;
    if(!c.Skip(3)||!c.Skip(28))return false;
    
    unsigned int effectCount=0;if(!c.Read(2,&effectCount))return false;
    out.effects.clear();
    for(unsigned int i=0;i<effectCount;++i){
        NativeSaveSummary::Effect e;unsigned int value=0;
        if(!c.Read(4,&value))return false;e.type=(int)value;
        if(!c.Read(4,&value))return false;e.id=(int)value;
        if(!c.Read(4,&value))return false;e.count=(int)value;
        if(!c.Read(4,&value))return false;e.timer=(int)value;
        out.effects.push_back(e);
    }
    
    if(!c.Skip(8)){return false;}
    if(!c.Skip(2)){return false;}
    if(!c.Skip(20)){return false;}
    if(!ReadBits(c)||!ReadBits(c)){return false;}
    if(!c.Skip(8)||!c.Skip(4)||!c.Skip(6)||!c.Skip(1)||!c.Skip(20)){return false;}
    if(!c.Skip(1)||!c.Skip(4)||!c.Skip(8)||!c.Skip(4)||!c.Skip(4)||!c.Skip(4)||!c.Skip(4)){return false;}
    unsigned int pocket=0,flags=0,card=0,pill=0;
    if(!c.Read(2,&pocket)||!c.Read(2,&pocket)||!c.Read(2,&pocket)||!c.Read(1,&flags))return false;
    if(!c.Read(2,&card)||!c.Read(2,&pocket)||!c.Read(2,&pocket)||!c.Read(2,&pill))return false;
    if(!(flags&1))card=0;
    if(!(flags&2))pill=0xffffU;
    out.heldCard=(int)card;out.heldPill=(pill==0xffffU)?-1:(int)pill;
    for(int i=0;i<4;++i){unsigned int charge=0,id=0;if(!c.Read(1,&charge)||!c.Read(4,&id))return false;out.activeItems[i]=(int)id;}
    if(!c.Read(4,&w[0])||!c.Read(4,&w[1]))return false;
    out.heldTrinkets[0]=(int)w[0];out.heldTrinkets[1]=(int)w[1];
    if(!c.Skip(1))return false;
    for(int i=0;i<4;++i)if(!c.Skip(5))return false;
    if(!c.Skip(8)||!c.Skip(8)||!c.Skip(4)){return false;}
    if(!SkipWords(c,1)||!c.Skip(8)){return false;}
    unsigned int special=0;if(!c.Read(1,&special))return false;
    if(special!=255&&!c.Skip(8)){return false;}
    if(!SkipWords(c,4))return false;
    unsigned int costumes=0;if(!c.Read(1,&costumes)||costumes>16)return false;
    if(!c.Skip((size_t)costumes*2)){return false;}
    if(!SkipWords(c,1)||!c.Skip(4))return false;
    return true;
}
bool ReadRoom(Cursor& c,NativeSaveSummary& out){
    unsigned int def[3];for(int i=0;i<3;++i)if(!c.Read(4,&def[i]))return false;
    unsigned int grid=0,safe=0,list=0,dimension=0;
    if(!c.Read(2,&grid)||!c.Read(2,&safe)||!c.Read(2,&list)||!c.Read(1,&dimension))return false;
    if(!c.Skip(4)||!c.Skip(32)||!c.Skip(12)||!c.Skip(2)||!c.Skip(2)||!c.Skip(4)||!c.Skip(20))return false;
    for(int i=0;i<448;++i){
        unsigned int type=0;if(!c.Read(2,&type))return false;
        if(type&&!c.Skip(22))return false;
    }
    unsigned int entities=0;if(!c.Read(4,&entities)||entities>4096)return false;
    if(!c.Skip((size_t)entities*82))return false;
    if(!c.Skip(4)||!c.Skip(4)||!c.Skip(34))return false;
    if((int)grid==out.currentGrid&&(out.dimension<0||(int)dimension==out.dimension))
        out.roomDefinition=(int)def[0];
    return true;
}
bool ReadPools(Cursor& c,NativeSaveSummary& out){
    for(int i=0;i<31;++i){
        unsigned int count=0;if(!c.Read(4,&count)||count>100000)return false;
        out.pools.weights[i].assign(count,0.0f);
        for(unsigned int j=0;j<count;++j){unsigned int bits=0;if(!c.Read(4,&bits))return false;memcpy(&out.pools.weights[i][j],&bits,4);}
    }
    for(int i=0;i<31;++i){unsigned int value=0;if(!c.Read(4,&value))return false;out.pools.poolCounters[i]=value;}
    if(!c.Read(4,&out.pools.seed)||!c.Read(4,&out.pools.counter)||!c.Read(4,&out.pools.random)||!c.Read(4,&out.pools.backup))return false;
    unsigned int bits=0;if(!c.Read(4,&bits))return false;memcpy(&out.pools.treasureMultiplier,&bits,4);
    if(!c.Read(4,&bits))return false;memcpy(&out.pools.devilMultiplier,&bits,4);
    unsigned int seen=0;if(!c.Read(4,&seen)||seen>100000||!c.Bytes(seen,out.pools.seen))return false;
    unsigned int trinkets=0;if(!c.Read(4,&trinkets)||trinkets>100000||!c.Bytes(trinkets,out.pools.trinketEnabled))return false;
    for(int i=0;i<15;++i){unsigned int value=0;if(!c.Read(4,&value))return false;out.pools.pillEffects[i]=(int)value;}
    for(int i=0;i<15;++i){unsigned int value=0;if(!c.Read(1,&value))return false;out.pools.pillIdentified[i]=value!=0;}
    for(int i=0;i<62;++i){unsigned int value=0;if(!c.Read(1,&value))return false;out.pools.pocketFlags[i]=(unsigned char)value;}
    out.hasPools=true;return true;
}
}
NativeSaveSummary::NativeSaveSummary():valid(false),hasPlayer(false),hasPools(false),seed(0),playerId(-1),stage(0),stageType(0),currentGrid(-1),dimension(-1),roomDefinition(-1),
    redHearts(0),maxRedHearts(0),soulHearts(0),blackHeartMask(0),keys(0),bombs(0),coins(0),heldCard(0),heldPill(-1),hasExtras(false),
    positionX(0.0f),positionY(0.0f),fireDelay(0.0f),damage(0.0f),moveSpeed(0.0f),luck(0.0f){
    activeItems[0]=activeItems[1]=activeItems[2]=activeItems[3]=0;heldTrinkets[0]=heldTrinkets[1]=0;
}
bool NativeReadSaveSummary(const char* path,NativeSaveSummary& output){
    output=NativeSaveSummary();
    if(!path)return false;
    FILE* file=fopen(path,"rb");if(!file)return false;
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return false;}
    long length=ftell(file);if(length<64||fseek(file,0,SEEK_SET)!=0){fclose(file);return false;}
    std::vector<unsigned char> bytes((unsigned int)length);
    size_t got=fread(&bytes[0],1,(size_t)length,file);fclose(file);
    if(got!=(size_t)length||memcmp(&bytes[0],"ISAACNG_GSR0142",15))return false;
    unsigned int checksum=0xfedcba76U;
    for(long i=16;i<length-4;++i){unsigned int n=~checksum;n=(n>>8)^CrcEntry((n&255)^bytes[i]);checksum=~n;}
    unsigned int stored=(unsigned int)bytes[length-4]|((unsigned int)bytes[length-3]<<8)|((unsigned int)bytes[length-2]<<16)|((unsigned int)bytes[length-1]<<24);
    if(stored!=(checksum^0x96696996U))return false;
    Cursor c(&bytes[0],(size_t)length);
    unsigned int value=0;
    if(!c.Read(4,&value)||!c.Skip(1+1+1+4))return false;
    if(!c.Read(2,&value)||value>49||!c.Skip(value))return false;
    if(!c.Skip(56+4+4+1+4+24))return false;
    for(int i=0;i<30;++i)if(!c.Skip(8))return false;
    if(!c.Skip(8))return false;
    unsigned int game[8];for(int i=0;i<8;++i)if(!c.Read(4,&game[i]))return false;
    output.stage=(int)game[0];output.stageType=(int)game[1];output.currentGrid=(int)game[2];
    if(!c.Skip(4))return false;
    if(!c.Read(2,&value)||value>24||!c.Skip(value))return false;
    if(!c.Skip(4+48+12+56))return false;
    unsigned int configs=0;if(!c.Read(4,&configs)||configs>100000||!c.Skip((size_t)configs*4))return false;
    unsigned int rooms=0;if(!c.Read(4,&rooms)||rooms>507)return false;
    for(unsigned int i=0;i<rooms+18;++i)if(!ReadRoom(c,output))return false;
    unsigned int owners=0;if(!c.Read(2,&owners)||!c.Skip((size_t)owners*2))return false;
    static const unsigned int sizes[]={4,1,1,4,4,4,4,4,4,4,1,1,4,4,4,4,4,1,1,4,4,1};
    if(!c.Skip(sizes[0])||!c.Read(1,&value))return false;
    output.dimension=(int)value;
    for(int i=2;i<22;++i)if(!c.Skip(sizes[i]))return false;
    unsigned int players=0;if(!c.Read(1,&players)||players!=1)return false;
    if(!ReadPlayer(c,output)){return false;}
    output.hasPlayer=true;
    if(!c.Skip(5)){return false;}
    if(!ReadPools(c,output)){return false;}
    unsigned char seedText[10]={0};
    for(int i=0;i<9;++i){unsigned int ch=0;if(!c.Read(1,&ch))return false;seedText[i]=(unsigned char)ch;}
    unsigned int effectBytes=0;if(!c.Read(1,&effectBytes)||effectBytes>64||!c.Skip(effectBytes))return false;
    if(!NativeSeedDecode((const char*)seedText,&output.seed))return false;
    output.valid=true;return true;
}
