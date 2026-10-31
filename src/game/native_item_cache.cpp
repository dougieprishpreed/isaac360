#include "native_item_cache.h"
NativeItemCacheEffect::NativeItemCacheEffect():tearsAdd(0),tearsMul(1),fireRateAdd(0),fireRateMul(1),
    damageAdd(0),damageMul(1),rangeAdd(0),rangeMul(1),speedAdd(0),speedMul(1),
    shotSpeedAdd(0),shotSpeedMul(1),luckAdd(0),flying(-1){}
NativeItemCacheEffect NativeItemCacheEffectFor(int id){
    NativeItemCacheEffect e;
    //Values are the original Repentance cache operations for items whose
    //effect is a stable passive stat change. Conditional items (Crown of
    //Light, Bloody Lust, Purity, ...) stay out of this table until their
    //trigger owners are ported; they must not silently apply unconditionally.
    switch(id){
    case 1:e.tearsAdd=0.7f;break;                              //Sad Onion
    case 2:e.fireRateMul=0.51f;break;                          //The Inner Eye
    case 4:e.damageAdd=0.5f;e.damageMul=1.5f;break;            //Cricket's Head
    case 5:e.damageAdd=1.5f;e.rangeMul=2.0f;e.rangeAdd=1.5f;  //My Reflection
        e.shotSpeedMul=1.6f;e.luckAdd=-1.0f;break;
    case 6:e.tearsAdd=1.5f;e.rangeAdd=-1.5f;break;            //Number One
    case 7:e.damageAdd=1.0f;break;                             //Blood of the Martyr
    case 12:e.damageAdd=0.3f;e.damageMul=1.5f;e.rangeAdd=2.5f;//Magic Mushroom
        e.speedAdd=0.3f;break;
    case 13:e.speedAdd=0.3f;break;                             //The Virus
    case 14:e.speedAdd=0.3f;e.rangeAdd=2.5f;break;             //Roid Rage
    case 20:e.flying=1;break;                                  //Transcendence
    case 27:case 28:e.speedAdd=0.3f;break;                     //Wooden Spoon/Belt
    case 29:case 30:e.rangeAdd=2.5f;break;                     //Mom's Underwear/Heels
    case 31:e.rangeAdd=3.75f;break;                            //Mom's Lipstick
    case 32:e.tearsAdd=0.7f;break;                             //Wire Coat Hanger
    case 46:e.luckAdd=1.0f;break;                              //Lucky Foot
    case 50:case 51:e.damageAdd=1.0f;break;                    //Steven/Pentagram
    case 62:e.damageAdd=0.3f;break;                            //Charm of the Vampire
    case 70:e.damageAdd=1.0f;e.speedAdd=0.2f;break;            //Growth Hormones
    case 71:e.speedAdd=0.3f;e.rangeAdd=2.5f;break;             //Mini Mush
    case 79:e.damageAdd=1.0f;e.speedAdd=0.2f;break;            //The Mark
    case 82:e.speedAdd=0.3f;e.flying=1;break;                  //Lord of the Pit
    case 90:e.damageAdd=1.0f;e.tearsAdd=0.2f;e.speedAdd=-0.2f; //The Small Rock
        break;
    case 101:e.damageAdd=0.3f;e.tearsAdd=0.2f;e.rangeAdd=1.5f;//The Halo
        e.speedAdd=0.3f;break;
    case 110:e.rangeAdd=1.5f;break;                            //Mom's Contacts
    case 119:e.speedAdd=0.3f;break;                            //Blood Bag
    case 120:e.tearsAdd=1.7f;e.speedAdd=0.3f;e.damageAdd=-0.3f; //Odd Mushroom (Thin)
        break;
    case 121:e.damageAdd=1.0f;e.rangeAdd=1.5f;e.speedAdd=-0.2f;//Odd Mushroom (Large)
        break;
    case 129:e.speedAdd=-0.2f;break;                           //Bucket of Lard
    case 130:e.flying=1;break;                                 //A Pony
    case 143:e.speedAdd=0.3f;e.shotSpeedAdd=0.2f;break;        //Speed Ball
    case 153:e.fireRateMul=0.42f;break;                        //Mutant Spider
    case 165:e.damageAdd=1.0f;e.shotSpeedAdd=0.23f;break;      //Cat-o-nine-tails
    case 176:e.shotSpeedAdd=0.16f;break;                       //Stem Cells
    case 182:e.damageAdd=1.0f;e.damageMul=2.3f;e.tearsAdd=-0.4f;//Sacred Heart
        e.shotSpeedAdd=-0.25f;break;
    case 183:e.tearsAdd=0.7f;e.shotSpeedAdd=0.16f;break;       //Tooth Picks
    case 189:e.damageAdd=0.3f;e.tearsAdd=0.2f;e.speedAdd=0.2f; //SMB Super Fan
        e.rangeAdd=2.5f;break;
    case 194:e.shotSpeedAdd=0.16f;break;                       //Magic 8 Ball
    case 197:e.damageAdd=0.5f;e.rangeAdd=1.5f;break;           //Jesus Juice
    case 206:e.damageAdd=1.0f;e.fireRateAdd=0.5f;break;        //Guillotine
    case 208:e.damageAdd=1.0f;break;                           //Champion Belt
    case 213:e.shotSpeedAdd=-0.15f;break;                      //Lost Contact
    case 214:e.rangeAdd=1.5f;break;                            //Anemic
    case 216:e.damageAdd=1.0f;break;                           //Ceremonial Robes
    case 222:e.fireRateAdd=1.0f;break;                         //Anti-Gravity
    case 224:e.fireRateAdd=0.5f;break;                         //Cricket's Body
    case 228:e.fireRateAdd=0.5f;break;                         //Mom's Perfume
    case 230:e.damageAdd=1.5f;e.speedAdd=0.2f;break;           //Abaddon
    case 237:e.damageAdd=1.5f;e.tearsAdd=-0.3f;break;          //Death's Touch
    case 253:e.luckAdd=1.0f;break;                             //Magic Scab
    case 255:e.tearsAdd=0.5f;e.shotSpeedAdd=0.2f;break;        //Screw
    case 259:e.damageAdd=1.0f;break;                           //Dark Matter
    case 300:e.speedAdd=0.25f;break;                           //Aries
    case 306:e.speedAdd=0.2f;break;                            //Sagittarius
    case 307:e.damageAdd=0.5f;e.speedAdd=0.1f;e.rangeAdd=0.75f;//Capricorn
        e.fireRateAdd=0.5f;break;
    case 310:e.damageMul=2.0f;e.fireRateMul=0.66f;e.shotSpeedAdd=-0.5f;//Eve's Mascara
        break;
    case 331:e.damageAdd=0.5f;e.tearsAdd=-0.3f;e.shotSpeedAdd=-0.3f;//Godhead
        break;
    case 339:e.rangeAdd=2.5f;e.shotSpeedAdd=0.16f;break;       //Safety Pin
    case 340:e.speedAdd=0.3f;break;                            //Caffeine Pill
    case 341:e.tearsAdd=0.7f;e.shotSpeedAdd=0.16f;break;       //Torn Photo
    case 342:e.tearsAdd=0.7f;e.shotSpeedAdd=-0.16f;break;      //Blue Cap
    case 343:e.luckAdd=1.0f;break;                             //Latch Key
    case 355:e.rangeAdd=2.5f;e.luckAdd=1.0f;break;             //Mom's Pearls
    case 359:e.damageAdd=1.5f;break;                           //8 Inch Nails
    case 370:e.tearsAdd=0.7f;e.rangeAdd=2.5f;break;            //Mr. Dolly
    case 381:e.tearsAdd=0.7f;break;                            //Eden's Blessing
    case 394:e.tearsAdd=0.7f;break;                            //Marked
    case 397:e.fireRateAdd=1.0f;e.rangeAdd=2.5f;               //Tractor Beam
        e.shotSpeedAdd=0.16f;break;
    case 438:e.tearsAdd=0.75f;break;                           //Binky
    case 440:e.speedAdd=-0.2f;break;                           //Kidney Stone
    case 445:e.damageAdd=0.3f;e.speedAdd=0.1f;break;           //Dog Tooth
    case 455:e.rangeAdd=2.5f;break;                            //Dad's Lost Coin
    case 465:e.tearsAdd=0.35f;break;                           //Analog Stick
    case 513:e.damageAdd=0.1f;break;                           //Bozo
    case 564:e.tearsAdd=0.5f;e.shotSpeedAdd=0.2f;break;        //A Bar of Soap
    case 572:e.damageAdd=1.0f;e.rangeAdd=2.0f;e.shotSpeedAdd=-0.16f;//Eye of the Occult
        break;
    case 590:e.speedAdd=0.4f;break;                            //Mercurius
    case 594:e.speedAdd=-0.3f;break;                           //Jupiter
    case 632:e.luckAdd=2.0f;break;                             //Evil Charm
    case 633:e.damageAdd=2.0f;e.flying=1;break;                //Dogma
    case 669:e.speedAdd=0.2f;e.damageAdd=0.5f;e.tearsAdd=0.5f; //Sausage
        e.rangeAdd=2.5f;e.shotSpeedAdd=0.2f;e.luckAdd=1.0f;break;
    case 730:e.damageAdd=0.75f;e.luckAdd=1.0f;break;           //Glass Eye
    default:break;
    }
    return e;
}
