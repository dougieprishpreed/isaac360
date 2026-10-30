#include "native_save_writer.h"
namespace {
bool Scalar(NativeSaveWriter& w,const NativePersistentFields& f,unsigned int offset,unsigned int bytes,unsigned int pc){return w.Field(f,offset,bytes,pc);}
bool EmptyWords(NativeSaveWriter& w,const std::vector<unsigned int>& v,unsigned int bytes,unsigned int pc){
    if(!v.empty())return w.Fail(pc);w.Words(v,bytes);return w.valid;
}
}
bool NativeSaveWriter::InitialGame(NativeGamePersistent& game){
    if(!valid||!data.empty()||!game.captured||!game.seeds.Valid()||game.descriptors.size()!=525||game.players.size()!=1||!game.effects.empty())return Fail(0x90ca80);
    const NativePersistentFields& f=game.fields;
    const char header[16]="ISAACNG_GSR0142";for(int i=0;i<16;++i)data.push_back((unsigned char)header[i]); //Excluded from checksum.
    if(!Field(f,0,4,0x90caeb)||!Field(f,4,1,0x90cb13)||!Field(f,0x1e4c8,1,0x90cb52)||!Field(f,5,1,0x90cb7f)||!Field(f,8,4,0x90cbae)||!Field(f,12,2,0x90cbdc))return false;
    unsigned int flags;if(!f.Get(12,2,&flags)||flags>49)return Fail(0x90cc0d,12);
    if(!Field(f,14,flags,0x90cc0d)||!Fields(f,0x40,14,0x90cc2c)||!Field(f,0x190,4,0x90ceb3)||!Field(f,0x194,4,0x90cee4)||!Field(f,0x78,1,0x90cf12)||!Field(f,0x7c,4,0x90cf41))return false;
    //918380: six totals, two interleaved30-word arrays, then their two tails.
    if(!Fields(f,0x80,6,0x91839a))return false;
    for(int i=0;i<30;++i)if(!Field(f,0x98+4*i,4,0x918482)||!Field(f,0x114+4*i,4,0x9184a1))return false;
    if(!Field(f,0x110,4,0x9184d2)||!Field(f,0x18c,4,0x9184fa))return false;
    for(unsigned int i=0;i<8;++i)if(!Field(f,0x198+4*i,4,0x90cf7e+0x31*i))return false;
    for(unsigned int i=0;i<4;++i)if(!Field(f,0x1b8+i,1,0x90d10a+0x32*i))return false;
    if(!Field(f,0x1bc,2,0x90d1d2))return false;unsigned int levelFlags;
    if(!f.Get(0x1bc,2,&levelFlags)||levelFlags>24)return Fail(0x90d206,0x1bc);
    if(!Field(f,0x1be,levelFlags,0x90d206)||!Field(f,0x1d8,4,0x90d22a)||!Fields(f,0x1dc,12,0x90d25a)||!Fields(f,0x20c,3,0x90d27c)||!Fields(f,0x218,14,0x90d319))return false;
    //The fresh normal generator uses immutable config definitions, with no
    //cloned custom room configurations in18188's constructor-empty ring.
    //Dynamic config capture is required before any nonempty owner is accepted.
    if(!game.levelConfigurations.empty())return Fail(0x90d3b2);
    Value((unsigned int)game.levelConfigurations.size(),4);
    unsigned int roomCount;if(!f.Get(0x250,4,&roomCount)||roomCount>507)return Fail(0x90d3d8,0x250);Value(roomCount,4);
    for(unsigned int i=0;i<roomCount;++i)if(!Room(game.descriptors[i]))return false;
    for(unsigned int i=507;i<525;++i)if(!Room(game.descriptors[i]))return false;
    //90d484: exactly507 dimension/cell owners, run-length encoded as words.
    std::vector<unsigned int> encoded;unsigned int empty=0;
    for(unsigned int i=0;i<507;++i){int owner=game.owners[i/169][i%169];
        if(owner<0)++empty;
        else {if(empty){encoded.push_back((empty-1)|0x8000U);empty=0;}encoded.push_back((unsigned short)owner);}}
    if(empty)encoded.push_back((empty-1)|0x8000U);
    Value((unsigned int)encoded.size(),2);for(unsigned int i=0;i<encoded.size();++i)Value(encoded[i],2);
    static const unsigned int at[]={0x19c48,0x19c50,0x19c51,0x19c4c,0x19c54,0x19c58,0x19c5c,0x19c60,0x19c64,0x19c6c,0x19c68,0x19c70,0x19c74,0x19c78,0x19c7c,0x19c84,0x19c88,0x19c80,0x19c8c,0x19c90,0x19c94,0x19c98};
    static const unsigned int size[]={4,1,1,4,4,4,4,4,4,4,1,1,4,4,4,4,4,1,1,4,4,1};
    static const unsigned int pc[]={0x90d573,0x90d5a3,0x90d5d4,0x90d605,0x90d635,0x90d669,0x90d69d,0x90d6cd,0x90d6fd,0x90d72d,0x90d75d,0x90d78e,0x90d7bf,0x90d7ef,0x90d81f,0x90d84f,0x90d87f,0x90d8af,0x90d8e0,0x90d911,0x90d941,0x90d971};
    for(unsigned int i=0;i<sizeof(at)/sizeof(at[0]);++i)if(!Scalar(*this,f,at[i],size[i],pc[i]))return false;
    Value((unsigned int)game.players.size(),1);for(unsigned int i=0;i<game.players.size();++i)if(!Player(game.players[i]))return false;
    //90b4ec constructs all four secondary records with playerType=-1. The
    //fresh sole-player capture has introduced no joined/secondary player.
    Value(4,1);for(int i=0;i<4;++i)Value(0,1);
    if(!Pools(game.pools))return false;
    char seed[10];if(!NativeSeedEncode(game.seeds.start,seed,sizeof(seed)))return Fail(0x927ef0);
    for(int i=0;i<9;++i)Value((unsigned char)seed[i],1);
    Value(10,1);for(unsigned int first=0;first<79;first+=8){unsigned int bits=0;
        for(unsigned int bit=0;bit<8&&first+bit<79;++bit)if(game.seeds.effects[first+bit])bits|=1U<<bit;
        Value(bits,1);}
    if(!Fields(f,0x1e524,3,0x90dc62)||!EmptyWords(*this,game.auxiliary,4,0x90dd07))return false;
    std::vector<bool> bossBits(104);for(unsigned int i=0;i<104;++i)bossBits[i]=(game.bosses.persistent[i/32]&(1U<<(i%32)))!=0;Bits(bossBits);
    Value(37,2);for(int i=0;i<37;++i)Value(game.bosses.ownerSeeds[i],4);
    if(!Field(f,0x1e530,4,0x90de8d))return false;
    Value((unsigned int)game.effects.size(),1);
    if(!Field(f,0x1e54c,2,0x90e077)||!Field(f,0x1e550,2,0x90e0b3)||!Field(f,0x1e554,1,0x90e0e2))return false;
    for(int i=0;i<7;++i){const NativePersistentFields& d=game.dormant[i].fields;unsigned int firstCount,secondCount;
        if(!d.Get(0x310,1,&firstCount)||!d.Get(0x314,1,&secondCount)||firstCount||secondCount)return Fail(0x90e114,0x310);
        if(!Field(d,0x318,1,0x90e12d)||!Field(d,0x310,1,0x90e166)||!Field(d,0x314,1,0x90e1f1)||!Field(d,0x31c,1,0x90e261))return false;
    }
    if(!Field(f,0x1fb38,2,0x90e2a4))return false;
    //6b731b leaves the fresh extra ID vector constructor-empty.
    if(!EmptyWords(*this,game.extraConfigurations,4,0x90e2cf))return false;
    if(!valid)return false;game.fields.Put(0x1fb48,checksum);
    const unsigned int trailer=checksum^0x96696996U;for(int i=0;i<4;++i)data.push_back((unsigned char)(trailer>>(8*i)));
    return valid;
}
