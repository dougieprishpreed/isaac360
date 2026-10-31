#include "native_persistence.h"
#include "native_player_persistence_data.h"
#include <string.h>
void NativePersistentFields::Put(unsigned int at,unsigned int value,unsigned int count){
    for(unsigned int i=0;i<count&&i<4;++i){bytes[at+i]=(unsigned char)(value>>(8*i));unwritten.erase(at+i);}
}
void NativePersistentFields::Unwritten(unsigned int at,unsigned int count){
    for(unsigned int i=0;i<count;++i){bytes.erase(at+i);unwritten.insert(at+i);}
}
bool NativePersistentFields::Get(unsigned int at,unsigned int count,unsigned int* value) const{
    if(!value||count>4)return false;unsigned int n=0;
    for(unsigned int i=0;i<count;++i){std::map<unsigned int,unsigned char>::const_iterator p=bytes.find(at+i);
        if(p==bytes.end())return false;n|=(unsigned int)p->second<<(8*i);}
    *value=n;return true;
}
bool NativePersistentFields::IsUnwritten(unsigned int at) const{return unwritten.find(at)!=unwritten.end();}
namespace {
bool Empty(const std::vector<int>& v){for(unsigned int i=0;i<v.size();++i)if(v[i])return false;return true;}
bool Empty(const std::vector<unsigned char>& v){for(unsigned int i=0;i<v.size();++i)if(v[i])return false;return true;}
void Bind(NativePersistentFields& source,unsigned int at,int value){source.Put(at,(unsigned int)value);}
bool NativeUnwrittenPlayerByte(unsigned int at){
    //707b40/709330 never assign these on ordinary player0.1839..3b and
    //1f26..27 are padding copied by wider native loads;1f84..1fab is the
    //inactive ghost payload (1f80=false).1cdc has no gameplay read: its
    //only saved-listing references in the player domain are760565 restore
    //and762181 capture. Preserve its indeterminate state, not a made-up0.
    //16e4 is only set by711da0/711de0; fresh Init clears16dc/e0
    //directly.77c558 can read its flags only when16dc is non-null.
    return at==0x16e4||at==0x1cdc||at==0x1cdd||at==0x1cde||at==0x1cdf||
        (at>=0x1839&&at<0x183c)||(at>=0x1f26&&at<0x1f28)||(at>=0x1f84&&at<0x1fac);
}
std::vector<unsigned int> Words(const std::vector<int>& v){
    std::vector<unsigned int> output;for(unsigned int i=0;i<v.size();++i)output.push_back((unsigned int)v[i]);return output;
}
unsigned int FloatBits(float value){unsigned int n;memcpy(&n,&value,4);return n;}
}
bool NativePlayerPersistent::CaptureInitial(const NativePlayerState& state,const NativePlayerStartup& startup,const NativePlayerCreation& creation,const NativeEntityBody& body,unsigned int* next){
    return CaptureState(state,startup,creation,body,false,false,next);
}
bool NativePlayerPersistent::CaptureAfterInitialSetup(const NativePlayerState& state,const NativePlayerStartup& startup,const NativePlayerCreation& creation,const NativeEntityBody& body,unsigned int* next){
    return CaptureState(state,startup,creation,body,true,false,next);
}
bool NativePlayerPersistent::CaptureCurrent(const NativePlayerState& state,const NativePlayerStartup& startup,const NativePlayerCreation& creation,const NativeEntityBody& body,unsigned int* next){
    return CaptureState(state,startup,creation,body,true,true,next);
}
bool NativePlayerPersistent::CaptureState(const NativePlayerState& state,const NativePlayerStartup& startup,const NativePlayerCreation& creation,const NativeEntityBody& body,bool afterSetup,bool allowCurrent,unsigned int* next){
    if(next)*next=0x761de0;
    if(captured||!creation.prefixPrepared||!startup.ready||!state.resourcesReady||state.ghost||state.stats.ready!=afterSetup||
       !state.effects.Entries().empty()||!state.collectibleOverrides.empty()||state.temporaryCollectible||
       (!allowCurrent&&(!Empty(state.collectibleCounts)||!Empty(state.trinketCounts)||!Empty(state.pillEffectCounts))))return false;
    //This caller is the first room-entry checkpoint, BEFORE76c990. A real
    //grant/update is not accepted as the fresh path merely because its counts
    //happen to return to zero. The later save caller uses its complete owner.
    if(creation.postGenerationPrepared!=afterSetup||state.native1734!=afterSetup||(afterSetup&&!creation.initialSaveNormalized))return false;
    unsigned int twin,alternate;
    unsigned char ghost=1;
    if(!creation.constructorFields.Scalar(0x1d74,&twin)||twin||
       !creation.constructorFields.Scalar(0x1ca8,&alternate)||alternate||
       !creation.constructorFields.Byte(0x1f80,&ghost)||ghost)return false;
    //The base parent is null on the constructor-owned first player. No
    //secondary variant1 owner or familiar has been introduced before entry.
    if(!body.ready||body.parentToken||body.childToken||body.type!=1||body.variant||(body.flagsHigh&4))return false;
    NativePersistentFields live;
    //Only maintained live owners override the addressed initialized storage.
    Bind(live,0x1304,state.playerType);Bind(live,0x1288,state.maxRedHearts);Bind(live,0x128c,state.redHearts);
    Bind(live,0x1294,state.soulHearts);Bind(live,0x1298,state.blackHeartMask);
    Bind(live,0x12a4,state.keys);Bind(live,0x12ac,state.bombs);Bind(live,0x12b0,state.coins);
    Bind(live,0x12b4,state.bombReserve);
    live.Put(0x1478,(unsigned short)state.healthItemSlots,2);live.Put(0x147a,(unsigned short)state.healthItemRemainder,2);
    live.Put(0x12c8,state.native12c8,2);live.Put(0x12ca,state.native12ca);
    Bind(live,0x153c,creation.native153c);Bind(live,0x1544,creation.native1544);
    Bind(live,0x15d8,state.heldTrinkets[0]);Bind(live,0x15dc,state.heldTrinkets[1]);
    //These histories are genuinely empty at this native call (70af0f/1b).
    if(!state.native166c.empty()||!state.native1678.empty())return false;
    fields.Clear();words.clear();bytes.clear();bits.clear();effects.clear();persistentEffects.clear();
    //GameState constructs every destination with7087b0 BEFORE911da0 Reset.
    //Use the same explicit constructor writes as the player's cache owner;
    //Reset alone does not define the persistent record's retained members.
    NativePlayerCreationCache destinationConstructor;
    for(unsigned int at=0;at<0x5bc;++at){unsigned char n;
        if(destinationConstructor.Byte(at,&n))fields.Put(at,n,1);
    }
    //708a31 jumps from1d8 to200. Neither911da0 nor the reached761de0
    //capture assigns these obsolete scalar slots;916040 still emits them.
    fields.Unwritten(0x1dc,0x1c);
    //7087b0's explicit scalar writes omit360. Reset and761de0 also
    //leave it untouched; the legacy916040 file schema still emits it.
    fields.Unwritten(0x360,4);
    for(unsigned int i=0;i<sizeof(kPlayerPersistentReset)/sizeof(kPlayerPersistentReset[0]);++i){
        const NativePersistentResetWrite& w=kPlayerPersistentReset[i];fields.Put(w.offset,w.value,w.bytes);}
    for(unsigned int i=0;i<sizeof(kPlayerPersistentTransfers)/sizeof(kPlayerPersistentTransfers[0]);++i){
        const NativePlayerTransfer& t=kPlayerPersistentTransfers[i];if(next)*next=t.pc;
        for(unsigned int b=0;b<t.sourceBytes;++b){unsigned int n;unsigned char value;
            if(live.Get(t.source+b,1,&n))fields.Put(t.destination+b,n,1);
            else if(creation.constructorFields.Byte(t.source+b,&value))fields.Put(t.destination+b,value,1);
            else if(NativeUnwrittenPlayerByte(t.source+b))fields.Unwritten(t.destination+b,1);
            else return false;
        }
        if(t.destinationBytes>t.sourceBytes){unsigned int sign=0;
            if(t.signExtend){unsigned int n;if(!fields.Get(t.destination+t.sourceBytes-1,1,&n))return false;sign=(n&128)?255:0;}
            for(unsigned int b=t.sourceBytes;b<t.destinationBytes;++b)fields.Put(t.destination+b,sign,1);
        }
    }
    //Computed stores and absent optional owners from the actual761de0 path.
    unsigned char native12fc;
    if(!creation.constructorFields.Byte(0x12fc,&native12fc))return false;
    fields.Put(0x32,native12fc>0?1:0,1);fields.Put(0x388,0,1);fields.Put(0x520,0xffffffffU); //911da0 Reset, no parent store.
    fields.Put(0x524,0);fields.Put(0x21c,0);fields.Put(0x36c,0);fields.Put(0x41c,0);
    //Original12/16-byte vector transfers retain source order and all bank
    //states, including the consumed trinket97 owner. No Init replay or hash.
    words[0x14c]=Words(state.collectibleCounts);words[0x158]=Words(state.native166c);words[0x164]=Words(state.native1678);
    words[0x1b4]=Words(state.pillEffectCounts);bytes[0x178]=state.trinketCounts;
    for(int bank=0;bank<NPB_COUNT;++bank){std::vector<unsigned int>& v=words[0x184+12*bank];
        for(unsigned int i=0;i<startup.banks[bank].size();++i)v.push_back(startup.banks[bank][i].state);}
    bits[0x49c]=creation.native1e04;bits[0x4ac]=creation.native1e14;
    const unsigned int ranges[][3]={{0x1e3c,0x4bc,8},{0x1e4c,0x4d0,6},{0x1e52,0x4dc,20}};
    for(int r=0;r<3;++r){std::vector<unsigned char>& v=bytes[ranges[r][1]];
        for(unsigned int b=0;b<ranges[r][2];++b){unsigned char value;
            if(!creation.constructorFields.Byte(ranges[r][0]+b,&value))return false;v.push_back(value);}}
    //Other copied containers are constructor-owned empty here, not indexed
    //arrays fabricated from catalogue extents. Verify their actual descriptors.
    const unsigned int emptyVectors[][2]={{0x1654,0x410},{0x1e94,0x4f4},{0x1cfc,0x3a8}};
    for(int i=0;i<3;++i){unsigned int begin,end;
        if(!creation.constructorFields.Scalar(emptyVectors[i][0],&begin)||!creation.constructorFields.Scalar(emptyVectors[i][0]+4,&end)||begin!=end)return false;
        words[emptyVectors[i][1]].clear();}
    //No live costume, familiar, parent or suspended-entity record on this
    //fresh first-player path. Their descriptor counts are native stores.
    fields.Put(0x1c0,0);words[0x1c4].clear();words[0x1d0].clear();words[0x370].clear();words[0x37c].clear();
    //763372..434c filters the actual live effects for persistent records.
    //CaptureFresh already required that very owner to be empty above.
    words[0x50c].clear();
    unsigned int count;
    if(!creation.constructorFields.Scalar(0x1ec0,&count)||count)return false;
    words[0x5b4].clear();
    fields.Put(0x5b8,count); //The verified native1ec0 empty-tree count above.
    if(allowCurrent){
        //761de0's live-to-persistent pocket/active owners. The four active
        //records are native+54c/+550 stride8; card/pill use the 1f28/1f34
        //transfers to+53c/+548. Narrowed fields match the save encoder.
        for(int i=0;i<4;++i){
            fields.Put(0x54c+8*i,(unsigned int)state.activeItems[i],4);
            fields.Put(0x550+8*i,0,1);
        }
        fields.Put(0x53c,(unsigned int)(state.heldCard>0?state.heldCard:0),2);
        fields.Put(0x548,(unsigned int)(state.heldPill>=0?state.heldPill:0xffff),2);
        fields.Put(0x538,state.heldCard>0?1:0,1);
        fields.Put(0x539,state.heldPill>=0?1:0,1);
        //The 1dc..1ff slots are obsolete in the native player schema. The port
        //reuses seven of them for exact current position/stats; the marker
        //keeps older native files (no marker) on the native placement path.
        fields.Put(0x1dc,0x424F4933U);
        fields.Put(0x1f4,FloatBits(state.stats.luck));
        fields.Put(0x1e0,FloatBits(body.position.x));
        fields.Put(0x1e4,FloatBits(body.position.y));
        fields.Put(0x1e8,FloatBits(state.stats.fireDelay));
        fields.Put(0x1ec,FloatBits(state.stats.damage));
        fields.Put(0x1f0,FloatBits(state.stats.moveSpeed));
    }
    captured=true;if(next)*next=0;return true;
}
