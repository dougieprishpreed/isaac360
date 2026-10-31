#include "native_player_state.h"
bool NativePlayerState::TrinketMultiplier(int id,int* output) const{
    if(!output||!ready||id<=0||(unsigned int)(id&0x7fff)>=trinketCounts.size()||trinketOverrides.size()!=trinketCounts.size())return false;
    //77c353: disabled trinkets and ghosts return before inventory predicates.
    if(trinketsDisabled||ghost){*output=0;return true;}
    id&=0x7fff;int total=0,value;bool held;
    //Ordinary New Run has no special trinket effect registration (1c2f50).
    //77c39f's three recursive selectors are checked in original order.
    if(id!=21&&id!=64&&id!=75){
        if(!TrinketMultiplier(21,&value)||value)return false;
        if(!TrinketMultiplier(64,&value)||value)return false;
        if(!TrinketMultiplier(75,&value)||value)return false;
        //Mom's Toenail/Black Lipstick temporary counters are zero after Init.
        //Their positive branches need the corresponding update owners.
        if((id==24&&native17ec>0)||(id==19&&native17f0>0))++total;
    }
    //77c4e1: held-slot capacity is selected before reading the slots.
    if(!HasCollectible(139,&held))return false;int count=held?2:1;
    if(!held){if(!HasCollectible(458,&held))return false;if(held)count=2;}
    for(int i=0;i<count;++i)if((heldTrinkets[i]&0x7fff)==id)total+=1+((heldTrinkets[i]&0x8000)!=0);
    if(temporaryTrinket==id)total+=1+(temporaryTrinketGolden?1:0);
    signed char smelted=(signed char)trinketCounts[id];if(smelted>0)total+=smelted;
    if(trinketOverrides[id])++total;
    if(total>0){if(!HasCollectible(439,&held))return false;if(held)++total;}
    *output=total;return true;
}
