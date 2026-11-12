#ifndef ISAAC360_PORT_DRAW_H
#define ISAAC360_PORT_DRAW_H
// One frame's sprites as the shared presentation (presentation.cpp) builds them, in draw order, for the one
// renderer (src/render/sprite_vk.cpp) on Windows and the console. Positions are output pixels (1280x720),
// texture coordinates normalised. Colours are R, G, B, A bytes in memory order, so they mean the same on the
// big-endian console; add is the original sprite shader's additive colour (src/render/shaders/sprite.frag).
#include <vector>

enum PortTexture { PT_FRONTEND = 0, PT_GAMEPLAY, PT_COUNT };

struct PortSpriteVertex {
    float x, y, u, v;
    unsigned char color[4];
    float add[4];
};

struct PortDrawRun {
    int texture;
    unsigned int first, count;         // vertices; three a triangle
};

struct PortDrawList {
    unsigned int clearArgb;            // the frame's background, D3DCOLOR order
    std::vector<PortSpriteVertex> vertices;
    std::vector<PortDrawRun> runs;
    void Clear(unsigned int argb) {
        clearArgb = argb;
        vertices.clear();
        runs.clear();
    }
    // corners: top-left, top-right, bottom-right, bottom-left
    void Quad(int texture, const float* xy, const float* uv, unsigned int argb, const float* add = 0) {
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        if (runs.empty() || runs.back().texture != texture) {
            PortDrawRun r = {texture, (unsigned int)vertices.size(), 0};
            runs.push_back(r);
        }
        for (int i = 0; i < 6; ++i) {
            const int j = order[i];
            PortSpriteVertex v;
            v.x = xy[j * 2];
            v.y = xy[j * 2 + 1];
            v.u = uv[j * 2];
            v.v = uv[j * 2 + 1];
            v.color[0] = (unsigned char)(argb >> 16);
            v.color[1] = (unsigned char)(argb >> 8);
            v.color[2] = (unsigned char)argb;
            v.color[3] = (unsigned char)(argb >> 24);
            v.add[0] = add ? add[0] : 0.0f;
            v.add[1] = add ? add[1] : 0.0f;
            v.add[2] = add ? add[2] : 0.0f;
            v.add[3] = 0.0f;
            vertices.push_back(v);
        }
        runs.back().count += 6;
    }
};

#endif
