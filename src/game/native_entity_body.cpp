#include "native_entity_body.h"
#include "native_entity_body_data.h"
#include "native_sprite.h"
#include <math.h>
#include <string.h>
namespace {
float F(unsigned int b){float v;memcpy(&v,&b,4);return v;}
bool Finite(float n){unsigned int b;memcpy(&b,&n,4);return (b&0x7fffffffU)<0x7f800000U;}
float Mul(float a,float b){volatile float v=a*b;return v;}
float Add(float a,float b){volatile float v=a+b;return v;}
float Div(float a,float b){volatile float v=a/b;return v;}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
void Scalar(unsigned int& h,float n){unsigned int b;memcpy(&b,&n,4);Fold(h,b);}
//Inherited entities2.xml type10 physical records. AI, death FX and rendering
//remain separate; these are the actual geometry/HP/damage fields.
const NativeEntityBodyProperties kEnemyBodies[]={
    {10,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,10.0f,0.0f,"gfx/characters/010.000_Frowning Gaper.anm2"},
    {10,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,10.0f,0.0f,"gfx/characters/010.001_Gaper.anm2"},
    {13,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,0.0f,3.0f,3.0f,0.0f,"gfx/characters/013.000_Fly.anm2"},
    {14,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,0.0f,4.0f,8.0f,0.0f,"gfx/characters/014.000_Pooter.anm2"},
    {208,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/characters/207.000_Fatty.anm2"},
    {226,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,4.0f,"gfx/226.000_Skinny.anm2"},
    {226,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,16.0f,5.0f,"gfx/226.001_Rotty.anm2"},
    {284,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,10.0f,0.0f,"gfx/284.000_Cyclopia.anm2"},
    {217,0,0,12,0.12f,1.0f,9.0f,1.0f,1.0f,1.0f,6.0f,3.0f,1.0f,"gfx/216.000_Dip.anm2"},
    {217,1,0,12,0.12f,1.0f,9.0f,1.0f,1.0f,1.0f,6.0f,3.0f,1.0f,"gfx/216.001_Corn.anm2"},
    {217,2,0,12,0.12f,1.0f,9.0f,1.0f,1.0f,1.0f,6.0f,3.0f,1.0f,"gfx/216.002_BrownieCorn.anm2"},
    {217,3,0,12,0.12f,1.0f,9.0f,1.0f,1.0f,1.0f,6.0f,6.0f,1.0f,"gfx/216.003_Big Corn.anm2"},
    {256,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,1.0f,3.0f,5.0f,0.0f,"gfx/256.000_DartFly.anm2"},
    {261,0,0,12,0.28f,1.0f,20.0f,1.0f,1.0f,1.0f,75.0f,300.0f,0.0f,"gfx/261.000_Dingle.anm2"},
    {261,1,0,12,0.28f,1.0f,20.0f,1.0f,1.0f,1.0f,75.0f,375.0f,0.0f,"gfx/261.001_Dangle.anm2"},
    {19,0,0,8,0.25f,1.0f,20.0f,1.0f,1.0f,1.0f,50.0f,22.0f,0.0f,"gfx/019.000_Larry Jr..anm2"},
    {20,0,0,12,0.56f,1.0f,40.0f,1.0f,1.0f,1.0f,50.0f,250.0f,0.0f,"gfx/020.000_Monstro.anm2"},
    {67,0,0,12,0.34f,1.0f,35.0f,1.0f,1.0f,1.0f,30.0f,110.0f,0.0f,"gfx/067.000_The Duke of Flies.anm2"},
    {260,0,0,12,0.28f,1.0f,26.0f,1.0f,1.0f,2.0f,50.0f,200.0f,0.0f,"gfx/260.000_Haunt.anm2"},
    {18,0,0,4,0.11f,1.0f,17.0f,1.0f,1.0f,1.0f,3.0f,5.0f,0.0f,"gfx/018.000_Attack Fly.anm2"},
    {79,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,20.0f,140.0f,0.0f,"gfx/079.000_Gemini.anm2"},
    {237,1,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,60.0f,0.0f,"gfx/237.000_Gurgling.anm2"},
    {237,2,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,50.0f,0.0f,"gfx/237.002_Turdling.anm2"},
    {237,0,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,24.0f,0.0f,"gfx/237.000_Gurgling.anm2"},
    {239,0,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,0.0f,"gfx/239.000_Grub.anm2"},
    {908,0,0,12,0.30f,1.0f,20.0f,1.0f,1.0f,1.0f,20.0f,234.0f,0.0f,"gfx/908.000_baby plum.anm2"},
    {21,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,15.0f,0.0f,"gfx/021.000_Maggot.anm2"},
    {22,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,7.0f,20.0f,0.0f,"gfx/022.000_Hive.anm2"},
    {23,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,20.0f,0.0f,"gfx/023.000_Charger.anm2"},
    {24,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,33.0f,0.0f,"gfx/024.000_Globin.anm2"},
    {25,0,0,12,0.20f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,8.0f,0.0f,"gfx/025.000_Boom Fly.anm2"},
    {26,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,20.0f,0.0f,"gfx/026.000_Maw.anm2"},
    {27,0,0,12,0.25f,1.0f,13.0f,1.0f,1.0f,0.0f,15.0f,14.0f,0.0f,"gfx/027.000_Host.anm2"},
    {29,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,10.0f,0.0f,"gfx/029.000_Hopper.anm2"},
    {30,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/030.000_Boil.anm2"},
    {31,0,0,12,0.26f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,10.0f,0.0f,"gfx/031.000_Spity.anm2"},
    {61,0,0,4,0.14f,1.0f,17.0f,1.0f,1.0f,1.0f,7.0f,10.0f,0.0f,"gfx/061.000_Sucker.anm2"},
    {85,0,0,12,0.11f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,6.5f,0.0f,"gfx/085.000_Spider.anm2"},
    {86,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,36.0f,0.0f,"gfx/086.000_Keeper.anm2"},
    {87,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,40.0f,0.0f,"gfx/087.000_Gurgle.anm2"},
    {88,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,20.0f,0.0f,"gfx/088.000_Walking Boil.anm2"},
    {89,0,0,12,0.24f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,20.0f,0.0f,"gfx/089.000_Buttlicker.anm2"},
    {94,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,20.0f,0.0f,"gfx/094.000_Big Spider.anm2"},
    {220,0,0,12,0.21f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,15.0f,0.0f,"gfx/220.000_Squirt.anm2"},
    {227,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,8.0f,0.0f,"gfx/227.000_Boney.anm2"},
    {234,0,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,20.0f,0.0f,"gfx/234.000_OneTooth.anm2"},
    {238,0,0,12,0.11f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,0.0f,"gfx/238.000_Splasher.anm2"},
    {240,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,0.0f,"gfx/240.000_Wall Creep.anm2"},
    {242,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,0.0f,"gfx/242.000_Blind Creep.anm2"},
    {243,0,0,12,0.24f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,18.0f,0.0f,"gfx/243.000_Conjoined Spitty.anm2"},
    {244,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,10.0f,0.0f,"gfx/244.000_Round Worm.anm2"},
    {248,0,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,1.0f,3.0f,10.0f,0.0f,"gfx/248.000_Psychic Horf.anm2"},
    {22,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,7.0f,18.0f,0.0f,"gfx/022.001_Drowned Hive.anm2"},
    {23,1,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,22.0f,0.0f,"gfx/023.001_Drowned Charger.anm2"},
    {24,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,33.0f,0.0f,"gfx/024.001_Gazing Globin.anm2"},
    {25,1,0,12,0.20f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,8.0f,0.0f,"gfx/025.001_Red Boom Fly.anm2"},
    {25,2,0,12,0.20f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,8.0f,0.0f,"gfx/025.002_Drowned Boom Fly.anm2"},
    {26,1,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,20.0f,0.0f,"gfx/026.001_Red Maw.anm2"},
    {26,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,7.0f,20.0f,0.0f,"gfx/026.002_Psychic Maw.anm2"},
    {27,1,0,12,0.25f,1.0f,13.0f,1.0f,1.0f,0.0f,15.0f,15.0f,0.0f,"gfx/027.001_Red Host.anm2"},
    {29,1,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,10.0f,0.0f,"gfx/029.001_Trite.anm2"},
    {30,1,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/030.001_Gut.anm2"},
    {30,2,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/030.002_Sack.anm2"},
    {61,1,0,4,0.14f,1.0f,17.0f,1.0f,1.0f,1.0f,7.0f,10.0f,0.0f,"gfx/061.001_Spit.anm2"},
    {88,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,20.0f,0.0f,"gfx/088.001_Walking Gut.anm2"},
    {88,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,20.0f,0.0f,"gfx/088.002_Walking Sack.anm2"},
    {220,1,0,12,0.21f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,15.0f,0.0f,"gfx/220.001_Dank Squirt.anm2"},
    {14,1,0,4,0.13f,1.0f,17.0f,1.0f,1.0f,0.0f,4.0f,8.0f,0.0f,"gfx/014.001_Super Pooter.anm2"},
    {208,1,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/207.001_Pale Fatty.anm2"},
    {39,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/039.002_Chubber.anm2"},
    {300,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,23.0f,0.0f,"gfx/300.000_MushroomMan.anm2"},
    {28,0,0,12,0.42f,1.0f,18.0f,1.0f,1.0f,2.0f,20.0f,350.0f,0.0f,"gfx/028.000_Chub.anm2"},
    {33,0,0,0,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,5.0f,0.0f,"gfx/033.000_Fireplace.anm2"},
    {36,0,0,12,1.0f,1.0f,80.0f,1.0f,1.0f,1.0f,100.0f,595.0f,0.0f,"gfx/036.000_Gurdy.anm2"},
    {62,0,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,100.0f,0.0f,"gfx/062.000_Pin.anm2"},
    {68,0,0,12,0.41f,1.0f,27.0f,1.0f,1.0f,2.0f,40.0f,450.0f,0.0f,"gfx/068.000_Peep.anm2"},
    {71,0,0,12,0.43f,1.0f,40.0f,1.0f,1.0f,2.0f,40.0f,60.0f,0.0f,"gfx/071.000_Fistula Big.anm2"},
    {99,0,0,12,0.57f,1.0f,35.0f,1.0f,1.0f,1.0f,80.0f,250.0f,0.0f,"gfx/099.000_Gurdy Jr..anm2"},
    {262,0,0,12,0.50f,1.0f,33.0f,1.0f,1.0f,1.0f,100.0f,350.0f,0.0f,"gfx/262.000_MegaMaw.anm2"},
    {264,0,0,12,0.56f,1.0f,40.0f,1.0f,1.0f,1.0f,100.0f,600.0f,0.0f,"gfx/264.000_Mega Fatty.anm2"},
    {916,0,0,12,0.40f,1.0f,30.0f,1.0f,1.0f,1.0f,40.0f,385.0f,0.0f,"gfx/916.000_bumbino.anm2"},
    {32,0,0,12,0.25f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,20.0f,0.0f,"gfx/032.000_Brain.anm2"},
    {34,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/034.000_Leaper.anm2"},
    {35,0,0,12,0.12f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/035.000_Mr. Maw Body.anm2"},
    {38,0,0,8,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/038.000_Baby.anm2"},
    {39,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/039.000_Vis.anm2"},
    {40,0,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/040.000_Guts.anm2"},
    {41,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/041.000_Knight.anm2"},
    {44,0,0,12,0.0f,1.0f,16.0f,1.0f,1.0f,1.0f,15.0f,100.0f,0.0f,"gfx/044.000_Poky.anm2"},
    {90,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,30.0f,40.0f,0.0f,"gfx/090.000_Hanger.anm2"},
    {91,0,0,12,0.26f,1.0f,13.0f,1.0f,1.0f,0.0f,30.0f,40.0f,0.0f,"gfx/091.000_Swarmer.anm2"},
    {92,0,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,0.0f,30.0f,40.0f,0.0f,"gfx/092.000_Heart.anm2"},
    {93,0,0,12,0.15f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,40.0f,0.0f,"gfx/093.000_Mask.anm2"},
    {209,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,16.0f,0.0f,"gfx/208.000_Fatsack.anm2"},
    {212,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,20.0f,20.0f,0.0f,"gfx/212.000_DeathsHead.anm2"},
    {219,0,0,12,0.21f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/219.000_Wizoob.anm2"},
    {229,0,0,12,0.26f,1.0f,16.0f,1.0f,1.0f,1.0f,22.0f,20.0f,0.0f,"gfx/229.000_Tumor.anm2"},
    {235,0,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,100.0f,0.0f,"gfx/235.000_GapingMaw.anm2"},
    {241,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,10.0f,0.0f,"gfx/241.000_Rage Creep.anm2"},
    {254,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/254.000_Floating Knight.anm2"},
    {277,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,8.0f,0.0f,"gfx/277.000_Black Boney.anm2"},
    {280,0,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,1.0f,3.0f,10.0f,0.0f,"gfx/280.000_Black Globins Body.anm2"},
    {260,10,0,12,0.11f,1.0f,13.0f,1.0f,1.0f,1.0f,16.0f,25.0f,0.0f,"gfx/260.010_Lil Haunt.anm2"},
    {19,1,0,8,0.23f,1.0f,20.0f,1.0f,1.0f,1.0f,50.0f,22.0f,0.0f,"gfx/019.001_The Hollow.anm2"},
    {33,2,0,0,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,5.0f,0.0f,"gfx/033.002_Blue Fireplace.anm2"},
    {72,1,0,12,0.27f,1.0f,25.0f,1.0f,1.0f,2.0f,30.0f,15.0f,0.0f,"gfx/072.001_Teratoma Medium.anm2"},
    {44,1,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,15.0f,100.0f,0.0f,"gfx/044.001_Slide.anm2"},
    {43,0,0,12,0.52f,1.0f,35.0f,1.0f,1.0f,1.0f,50.0f,632.5f,0.0f,"gfx/043.000_Monstro II.anm2"},
    {45,0,0,0,0.33f,1.0f,35.0f,1.0f,1.0f,2.0f,70.0f,645.0f,0.0f,"gfx/045.000_Mom.anm2"},
    {263,0,0,12,0.0f,1.0f,33.0f,1.0f,1.0f,1.0f,100.0f,500.0f,0.0f,"gfx/263.000_MegaMaw2.anm2"},
    {265,0,0,12,0.56f,1.0f,40.0f,1.0f,1.0f,1.0f,100.0f,800.0f,0.0f,"gfx/265.000_Mega Fatty 2.anm2"},
    {900,0,0,12,0.54f,1.0f,35.0f,1.0f,1.0f,1.0f,38.0f,690.0f,0.0f,"gfx/900.000_reap creep.anm2"},
    {24,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,33.0f,0.0f,"gfx/024.002_Dank Globin.anm2"},
    {33,3,0,0,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,5.0f,0.0f,"gfx/033.003_Purple Fireplace.anm2"},
    {39,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/039.001_Double Vis.anm2"},
    {42,0,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/042.000_Stone Grimace.anm2"},
    {42,1,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/042.001_Vomit Grimace.anm2"},
    {55,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,25.0f,0.0f,"gfx/055.000_Leech.anm2"},
    {72,0,0,12,0.25f,1.0f,25.0f,1.0f,1.0f,2.0f,30.0f,15.0f,0.0f,"gfx/072.000_Fistula Medium.anm2"},
    {73,1,0,12,0.23f,1.0f,13.0f,1.0f,1.0f,2.0f,20.0f,8.0f,0.0f,"gfx/073.001_Teratoma Small.anm2"},
    {80,0,0,0,0.13f,1.0f,17.0f,1.0f,1.0f,1.0f,3.0f,10.0f,0.0f,"gfx/080.000_Moter.anm2"},
    {83,0,0,0,0.23f,1.0f,16.0f,1.0f,1.0f,2.0f,30.0f,100.0f,0.0f,"gfx/083.000_Headless Horsemans Head.anm2"},
    {204,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,14.0f,0.0f,"gfx/204.000_Mobile Host.anm2"},
    {205,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,7.0f,20.0f,0.0f,"gfx/205.000_Nest.anm2"},
    {210,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,16.0f,0.0f,"gfx/209.000_Blubber.anm2"},
    {211,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,10.0f,0.0f,"gfx/210.000_Half Sack.anm2"},
    {212,1,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,20.0f,20.0f,0.0f,"gfx/212.001_Dank DeathsHead.anm2"},
    {213,0,0,12,0.42f,1.0f,40.0f,1.0f,1.0f,1.0f,16.0f,20.0f,0.0f,"gfx/212.000_Moms Hand.anm2"},
    {223,0,0,12,0.11f,1.0f,17.0f,1.0f,1.0f,2.0f,3.0f,62.0f,0.0f,"gfx/223.000_Dinga.anm2"},
    {246,0,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,13.0f,0.0f,"gfx/246.000_Ragling.anm2"},
    {252,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,30.0f,0.0f,"gfx/252.000_Nulls.anm2"},
    {258,0,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,35.0f,0.0f,"gfx/258.000_Fat Bat.anm2"},
    {282,0,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/282.000_MegaClotty.anm2"},
    {288,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,20.0f,25.0f,0.0f,"gfx/288.000_Dukie.anm2"},
    {868,0,0,4,0.11f,1.0f,14.0f,1.0f,1.0f,1.0f,3.0f,5.0f,0.0f,"gfx/868.000_army fly.anm2"},
    {881,0,0,12,0.14f,1.0f,14.0f,1.0f,1.0f,1.0f,30.0f,10.0f,0.0f,"gfx/881.000_needle.anm2"},
    {881,1,0,12,0.14f,1.0f,14.0f,1.0f,1.0f,1.0f,30.0f,10.0f,0.0f,"gfx/881.001_pasty.anm2"},
    {35,2,0,12,0.12f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/035.002_Mr. Red Maw Body.anm2"},
    {29,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/029.002_Eggy.anm2"},
    {42,2,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,20.0f,0.0f,"gfx/042.002_Triple Grimace.anm2"},
    {202,0,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,1.0f,0.0f,"gfx/202.000_Constant Stone Shooter.anm2"},
    {202,10,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,1.0f,0.0f,"gfx/202.010_cross stone shooter.anm2"},
    {231,0,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,20.0f,0.0f,"gfx/231.000_NerveEnding.anm2"},
    {236,0,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,100.0f,0.0f,"gfx/236.000_Broken GapingMaw.anm2"},
    {301,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,20.0f,0.0f,"gfx/301.000_PoisonMind.anm2"},
    {303,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,16.0f,0.0f,"gfx/303.000_Blister.anm2"},
    {304,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,15.0f,0.0f,"gfx/304.000_TheThing.anm2"},
    {305,0,0,12,0.20f,1.0f,13.0f,1.0f,1.0f,1.0f,5.0f,9.0f,0.0f,"gfx/305.000_Ministro.anm2"},
    {306,0,0,0,0.24f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,9.0f,0.0f,"gfx/306.000_Portal.anm2"},
    {15,0,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,15.0f,0.0f,"gfx/015.000_Clotty.anm2"},
    {15,1,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,15.0f,0.0f,"gfx/015.001_Clot.anm2"},
    {15,3,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/015.003_grilled clotty.anm2"},
    {53,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,10.0f,20.0f,0.0f,"gfx/053.000_Dople.anm2"},
    {56,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,30.0f,0.0f,"gfx/056.000_Lump.anm2"},
    {57,0,0,12,0.42f,1.0f,13.0f,1.0f,1.0f,2.0f,30.0f,62.0f,0.0f,"gfx/057.000_MemBrain.anm2"},
    {57,1,0,12,0.40f,1.0f,13.0f,1.0f,1.0f,2.0f,30.0f,62.0f,0.0f,"gfx/057.001_Mama Guts.anm2"},
    {58,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,35.0f,0.0f,"gfx/058.000_Para-Bite.anm2"},
    {58,1,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,35.0f,0.0f,"gfx/058.001_Scarred Para-Bite.anm2"},
    {59,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,20.0f,0.0f,"gfx/059.000_Fred.anm2"},
    {60,0,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,20.0f,0.0f,"gfx/060.000_Eye.anm2"},
    {60,1,0,12,0.0f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,20.0f,0.0f,"gfx/060.001_Bloodshot Eye.anm2"},
    {75,0,0,12,0.40f,1.0f,30.0f,1.0f,1.0f,2.0f,20.0f,75.0f,0.0f,"gfx/075.000_Blastocyst Medium.anm2"},
    {76,0,0,12,0.31f,1.0f,20.0f,1.0f,1.0f,1.0f,10.0f,30.0f,0.0f,"gfx/076.000_Blastocyst Small.anm2"},
    {77,0,0,12,0.22f,1.0f,13.0f,1.0f,1.0f,1.0f,3.0f,12.0f,0.0f,"gfx/077.000_Embryo.anm2"},
    {216,0,0,12,0.12f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,38.0f,0.0f,"gfx/221.000_Swinger.anm2"},
    {218,0,0,12,0.0f,1.0f,16.0f,1.0f,1.0f,1.0f,15.0f,100.0f,0.0f,"gfx/218.000_Wall Hugger.anm2"},
    {221,0,0,12,0.30f,1.0f,16.0f,1.0f,1.0f,2.0f,100.0f,20.0f,0.0f,"gfx/220.000_Cod Worm.anm2"},
    {224,0,0,8,0.24f,1.0f,18.0f,1.0f,1.0f,1.0f,30.0f,100.0f,0.0f,"gfx/224.000_Oob.anm2"},
    {228,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,36.0f,0.0f,"gfx/228.000_Homunculus.anm2"},
    {229,1,0,12,0.26f,1.0f,16.0f,1.0f,1.0f,1.0f,22.0f,28.0f,0.0f,"gfx/229.001_Planetoid.anm2"},
    {247,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,27.0f,0.0f,"gfx/247.000_Flesh Mobile Host.anm2"},
    {286,0,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,1.0f,20.0f,50.0f,0.0f,"gfx/286.000_FleshDeathsHead.anm2"},
    {290,0,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,15.0f,0.0f,"gfx/290.000_Meatball.anm2"},
    {835,0,0,12,0.15f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,60.0f,0.0f,"gfx/835.000_peeper fatty.anm2"},
    {859,0,0,12,0.20f,1.0f,13.0f,1.0f,1.0f,1.0f,40.0f,20.0f,0.0f,"gfx/859.000_floating host.anm2"},
    {62,1,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,2.0f,30.0f,300.0f,0.0f,"gfx/062.001_Scolex.anm2"},
    {62,2,0,12,0.18f,1.0f,13.0f,1.0f,1.0f,1.0f,30.0f,200.0f,0.0f,"gfx/062.002_TheFrail.anm2"},
    {78,0,0,0,0.33f,1.0f,35.0f,1.0f,1.0f,0.0f,100.0f,950.0f,0.0f,"gfx/078.000_Moms Heart.anm2"},
    {78,1,0,0,0.33f,1.0f,35.0f,1.0f,1.0f,0.0f,100.0f,950.0f,0.0f,"gfx/078.001_It Lives.anm2"},
    {266,0,0,12,0.0f,1.0f,30.0f,1.0f,1.0f,2.0f,100.0f,750.0f,0.0f,"gfx/266.000_Mega Gurdy.anm2"},
    {270,0,0,8,0.0f,1.0f,16.0f,1.0f,1.0f,2.0f,60.0f,500.0f,0.0f,"gfx/270.000_MegaFred.anm2"},
    {74,0,0,12,0.55f,1.0f,40.0f,1.0f,1.0f,2.0f,30.0f,190.0f,0.0f,"gfx/074.000_Blastocyst Big.anm2"},
    {81,0,0,0,0.24f,1.0f,20.0f,1.0f,1.0f,2.0f,50.0f,60.0f,0.0f,"gfx/081.000_The Fallen.anm2"},
    {55,1,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,25.0f,0.0f,"gfx/055.001_Kamikaze Leech.anm2"},
    {55,2,0,12,0.17f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,25.0f,0.0f,"gfx/055.002_Holy Leech.anm2"},
    {834,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,35.0f,0.0f,"gfx/834.000_whipper.anm2"},
    {834,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,35.0f,0.0f,"gfx/834.001_snapper.anm2"},
    {41,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/041.001_Selfless Knight.anm2"},
    {38,3,0,4,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/038.003_Wrinkly Baby.anm2"},
    {38,1,0,8,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/038.001_Angelic Baby.anm2"},
    {61,2,0,4,0.14f,1.0f,17.0f,1.0f,1.0f,1.0f,100.0f,18.0f,0.0f,"gfx/061.002_soul sucker.anm2"},
    {61,5,0,4,0.14f,1.0f,17.0f,1.0f,1.0f,0.0f,5.0f,10.0f,0.0f,"gfx/061.005_bulb.anm2"},
    {251,0,0,12,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,14.0f,60.0f,0.0f,"gfx/251.000_Begotten.anm2"},
    {283,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/283.000_BoneKnight.anm2"},
    {259,0,0,4,0.16f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,25.0f,0.0f,"gfx/259.000_Imp.anm2"},
    {285,0,0,12,0.21f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,20.0f,0.0f,"gfx/285.000_Red Ghost.anm2"},
    {230,0,0,12,0.26f,1.0f,16.0f,1.0f,1.0f,1.0f,22.0f,32.0f,0.0f,"gfx/230.000_camillojr.anm2"},
    {225,0,0,8,0.32f,1.0f,20.0f,1.0f,1.0f,1.0f,14.0f,20.0f,0.0f,"gfx/225.000_Black Maw.anm2"},
    {278,0,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,33.0f,0.0f,"gfx/278.000_Black Globin.anm2"},
    {203,0,0,0,0.22f,1.0f,13.0f,1.0f,1.0f,0.0f,100.0f,1.0f,0.0f,"gfx/203.000_Brimstone Head.anm2"},
    {50,0,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,2.0f,30.0f,60.0f,0.0f,"gfx/050.000_Greed.anm2"},
    {52,0,0,12,0.12f,1.0f,13.0f,1.0f,1.0f,2.0f,30.0f,60.0f,0.0f,"gfx/052.000_Pride.anm2"},
    {53,1,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,10.0f,20.0f,0.0f,"gfx/053.001_Evil Twin.anm2"},
    {279,0,0,12,0.25f,1.0f,13.0f,1.0f,1.0f,2.0f,10.0f,20.0f,0.0f,"gfx/279.000_Black Globins Head.anm2"},
    {833,0,0,12,0.10f,1.0f,13.0f,1.0f,1.0f,1.0f,16.0f,25.0f,0.0f,"gfx/833.000_candler.anm2"},
    {816,0,0,8,0.10f,1.0f,13.0f,1.0f,1.0f,1.0f,16.0f,14.0f,0.0f,"gfx/816.000_polty.anm2"},
    {805,0,0,12,0.34f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,14.0f,0.0f,"gfx/805.000_bishop.anm2"},
    {253,0,0,12,0.26f,1.0f,16.0f,1.0f,1.0f,1.0f,22.0f,40.0f,0.0f,"gfx/253.000_PsyTumor.anm2"},
    {22,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,0.0f,7.0f,40.0f,0.0f,"gfx/022.002_Holy Mulligan.anm2"},
    {60,2,0,12,0.14f,1.0f,13.0f,1.0f,1.0f,1.0f,100.0f,20.0f,0.0f,"gfx/060.002_Holy Eye.anm2"},
    {227,1,0,12,0.20f,1.0f,20.0f,1.0f,1.0f,1.0f,30.0f,60.0f,0.0f,"gfx/227.001_Holy Bony.anm2"},
    {69,0,0,12,0.19f,1.0f,20.0f,1.0f,1.0f,2.0f,20.0f,350.0f,0.0f,"gfx/069.000_Loki.anm2"},
    {100,0,0,12,0.54f,1.0f,35.0f,1.0f,1.0f,1.0f,38.0f,130.0f,0.0f,"gfx/100.000_Widow.anm2"},
    {291,0,0,12,0.0f,1.0f,32.0f,1.0f,1.0f,0.0f,50.0f,0.0f,0.0f,"gfx/291.000_Pitfall.anm2"},
    {15,2,0,12,0.28f,1.0f,13.0f,1.0f,1.0f,1.0f,10.0f,15.0f,0.0f,"gfx/015.002_I.Blob.anm2"},
    {84,0,0,12,0.52f,1.0f,35.0f,1.0f,1.0f,2.0f,50.0f,600.0f,0.0f,"gfx/084.000_Satan.anm2"},
    {102,0,0,12,0.33f,1.0f,20.0f,1.0f,1.0f,1.0f,50.0f,2000.0f,0.0f,"gfx/102.000_Isaac (final boss).anm2"},
    {16,0,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,0.0f,6.0f,13.0f,0.0f,"gfx/016.000_Mulligan.anm2"},
    {16,1,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,0.0f,6.0f,13.0f,0.0f,"gfx/016.001_Mulligoon.anm2"},
    {16,2,0,12,0.13f,1.0f,13.0f,1.0f,1.0f,0.0f,6.0f,13.0f,0.0f,"gfx/016.002_Mulliboom.anm2"}
};
}
const NativeEntityBodyProperties* NativePreparedEnemyBody(int type,int variant){
    for(unsigned int i=0;i<sizeof(kEnemyBodies)/sizeof(kEnemyBodies[0]);++i)
        if(kEnemyBodies[i].type==type&&kEnemyBodies[i].variant==variant)return &kEnemyBodies[i];
    return 0;
}
bool NativeBossEntityType(int type,int variant){
    //Prepared floor-boss families. Variant coverage expands with each
    //prepared body record.
    if(type==261)return variant==0||variant==1;
    if(type==19)return variant==0||variant==1;
    if(type==20)return variant==0;
    if(type==67)return variant==0;
    if(type==260)return variant==0;
    if(type==79)return variant==0;
    if(type==237)return variant==1||variant==2;
    if(type==908)return variant==0;
    if(type==28)return variant==0;
    if(type==36)return variant==0;
    if(type==62)return variant==0;
    if(type==68)return variant==0;
    if(type==71)return variant==0;
    if(type==99)return variant==0;
    if(type==262)return variant==0;
    if(type==264)return variant==0;
    if(type==916)return variant==0;
    if(type==72)return variant==0||variant==1;
    if(type==43)return variant==0;
    if(type==45)return variant==0;
    if(type==263)return variant==0;
    if(type==265)return variant==0;
    if(type==900)return variant==0;
    if(type==75)return variant==0;
    if(type==76)return variant==0;
    if(type==62)return variant==1||variant==2;
    if(type==78)return variant==0||variant==1;
    if(type==266)return variant==0;
    if(type==270)return variant==0;
    if(type==74)return variant==0;
    if(type==81)return variant==0;
    if(type==50)return variant==0;
    if(type==52)return variant==0;
    if(type==69)return variant==0;
    if(type==84)return variant==0;
    if(type==100)return variant==0;
    if(type==102)return variant==0;
    return false;
}
const NativeEntityBodyProperties* NativePreparedPlayerBody(int variant){
    for(unsigned int i=0;i<sizeof(kNativePlayerBodies)/sizeof(kNativePlayerBodies[0]);++i)
        if(kNativePlayerBodies[i].variant==variant)return &kNativePlayerBodies[i];
    return 0;
}
bool NativeEntityCollisionShape::Resize(float r,float x,float y,int count,const NativeEntityBodyProperties& props){
    if(x==-1&&y==-1){x=props.xMultiplier;y=props.yMultiplier;}if(count==-1)count=props.gridPoints;
    if(!Finite(r)||!Finite(x)||!Finite(y))return false;
    if(radius==r&&xMultiplier==x&&yMultiplier==y)return true;
    std::vector<NativePoint> next;
    //Keep separate SSE float operations before the double CRT calls. Native
    //precision across different CRTs/PowerPC remains a target-validation limit.
    for(int parity=0;parity<2;++parity)for(int i=parity;i<count;){
        float angle=Div(Mul(Add((float)i,(float)i),F(kNativeBodyPi)),(float)count);
        volatile float c=(float)cos((double)angle),s=(float)sin((double)angle);
        NativePoint p={Mul(c,Mul(r,x)),Mul(s,Mul(r,y))};
        if(!Finite(p.x)||!Finite(p.y))return false;next.push_back(p);
        if(i>count-3)break;i+=2;
    }
    radius=r;xMultiplier=x;yMultiplier=y;points.swap(next);return true;
}
void NativeEntityCollisionShape::Swap(NativeEntityCollisionShape& other){
    float r=radius,x=xMultiplier,y=yMultiplier;radius=other.radius;xMultiplier=other.xMultiplier;yMultiplier=other.yMultiplier;
    other.radius=r;other.xMultiplier=x;other.yMultiplier=y;points.swap(other.points);
}
NativeEntityBody::NativeEntityBody():ready(false),native168(false),native169(false),native16a(false),native16b(false),removed(false),native16e(false),flagsLow(0),flagsHigh(0),type(0),variant(0),subtype(0),frameCount(0),initialFrame(0),state(0),gridCollisionClass(0),entityCollisionClass(0),renderDepth(0),initialSeed(0),entityToken(0),native34(0),native38(0),parentToken(0),childToken(0),
    shadowScale(0),friction(0),mass(0),collisionDamage(0),hitPoints(0),maxHitPoints(0),native2a0(0),native2ec(0),native2fc(0),appliedFriction(0),previousTime(0),time(0),native304(0),native308(0),native26c(0),native27c(0),native340(0),native360(0),boundStatus(false),damagePending(false){
    position.x=position.y=velocity.x=velocity.y=previousPosition.x=previousPosition.y=previousVelocity.x=previousVelocity.y=0;random.Default();
    native264.x=native264.y=0;memset(statusTimers,0,sizeof(statusTimers));
    native18=native1c=0;native34c=native350=0;
    gridContactKnown=gridContact=false;contactVelocity.x=contactVelocity.y=contactNormal.x=contactNormal.y=0;
}
NativeEntityBodyResult NativeEntityBody::BeginFresh(int t,int v,int sub,unsigned int seed,unsigned int frame,int stage,const NativePoint* pos,const NativePoint* vel){
    if(t<0||v<0||sub<0||!seed||stage<0)return NEB_BAD_REQUEST;
    const NativeEntityBodyProperties* p=t==1&&sub==0?NativePreparedPlayerBody(v):
        (t==1000?NativePreparedEffectBody(v,sub):(sub==0?NativePreparedEnemyBody(t,v):0));
    if(!p)return NEB_UNSUPPORTED_PROPERTIES;
    NativeEntityBody next;next.type=t;next.variant=v;next.subtype=sub;next.initialSeed=seed;next.random.Set(seed,30);
    next.frameCount=0; //66f544: native+24 reset.
    next.native27c=-1; //66f566: separate damage aggregate, not the frame count.
    next.initialFrame=(int)frame;next.shadowScale=p->shadowScale;next.friction=p->friction;
    next.mass=p->mass;next.collisionDamage=p->collisionDamage;next.hitPoints=p->baseHP;
    if(p->stageHP>F(kNativeBodyStageThreshold)){float s=(float)stage;if(s>F(kNativeBodyStageCap))s=F(kNativeBodyStageCap);
        next.hitPoints=Add(Mul(Mul(stage<5?F(kNativeBodyEarlyHP):F(kNativeBodyLateHP),s),p->stageHP),p->baseHP);}
    next.maxHitPoints=next.hitPoints;
    if(pos)next.position=*pos;if(vel)next.velocity=*vel;
    next.previousPosition=next.position;next.previousVelocity=next.velocity;
    if(!next.collision.Resize(p->radius,p->xMultiplier,p->yMultiplier,p->gridPoints,*p))return NEB_BAD_REQUEST;
    //Both bound player records omit a gridcollision override. Configured
    //boundary samples select5; an empty point vector selects3 at0066fb2c.
    next.gridCollisionClass=next.collision.points.empty()?3:5;
    //66f5ec is a FOUR-byte write at169: both169 and16a become true.
    next.native169=next.native16a=true;next.state=2;next.entityCollisionClass=4;
    next.native2ec=1;next.native2fc=F(0x3f666666U);next.appliedFriction=next.friction;
    next.previousTime=F(0xb727c5acU);next.time=0;next.native304=next.native308=1;
    next.ready=true;Swap(next);return NEB_OK;
}
void NativeEntityBody::Swap(NativeEntityBody& other){
    //The scalar prefix contains no pointers, vtable or padding-dependent
    //serialization. Swap named members; the shape owns its vector separately.
#define EXCHANGE(member,type) {type n=member;member=other.member;other.member=n;}
    EXCHANGE(ready,bool);EXCHANGE(type,int);EXCHANGE(variant,int);EXCHANGE(subtype,int);EXCHANGE(frameCount,int);EXCHANGE(initialFrame,int);EXCHANGE(state,int);
    EXCHANGE(native168,bool);EXCHANGE(native169,bool);EXCHANGE(native16a,bool);EXCHANGE(native16b,bool);EXCHANGE(removed,bool);EXCHANGE(native16e,bool);EXCHANGE(flagsLow,unsigned int);EXCHANGE(flagsHigh,unsigned int);
    EXCHANGE(native18,unsigned int);EXCHANGE(native1c,unsigned int);EXCHANGE(native34c,int);EXCHANGE(native350,int);
    EXCHANGE(gridContactKnown,bool);EXCHANGE(gridContact,bool);EXCHANGE(contactVelocity,NativePoint);EXCHANGE(contactNormal,NativePoint);
    EXCHANGE(gridCollisionClass,int);EXCHANGE(entityCollisionClass,int);EXCHANGE(renderDepth,int);EXCHANGE(initialSeed,unsigned int);EXCHANGE(random,NativeRng);
    EXCHANGE(position,NativePoint);EXCHANGE(velocity,NativePoint);EXCHANGE(previousPosition,NativePoint);EXCHANGE(previousVelocity,NativePoint);
    EXCHANGE(shadowScale,float);EXCHANGE(friction,float);EXCHANGE(mass,float);EXCHANGE(collisionDamage,float);EXCHANGE(hitPoints,float);EXCHANGE(maxHitPoints,float);
    EXCHANGE(native2a0,float);EXCHANGE(native2ec,float);EXCHANGE(native2fc,float);
    EXCHANGE(entityToken,unsigned int);EXCHANGE(appliedFriction,float);EXCHANGE(previousTime,float);EXCHANGE(time,float);EXCHANGE(native304,float);EXCHANGE(native308,float);
    EXCHANGE(native34,unsigned int);EXCHANGE(native38,unsigned int);EXCHANGE(parentToken,unsigned int);EXCHANGE(childToken,unsigned int);
    EXCHANGE(native26c,int);EXCHANGE(native27c,int);EXCHANGE(native340,int);EXCHANGE(native360,int);EXCHANGE(native264,NativePoint);EXCHANGE(boundStatus,bool);EXCHANGE(damagePending,bool);
    for(int i=0;i<20;++i){EXCHANGE(statusTimers[i],int);}
#undef EXCHANGE
    tintRecords.swap(other.tintRecords);damageQueue.swap(other.damageQueue);
    collision.Swap(other.collision);
}
unsigned int NativeEntityBody::StateHash() const{
    unsigned int h=2166136261U;Fold(h,ready?1:0);Fold(h,type);Fold(h,variant);Fold(h,subtype);Fold(h,frameCount);Fold(h,initialFrame);Fold(h,state);
    Fold(h,native168);Fold(h,native169);Fold(h,native16a);Fold(h,native16b);Fold(h,removed);Fold(h,native16e);Fold(h,flagsLow);Fold(h,flagsHigh);
    Fold(h,native18);Fold(h,native1c);Fold(h,native34c);Fold(h,native350);
    Fold(h,gridContactKnown);if(gridContactKnown){Fold(h,gridContact);Scalar(h,contactNormal.x);Scalar(h,contactNormal.y);
        if(gridContact){Scalar(h,contactVelocity.x);Scalar(h,contactVelocity.y);}}
    Fold(h,gridCollisionClass);Fold(h,entityCollisionClass);Fold(h,initialSeed);Fold(h,random.state);Fold(h,random.index);
    Scalar(h,position.x);Scalar(h,position.y);Scalar(h,velocity.x);Scalar(h,velocity.y);Scalar(h,previousPosition.x);Scalar(h,previousPosition.y);Scalar(h,previousVelocity.x);Scalar(h,previousVelocity.y);
    Scalar(h,shadowScale);Scalar(h,friction);Scalar(h,mass);Scalar(h,collisionDamage);Scalar(h,hitPoints);Scalar(h,maxHitPoints);Scalar(h,native2a0);Scalar(h,native2ec);Scalar(h,native2fc);
    Scalar(h,collision.radius);Scalar(h,collision.xMultiplier);Scalar(h,collision.yMultiplier);Fold(h,(unsigned int)collision.points.size());
    Fold(h,renderDepth);Fold(h,entityToken);Scalar(h,appliedFriction);Scalar(h,previousTime);Scalar(h,time);Scalar(h,native304);Scalar(h,native308);
    Fold(h,native34);Fold(h,native38);Fold(h,parentToken);Fold(h,childToken);
    Fold(h,native26c);Fold(h,native27c);Fold(h,native340);Fold(h,native360);Scalar(h,native264.x);Scalar(h,native264.y);Fold(h,boundStatus);Fold(h,damagePending);
    for(int i=0;i<20;++i)Fold(h,statusTimers[i]);Fold(h,(unsigned int)tintRecords.size());Fold(h,(unsigned int)damageQueue.size());
    for(unsigned int i=0;i<collision.points.size();++i){Scalar(h,collision.points[i].x);Scalar(h,collision.points[i].y);}return h;
}
