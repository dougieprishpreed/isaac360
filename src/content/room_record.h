#ifndef GENERIC_ROOM_RECORD_H
#define GENERIC_ROOM_RECORD_H
#include <stddef.h>
enum RoomRecordFormat { RRF_LEGACY,RRF_STB1,RRF_STB2 };
enum RoomRecordResult { RRR_OK,RRR_BAD_FORMAT,RRR_TRUNCATED,RRR_TRAILING };
struct RoomRecordDoor { int x,y; unsigned int existsByte; };
struct RoomRecordOption { unsigned int type,variant,subtype,weightBits; };
struct RoomRecordStack {
    int x,y; unsigned int count;
    const unsigned char* options;
    bool Option(unsigned int index,RoomRecordOption* output) const;
};
struct RoomRecordCursor {
    const unsigned char* data; size_t remaining;
    unsigned int left;
    bool Next(RoomRecordStack* output);
};
// Validated zero-copy view of ONE prepared record, excluding the file header.
// Caller owns the original bytes for the view's lifetime. No native selection,
// actor filtering, marker expansion or float normalization happens here.
struct RoomRecord {
    unsigned int type,variant,subtype,difficulty,weightBits;
    unsigned int width,height,shape,doorCount,stackCount,nameBytes;
    const unsigned char* name;
    const unsigned char* extra; // Nine opaque STB2 bytes, null for other formats.
    RoomRecordFormat format;
    RoomRecordResult Open(const unsigned char* data,size_t size,RoomRecordFormat format);
    bool Door(unsigned int index,RoomRecordDoor* output) const;
    RoomRecordCursor Stacks() const;
private:
    const unsigned char* doors;
    const unsigned char* stacks;
    size_t stackBytes;
};
#endif
