#ifndef GENERIC_NATIVE_STATIC_GRID_H
#define GENERIC_NATIVE_STATIC_GRID_H
#include "native_room_grid.h"

//Ordinary static wall:006c5670 ->006c69a6 ->006d8a40. These are named
//fields, not a native-memory image or a new run-save format.
struct NativeStaticGrid {
    int grid,type,nativeType,variant,state,initializations,collisionClass;
    unsigned int seed,native18,native20,initializedFrame;
    bool initialized;
};
//The save/restore path stores the small factory ID consumed by006c67e0, not
//the STB spawn request retained in NativeStaticGrid::type. These mappings are
//only for factory paths whose0079b1c0 dispatch is established.
int NativeSerializedGridType(int rawType);
int NativeSerializedGridVariant(int rawType,unsigned int rawVariant);
int NativeRawGridType(int serializedType);
int NativeRawGridTypeForVariant(int serializedType,unsigned int variant);
//The type16 room's graphic/color services are not implemented here. No
//global MT is used by the selected1999 wall path; its seed is the main
//spawn pass's current award clone, as passed at0079c380.
bool NativeSelectedWall(int roomType,int grid,unsigned int variant,unsigned int subtype,
    unsigned int selectedSeed,unsigned int frame,NativeStaticGrid*);
//79c4d0 ordinary branch. The caller checks grid ownership BEFORE drawing.
bool NativePerimeterWall(int roomType,int grid,unsigned int seed,unsigned int frame,NativeStaticGrid*);
//007a1130's common saved-record restoration for the reached class4/6/15
//factory family. Virtual Init runs before the native counter increment.
bool NativeRestoreSerializedGrid(int grid,int serializedType,unsigned int variant,unsigned int state,
    unsigned int count,unsigned int seed,unsigned int tail,unsigned int auxiliary,unsigned int frame,
    NativeStaticGrid*);
#endif
