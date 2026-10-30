#include "native_player_creation.h"
namespace {
bool Scalar(const NativePlayerCreation& c,unsigned int at,unsigned int expected){unsigned int n;return c.constructorFields.Scalar(at,&n)&&n==expected;}
bool Byte(const NativePlayerCreation& c,unsigned int at,unsigned char expected){unsigned char n;return c.constructorFields.Byte(at,&n)&&n==expected;}
}
bool NativePlayerCreation::NormalizeInitialSave(NativePlayerState& state,NativePlayerSprites& sprites,unsigned int* next){
    if(next)*next=0x757a50;
    if(!next||initialSaveNormalized||!postGenerationPrepared||!state.native1734||!state.runInitializing||!state.stats.ready||
       state.playerType||state.ghost||!state.pendingCollectibles.empty()||!state.effects.Entries().empty()||!costumeRecords.empty())return false;
    for(unsigned int i=0;i<state.collectibleCounts.size();++i)if(state.collectibleCounts[i])return false;
    //711e20 observes the actual null temporary-item owner. Its payload16e4
    //remains unwritten and is not inspected when16dc is null.
    if(state.temporaryCollectible||!Scalar(*this,0x16dc,0)||!Scalar(*this,0x16e0,0)){*next=0x711e20;return false;}
    //The true argument preserves12fc. Clear only the two native bytesfd/fe.
    constructorFields.AssignBytes(0x12fd,0,2);constructorFields.AssignScalar(0x15d0,0);
    //757a94's null21 effect is absent in the actual owned list.12d0 is the
    //temporary active item; none was introduced by this clean starting branch.
    if(!Scalar(*this,0x12d0,0)){*next=0x757c4f;return false;}
    //757d49/75 and7431e0 scan the same empty effect list. The17b0 health
    //adjustment loop and1ea4 item-recovery tree are constructor/reset empty.
    if(!Scalar(*this,0x17b0,0)||!Scalar(*this,0x1ea4,0)){*next=0x743219;return false;}
    constructorFields.AssignScalar(0x17b0,0);
    for(unsigned int at=0x1624;at<=0x164c;at+=4)constructorFields.AssignScalar(at,at<0x1634?0x3f800000U:0);
    //784d60(0,3,false) still scans all four live slot records; their actual
    //type1 is not the removal type2. No active is granted by argument0.
    for(int i=0;i<4;++i)if(!Scalar(*this,0x16bc+8*i,1)){*next=0x784d80;return false;}
    for(int i=0;i<4;++i)if(state.activeItems[i]){*next=0x757f94;return false;}
    //75808d clears the temporary action Sprite, then773ba0(ordinary type0).
    //Its original empty costume refresh is the same reached70f9f0 branch
    //used by76edae during appearance; retain the real skin/slot selectors.
    if(!Scalar(*this,0x1524,0)||!Byte(*this,0x12e0,0)){*next=0x758073;return false;}
    sprites.sprites[1].Clear();constructorFields.AssignBytes(0x12e2,0,1);
    for(int i=0;i<15;++i){NativePlayerCostumeSlot& s=costumeSlots[i];s.native0=s.native4=s.native8=-1;s.nativeC=(i>=1&&i<=3)||i==11;}
    constructorFields.AssignScalar(0x1524,0);constructorFields.AssignScalar(0x1528,0);
    //No326 effect means the count compared with5 at7580b4 is0. The two
    //constructor-owned preview owners retain equal native120 values, so the
    //backup-copy branch at7581a7 is not reached.
    if(previews[0].native120!=previews[1].native120){*next=0x758196;return false;}
    //78c4b0 clears the real empty1d5c pointer/count owner, without inventing
    //a list item or copying an address. Ordinary player0 has no parent.
    if(!Scalar(*this,0x1d60,0)||!Scalar(*this,0x1d64,0)||!Scalar(*this,0x1d6c,0)){*next=0x78c4b0;return false;}
    initialSaveNormalized=true;*next=0;return true;
}
