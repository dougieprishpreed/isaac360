#ifndef GENERIC_NATIVE_ITEM_CACHE_H
#define GENERIC_NATIVE_ITEM_CACHE_H
//Maintained numeric stat-cache effects for passive collectibles.
//
//The original executable stores these deltas in the item cache operator
//(00716840) rather than in items.xml; items.xml exposes only the cache flag
//names. This table records the values that operator applies for the common
//original items, expressed in the same units as NativePlayerStats:
//  tearsAdd      -> Tears stat (before the 30/(tears+1)-1 delay conversion)
//  fireRateAdd   -> direct fire-rate addition after the delay expression
//  fireRateMul   -> direct fire-rate multiplier
//  damageAdd/Mul, rangeAdd/Mul, speedAdd/Mul, shotSpeedAdd/Mul, luckAdd
//-1 flying means "unchanged"; 0/1 clear/set flight.
struct NativeItemCacheEffect {
    float tearsAdd,tearsMul;
    float fireRateAdd,fireRateMul;
    float damageAdd,damageMul;
    float rangeAdd,rangeMul;
    float speedAdd,speedMul;
    float shotSpeedAdd,shotSpeedMul;
    float luckAdd;
    int flying;
    NativeItemCacheEffect();
};
NativeItemCacheEffect NativeItemCacheEffectFor(int id);
#endif
