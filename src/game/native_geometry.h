#ifndef GENERIC_NATIVE_GEOMETRY_H
#define GENERIC_NATIVE_GEOMETRY_H
// Topology coordinates are 13x13 level cells, not room tiles or world pixels.
struct NativeCell { int x,y; };
int NativeCellIndex(NativeCell cell);
bool NativeShapeSize(int shape,int* width,int* height);
int NativeShapeFootprint(int shape,NativeCell* cells); // Up to four local cells.
bool NativeShapeDoorAllowed(int shape,int slot,bool includeThinSides=false);
NativeCell NativeShapeDoor(NativeCell origin,int shape,int slot,bool includeThinSides=false);
NativeCell NativeShapeDoorAnchor(NativeCell origin,int shape,int slot);
// Prepared STB coordinates use 13x7 tiles per level cell. Interior L-shape
// edges use the last tile of the missing quadrant, as stored in STB1 records.
bool NativeShapeStbDoor(int shape,int slot,NativeCell* cell);
#endif
