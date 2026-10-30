#ifndef GENERIC_NATIVE_SAVE_WRITER_H
#define GENERIC_NATIVE_SAVE_WRITER_H
#include "native_persistence.h"
class NativeProfileStorage;
//Original GSR0142 byte order and649d30 checksum, independent of host endianness.
//A partial encoder is never published as a save file.
class NativeSaveWriter {
public:
    std::vector<unsigned char> data;
    unsigned int checksum,nextAddress,missingOffset,indeterminateBytes;
    bool valid;
    NativeSaveWriter();
    void Value(unsigned int,unsigned int bytes);
    bool Field(const NativePersistentFields&,unsigned int offset,unsigned int bytes,unsigned int pc);
    bool Fields(const NativePersistentFields&,unsigned int first,unsigned int count,unsigned int pc);
    bool PackedFlags(const NativePersistentFields&,const unsigned int*,unsigned int count,unsigned int pc);
    void Words(const std::vector<unsigned int>&,unsigned int countBytes=4);
    void Bytes(const std::vector<unsigned char>&);
    void Bits(const std::vector<bool>&);
    bool Fail(unsigned int pc,unsigned int offset=0);
    bool Player(const NativePlayerPersistent&);
    bool Room(const NativeDescriptorPersistent&);
    bool Entity(const NativePersistentFields&);
    bool Pools(const NativeItemPoolSnapshot&);
    bool InitialGame(NativeGamePersistent&);
    bool Profile(const NativeProfile&,const NativeProfileStorage&,unsigned int serial);
private:
    void ChecksumByte(unsigned char);
};
#endif
