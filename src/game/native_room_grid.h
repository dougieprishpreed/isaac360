#ifndef GENERIC_NATIVE_ROOM_GRID_H
#define GENERIC_NATIVE_ROOM_GRID_H

// Native room tiles, not the13x13 level topology. The largest bordered room
// has28x16 cells. Thin/L shapes use the full rectangular storage; this service
// does not claim to implement their physical walls, collision or camera.
static const int kNativeRoomGridLimit=448;
struct NativeRoomGrid {
    int width,height,stride,rows,cells;
    bool Init(int shape,int preparedWidth,int preparedHeight) {
        // Native shape sizes00bc6d20 and exact prepared STB dimensions.
        static const int sizes[13][2]={{0,0},{13,7},{13,7},{13,7},{13,14},{13,14},
            {26,7},{26,7},{26,14},{26,14},{26,14},{26,14},{26,14}};
        if(shape<1 || shape>12 || preparedWidth!=sizes[shape][0] || preparedHeight!=sizes[shape][1]) return false;
        width=preparedWidth;height=preparedHeight;stride=width+2;rows=height+2;cells=stride*rows;return true;
    }
    //007a1301..007a130e flattens signed STB coordinates without clamping axes.
    // Some actual prepared data wraps x into a different row. Preserve that
    // linear index; only reject indices outside the bounded room storage.
    int StbIndex(int x,int y) const {
        if(x<-32768 || x>32767 || y<-32768 || y>32767) return -1;
        int index=(y+1)*stride+x+1;return index>=0 && index<cells?index:-1;
    }
    bool Tile(int index,int* x,int* y) const {
        if(!x || !y || index<0 || index>=cells) return false;
        *x=index%stride-1;*y=index/stride-1;return true;
    }
    //007bd550 scalar SSE rounding, then CVTTSS2SI (truncation, not floor).
    int PositionIndex(float x,float y) const {
        volatile float sx=x-40.0f;sx=sx/40.0f;sx=sx+0.5f;
        volatile float sy=y-120.0f;sy=sy/40.0f;sy=sy+0.5f;
        // Negative fractions in(-1,0) truncate to cell0 in the saved code.
        // These comparisons also reject NaN/infinity before an unsafe cast.
        if(!(sx>-1 && sx<stride && sy>-1 && sy<rows)) return -1;
        return (int)sy*stride+(int)sx;
    }
    //007bd7c0: native world origin40/120; caller retains its own camera origin.
    bool Position(int index,float* x,float* y) const {
        if(!x || !y || index<0 || index>=cells) return false;
        *x=(float)(index%stride)*40.0f+40.0f;
        *y=(float)(index/stride)*40.0f+120.0f;return true;
    }
};
#endif
