// The frame as both platforms show it: frontend scenes from the prepared original menu animations, gameplay
// from the prepared gameplay atlas. Moved from the XDK adapter (the most complete of the two old renderers);
// it only builds a draw list (port_draw.h), so the Windows binary and the console draw the same sprites.
#include "port_app.h"
#include "../game/native_room_view.h"
#include "../game/native_room_background.h"
#include "../game/native_door_resources.h"
#include "frontend_assets.h"
#include "gameplay_atlas_data.h"
#include <math.h>
#include <string.h>

namespace {
PortApp* gApp = 0;
PortDrawList* gOut = 0;

const float kFrontendScale = 1280.0f / 480.0f;
// The native room view is authored in a 480x270 logical viewport. Gameplay uses that same inherited
// presentation scale.
const float kGameScale = kFrontendScale;

float Bits(unsigned int u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

unsigned int Modulate(unsigned int a, unsigned int b) {
    unsigned int color = 0;
    for (int shift = 0; shift < 32; shift += 8) color |= ((((a >> shift) & 255) * ((b >> shift) & 255) / 255) << shift);
    return color;
}

// Frontend atlas quads: corners in output pixels (TL, TR, BR, BL), half-texel inset as the XDK shader sampled.
void Quad(const float* xy, float tx, float ty, float tw, float th, unsigned int color, const float* add = 0) {
    const float W = (float)kAtlasWidth, H = (float)kAtlasHeight;
    const float uv[8] = {(tx + 0.5f) / W, (ty + 0.5f) / H, (tx + tw - 0.5f) / W, (ty + 0.5f) / H,
                         (tx + tw - 0.5f) / W, (ty + th - 0.5f) / H, (tx + 0.5f) / W, (ty + th - 0.5f) / H};
    gOut->Quad(PT_FRONTEND, xy, uv, color, add);
}

// Gameplay quads: corners in the 480x270 view (TL, TR, BL, BR as the room view lists them), atlas pixels.
void TexQuad(const float* xy, const float* uv, unsigned int color) {
    static const int order[4] = {0, 1, 3, 2};
    float sxy[8], suv[8];
    for (int i = 0; i < 4; ++i) {
        const int j = order[i];
        sxy[i * 2] = xy[j * 2] * kGameScale;
        sxy[i * 2 + 1] = xy[j * 2 + 1] * kGameScale;
        suv[i * 2] = uv[j * 2] / (float)kGameplayAtlasWidth;
        suv[i * 2 + 1] = uv[j * 2 + 1] / (float)kGameplayAtlasHeight;
    }
    gOut->Quad(PT_GAMEPLAY, sxy, suv, color);
}

void TexRect(float x, float y, float w, float h, float sx, float sy, float sw, float sh, unsigned int color = 0xffffffff) {
    const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    const float uv[8] = {sx, sy, sx + sw, sy, sx, sy + sh, sx + sw, sy + sh};
    TexQuad(xy, uv, color);
}

void TexRectRotated(float cx, float cy, float x, float y, float w, float h, float sx, float sy, float sw, float sh,
                    float degrees, unsigned int color = 0xffffffff) {
    const float radians = degrees * 3.14159265f / 180.0f;
    const float c = cosf(radians), s = sinf(radians);
    const float corners[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    float xy[8];
    for (int i = 0; i < 4; ++i) {
        const float dx = corners[i * 2] - cx, dy = corners[i * 2 + 1] - cy;
        xy[i * 2] = cx + dx * c - dy * s;
        xy[i * 2 + 1] = cy + dx * s + dy * c;
    }
    const float uv[8] = {sx, sy, sx + sw, sy, sx, sy + sh, sx + sw, sy + sh};
    TexQuad(xy, uv, color);
}

void TexRectFlip(float x, float y, float w, float h, float sx, float sy, float sw, float sh, bool flipX, bool flipY,
                 unsigned int color = 0xffffffff) {
    const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    float uv[8] = {sx, sy, sx + sw, sy, sx, sy + sh, sx + sw, sy + sh};
    if (flipX) {
        float t = uv[0]; uv[0] = uv[2]; uv[2] = t;
        t = uv[4]; uv[4] = uv[6]; uv[6] = t;
    }
    if (flipY) {
        float t = uv[1]; uv[1] = uv[5]; uv[5] = t;
        t = uv[3]; uv[3] = uv[7]; uv[7] = t;
    }
    TexQuad(xy, uv, color);
}

bool DoorAtlasRect(const std::string& path, int* ox, int* oy) {
    const char* p = path.c_str();
    if (strstr(p, "door_12_cellardoor")) { *ox = kGameplayDoorcellarX; *oy = kGameplayDoorcellarY; return true; }
    if (strstr(p, "door_00_shopdoor")) { *ox = kGameplayDoorshopX; *oy = kGameplayDoorshopY; return true; }
    if (strstr(p, "door_02_treasureroomdoor")) { *ox = kGameplayDoortreasureX; *oy = kGameplayDoortreasureY; return true; }
    if (strstr(p, "door_04_selfsacrificeroomdoor")) { *ox = kGameplayDoorsacrificeX; *oy = kGameplayDoorsacrificeY; return true; }
    if (strstr(p, "door_01_normaldoor")) { *ox = kGameplayDoornormalX; *oy = kGameplayDoornormalY; return true; }
    return false;
}

bool MinimapAtlasRect(const NativeSpriteAsset* asset, int* ox, int* oy) {
    if (!asset || !asset->sheetCount || !asset->sheets) return false;
    const char* p = asset->sheets[0].path;
    if (strstr(p, "minimap1.png")) { *ox = kGameplayMinimap1X; *oy = kGameplayMinimap1Y; return true; }
    if (strstr(p, "minimap2.png")) { *ox = kGameplayMinimap2X; *oy = kGameplayMinimap2Y; return true; }
    if (strstr(p, "minimap_icons.png")) { *ox = kGameplayMinimapiconsX; *oy = kGameplayMinimapiconsY; return true; }
    return false;
}

const NativeSpriteFrame* MinimapFrame(const NativeSpriteAsset* asset, const char* animation, int frame) {
    if (!asset || !animation) return 0;
    for (unsigned int i = 0; i < asset->clipCount; ++i) {
        const NativeSpriteClip& clip = asset->clips[i];
        if (strcmp(clip.name, animation) != 0) continue;
        for (unsigned int j = 0; j < clip.layerCount; ++j) {
            const NativeSpriteTrack& track = clip.layers[j];
            if (!track.count || !track.frames) continue;
            int at = frame;
            if (at < 0) at = 0;
            if ((unsigned int)at >= track.count) at = (int)track.count - 1;
            return track.frames + at;
        }
        return 0;
    }
    return 0;
}

bool BackdropAtlasRect(int id, int* ox, int* oy) {
    switch (id) {
    case 1: *ox = kGameplayBackdrop1X; *oy = kGameplayBackdrop1Y; return true;
    case 2: *ox = kGameplayBackdrop2X; *oy = kGameplayBackdrop2Y; return true;
    case 4: *ox = kGameplayBackdrop4X; *oy = kGameplayBackdrop4Y; return true;
    case 5: *ox = kGameplayBackdrop5X; *oy = kGameplayBackdrop5Y; return true;
    case 7: *ox = kGameplayBackdrop7X; *oy = kGameplayBackdrop7Y; return true;
    case 8: *ox = kGameplayBackdrop8X; *oy = kGameplayBackdrop8Y; return true;
    case 10: *ox = kGameplayBackdrop10X; *oy = kGameplayBackdrop10Y; return true;
    case 11: *ox = kGameplayBackdrop11X; *oy = kGameplayBackdrop11Y; return true;
    case 14: *ox = kGameplayBackdrop14X; *oy = kGameplayBackdrop14Y; return true;
    case 15: *ox = kGameplayBackdrop15X; *oy = kGameplayBackdrop15Y; return true;
    case 16: *ox = kGameplayBackdrop16X; *oy = kGameplayBackdrop16Y; return true;
    case 17: *ox = kGameplayBackdrop17X; *oy = kGameplayBackdrop17Y; return true;
    default: return false;
    }
}

float RoomCameraX(const NativeRoomBackground& bg) { return (480.0f - (bg.right + bg.left)) * 0.5f; }

// The current room's view mapping: world (room space) to the 480x270 view.
struct RoomMap {
    NativeWorldResident* live;
    const NativeRoomBackground* background;
    const NativeRoomSpace* space;
    float roomW, worldW, roomH, worldH;
    float X(float wx) const { return background->left + (wx - space->left) * roomW / worldW + gApp->roomCameraX; }
    float Y(float wy) const { return background->top + (wy - space->top) * roomH / worldH; }
};

bool CurrentRoom(RoomMap* m) {
    PortNormalRuntime& rt = gApp->runtime;
    if (rt.world.currentList < 0 || rt.world.currentList >= kNativeWorldRoomLimit) return false;
    NativeWorldResident& live = rt.world.resident[rt.world.currentList];
    if (!live.room || !live.view || !live.view->background.ready) return false;
    m->live = &live;
    m->background = &live.view->background;
    m->space = &live.room->space;
    m->roomW = m->background->right - m->background->left;
    m->worldW = m->space->right - m->space->left;
    m->roomH = m->background->bottom - m->background->top;
    m->worldH = m->space->bottom - m->space->top;
    return m->worldW > 0 && m->worldH > 0;
}

void DrawBackground() {
    PortNormalRuntime& rt = gApp->runtime;
    if (rt.world.currentList < 0 || rt.world.currentList >= kNativeWorldRoomLimit) return;
    NativeWorldResident& live = rt.world.resident[rt.world.currentList];
    // Transitioned residents are constructed by EnterLinked without the initial Room::Init presentation block.
    // Prepare the same original background commands lazily; this never advances simulation or RNG.
    if (!live.view) live.view = new NativeRoomView;
    if (live.room && !live.view->background.ready) {
        const NativePlacedRoom* descriptor = rt.world.CurrentDescriptor();
        if (descriptor) live.view->background.Prepare(*live.room, live.backdrop, descriptor->seeds.spawn);
    }
    if (!live.view->background.ready || !live.view->background.definition) return;
    const NativeRoomBackground& background = live.view->background;
    gApp->roomCameraX = RoomCameraX(background);
    int ox, oy;
    if (!BackdropAtlasRect(background.definition->id, &ox, &oy)) return;
    // Floor and wall commands both address the original backdrop sheet. The floor-variant region is part of
    // that same 520x468 sheet.
    for (unsigned int pass = 0; pass < 2; ++pass) {
        const std::vector<NativeBackgroundQuad>& list = pass ? background.wall : background.floor;
        for (unsigned int i = 0; i < list.size(); ++i) {
            const NativeBackgroundQuad& q = list[i];
            float xy[8], uv[8];
            for (int k = 0; k < 4; ++k) {
                xy[k * 2] = q.destination[k].x + gApp->roomCameraX;
                xy[k * 2 + 1] = q.destination[k].y;
                uv[k * 2] = q.source[k].x + ox;
                uv[k * 2 + 1] = q.source[k].y + oy;
            }
            TexQuad(xy, uv, 0xffffffff);
        }
    }
}

void DrawPlayer() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (rt.playerBody.removed || !rt.playerSprites.bodyReady || !CurrentRoom(&m)) return;
    NativeSprite& sprite = rt.playerSprites.sprites[0];
    if (sprite.layerSheets.size() <= 1) return;
    // The prepared atlas currently contains the fresh-profile Isaac costume. Other sheets remain explicit
    // rendering stops rather than wrong art.
    if (strstr(sprite.layerSheets[1].path.c_str(), "character_001_isaac") == 0) return;
    const float px = m.X(rt.playerBody.position.x), py = m.Y(rt.playerBody.position.y);
    const NativeSpriteFrame* frames[2] = {sprite.LayerFrame(1), sprite.LayerFrame(4, true)};
    for (int i = 0; i < 2; ++i) {
        const NativeSpriteFrame* f = frames[i];
        if (!f || !f->visible) continue;
        const float cropX = Bits(f->geometry[0]), cropY = Bits(f->geometry[1]);
        const float w = Bits(f->geometry[2]), h = Bits(f->geometry[3]);
        const float offX = Bits(f->geometry[4]), offY = Bits(f->geometry[5]);
        float scaleX = Bits(f->geometry[6]), scaleY = Bits(f->geometry[7]);
        const float pivotX = Bits(f->geometry[8]), pivotY = Bits(f->geometry[9]);
        if (w <= 0 || h <= 0) continue;
        if (scaleX == 0 || scaleY == 0) scaleX = scaleY = 1;
        const float x = px + (offX - pivotX) * fabsf(scaleX), y = py + (offY - pivotY) * fabsf(scaleY);
        TexRectFlip(x, y, w * fabsf(scaleX), h * fabsf(scaleY), cropX + (float)kGameplayPlayerX,
                    cropY + (float)kGameplayPlayerY, w, h, scaleX < 0, scaleY < 0);
    }
}

void DrawDoors() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m) || !m.live->doors) return;
    NativeWorldResident& live = *m.live;
    bool needsVisuals = !live.doorVisuals;
    if (live.doorVisuals)
        for (int slot = 0; slot < 8; ++slot)
            if ((live.doors->attached & (1U << slot)) && !live.doorVisuals[slot].ready &&
                !live.doorVisuals[slot].unsupportedTarget) {
                needsVisuals = true;
                break;
            }
    if (needsVisuals)
        rt.world.PrepareDoorVisuals(rt.run.context.stage, rt.run.context.stageType, rt.run.context.nativeMode,
                                    rt.run.context.challenge, rt.playerState);
    if (!live.doorVisuals) return;
    for (int slot = 0; slot < 8; ++slot) {
        if (!(live.doors->attached & (1U << slot))) continue;
        NativeDoorVisual& visual = live.doorVisuals[slot];
        if (!visual.ready || !visual.main.asset || visual.main.layerSheets.empty()) continue;
        int ax = 0, ay = 0;
        if (!DoorAtlasRect(visual.main.layerSheets[0].path, &ax, &ay)) continue;
        float wx, wy;
        if (!live.room->geometry.Position(live.doors->actors[slot].grid, &wx, &wy)) continue;
        const float px = m.X(wx + 20.0f) + visual.main.offset.x;
        const float py = m.Y(wy + 20.0f) + visual.main.offset.y;
        for (unsigned int i = 0; i < visual.main.asset->layerCount; ++i) {
            const NativeSpriteFrame* f = visual.main.LayerFrame(visual.main.asset->layers[i].id);
            if (!f || !f->visible) continue;
            const float cropX = Bits(f->geometry[0]), cropY = Bits(f->geometry[1]);
            const float w = Bits(f->geometry[2]), h = Bits(f->geometry[3]);
            const float offX = Bits(f->geometry[4]), offY = Bits(f->geometry[5]);
            float scaleX = Bits(f->geometry[6]), scaleY = Bits(f->geometry[7]);
            const float pivotX = Bits(f->geometry[8]), pivotY = Bits(f->geometry[9]);
            if (w <= 0 || h <= 0) continue;
            if (scaleX == 0 || scaleY == 0) scaleX = scaleY = 1;
            const float x = px + (offX - pivotX) * fabsf(scaleX), y = py + (offY - pivotY) * fabsf(scaleY);
            TexRectRotated(px, py, x, y, w * fabsf(scaleX), h * fabsf(scaleY), cropX + (float)ax, cropY + (float)ay, w, h,
                           visual.main.rotation);
        }
    }
}

void DrawRocks() {
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    const NativeRoomState* room = m.live->room;
    const float cellW = 40.0f * m.roomW / m.worldW, cellH = 40.0f * m.roomH / m.worldH;
    for (int i = 0; i < room->staticGridCount; ++i) {
        const NativeStaticGrid& g = room->staticGrids[i];
        if (g.type != 1000 && g.type != 1001 && g.type != 1002 && g.type != 1008 && g.type != 1010 && g.type != 1500 &&
            g.type != 1400)
            continue;
        float wx, wy;
        if (!room->geometry.Position(g.grid, &wx, &wy)) continue;
        int sheetX = kGameplayRocksbasementX, sheetY = kGameplayRocksbasementY, sx = 0, sy = 0;
        if (g.type == 1002) sy = 32;
        else if (g.type == 1500) { sheetX = kGameplayPoopsheetX; sheetY = kGameplayPoopsheetY; }
        else if (g.type == 1400) { sheetX = kGameplayFireplacesheetX; sheetY = kGameplayFireplacesheetY; }
        else sx = (g.variant & 3) * 32;
        TexRect(m.X(wx), m.Y(wy), cellW, cellH, (float)(sheetX + sx), (float)(sheetY + sy), 32, 32);
    }
}

bool EnemyWalkRect(int type, int variant, int* sx, int* sy, int* frames, int* fw, int* fh) {
    for (unsigned int i = 0; i < sizeof(kGameplayEnemySprites) / sizeof(kGameplayEnemySprites[0]); ++i) {
        const GameplayEnemySprite& s = kGameplayEnemySprites[i];
        if (s.type != type || s.variant != variant) continue;
        *sx = s.x; *sy = s.y; *frames = s.frames; *fw = s.frameW; *fh = s.frameH;
        return true;
    }
    return false;
}

void DrawTrapdoor() {
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    const NativeRoomState* room = m.live->room;
    if (room->trapdoorState == 0) return;
    int frame = room->trapdoorState == 1 ? room->trapdoorTimer / 5 : 5;
    if (frame < 0) frame = 0;
    if (frame > 5) frame = 5;
    TexRect(m.X(room->trapdoorX) - 32, m.Y(room->trapdoorY) - 32, 64, 64, (float)(kGameplayTrapdooropenX + frame * 64),
            (float)kGameplayTrapdooropenY, 64, 64);
}

void DrawFamiliars() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    for (unsigned int i = 0; i < rt.familiars.active.size(); ++i) {
        const NativeFamiliar& f = rt.familiars.active[i];
        int sx = 0, sy = 0, frames = 0;
        if (f.item == 8) { sx = kGameplayBrotherbobbywalkX; sy = kGameplayBrotherbobbywalkY; frames = kGameplayBrotherbobbywalkFrames; }
        else if (f.item == 10) { sx = kGameplayFlywalkX; sy = kGameplayFlywalkY; frames = kGameplayFlywalkFrames; }
        else if (f.item == 117) { sx = kGameplayDeadbirdwalkX; sy = kGameplayDeadbirdwalkY; frames = kGameplayDeadbirdwalkFrames; }
        else continue;
        if (frames <= 0) continue;
        const int frame = ((int)(f.time / 6)) % frames;
        TexRect(m.X(f.position.x) - 32, m.Y(f.position.y) - 48, 64, 64, (float)(sx + frame * 64), (float)sy, 64, 64);
    }
}

void DrawEnemies() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    NativeWorldResident& live = *m.live;
    if (gApp->loggedEnemyList != rt.world.currentList) {
        gApp->loggedEnemyList = rt.world.currentList;
        for (int i = 0; i < live.enemies.count; ++i)
            if (NativeEnemy* e = live.enemies.entries[i])
                PortLog("enemy type=%d variant=%d pos=(%.1f,%.1f) ready=%d dead=%d", e->type, e->body.variant,
                        e->body.position.x, e->body.position.y, e->ready ? 1 : 0, e->dead ? 1 : 0);
    }
    for (int i = 0; i < live.enemies.count; ++i) {
        NativeEnemy* enemy = live.enemies.entries[i];
        if (!enemy || !enemy->ready || enemy->dead) continue;
        int sx = 0, sy = 0, frames = 0, fw = 64, fh = 64;
        if (!EnemyWalkRect(enemy->type, enemy->body.variant, &sx, &sy, &frames, &fw, &fh) || frames <= 0) continue;
        const int frame = ((int)(enemy->body.time / 6)) % frames;
        TexRect(m.X(enemy->body.position.x) - fw * 0.5f, m.Y(enemy->body.position.y) - fh * 0.75f, (float)fw, (float)fh,
                (float)(sx + frame * fw), (float)sy, (float)fw, (float)fh);
    }
}

void DrawDeathFx() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    for (unsigned int i = 0; i < rt.deathFx.active.size(); ++i) {
        const NativeDeathFx& fx = rt.deathFx.active[i];
        int sx = 0, sy = 0, frames = 0, fw = 64, fh = 64;
        if (!EnemyWalkRect(fx.type, fx.variant, &sx, &sy, &frames, &fw, &fh) || frames <= 0) continue;
        float scale = 1.0f - fx.time / 30.0f;
        if (scale < 0.15f) scale = 0.15f;
        const int frame = ((int)(fx.time / 3)) % frames;
        const unsigned int alpha = ((unsigned int)(255.0f * scale) & 255U) << 24;
        TexRect(m.X(fx.position.x) - fw * scale * 0.5f, m.Y(fx.position.y) - fh * scale * 0.75f, fw * scale, fh * scale,
                (float)(sx + frame * fw), (float)sy, (float)fw, (float)fh, alpha | 0x00ffffffU);
    }
}

void DrawPickups() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    NativeWorldResident& live = *m.live;
    if (gApp->loggedPickupList != rt.world.currentList) {
        gApp->loggedPickupList = rt.world.currentList;
        for (int i = 0; i < live.pickups.count; ++i)
            if (NativePickup* p = live.pickups.entries[i])
                PortLog("pickup variant=%d subtype=%d pos=(%.1f,%.1f)", p->variant, p->subtype, p->position.x, p->position.y);
    }
    for (int i = 0; i < live.pickups.count; ++i) {
        NativePickup* p = live.pickups.entries[i];
        if (!p || !p->ready || p->collected) continue;
        int sx, sy;
        if (p->variant == 10) { sx = kGameplayPickupheartX; sy = kGameplayPickupheartY; }
        else if (p->variant == 20) { sx = kGameplayPickupcoinX; sy = kGameplayPickupcoinY; }
        else if (p->variant == 30) { sx = kGameplayPickupkeyX; sy = kGameplayPickupkeyY; }
        else if (p->variant == 40) { sx = kGameplayPickupbombX; sy = kGameplayPickupbombY; }
        else continue;
        TexRect(m.X(p->position.x) - 8, m.Y(p->position.y) - 8, 16, 16, (float)sx, (float)sy, 32, 32);
    }
}

void DrawProjectiles() {
    PortNormalRuntime& rt = gApp->runtime;
    RoomMap m;
    if (!CurrentRoom(&m)) return;
    for (unsigned int i = 0; i < rt.projectiles.active.size(); ++i) {
        const NativeProjectile& p = rt.projectiles.active[i];
        TexRect(m.X(p.position.x) - 18, m.Y(p.position.y) - 18, 36, 36, (float)(kGameplayTearsX + 32),
                (float)(kGameplayTearsY + 32), 32, 32);
    }
    for (unsigned int i = 0; i < rt.enemyShots.active.size(); ++i) {
        const NativeEnemyShot& p = rt.enemyShots.active[i];
        TexRect(m.X(p.position.x) - 14, m.Y(p.position.y) - 14, 28, 28, (float)(kGameplayTearsX + 128),
                (float)(kGameplayTearsY + 64), 32, 32);
    }
}

void DrawHud() {
    PortNormalRuntime& rt = gApp->runtime;
    if (!rt.playerState.ready) return;
    float x = 10, y = 10;
    const int red = rt.playerState.redHearts, containers = rt.playerState.maxRedHearts / 2;
    for (int i = 0; i < containers; ++i) {
        float sx = 32, sy = 0;
        if (red >= 2 * (i + 1)) sx = 0;
        else if (red == 2 * i + 1) sx = 16;
        TexRect(x, y, 16, 16, sx + kGameplayHeartsX, sy + kGameplayHeartsY, 16, 16);
        x += 16;
    }
    const int soul = rt.playerState.soulHearts;
    for (int i = 0; i < soul / 2; ++i) {
        TexRect(x, y, 16, 16, (float)kGameplayHeartsX, (float)(kGameplayHeartsY + 16), 16, 16);
        x += 16;
    }
    if (soul & 1) TexRect(x, y, 16, 16, (float)(kGameplayHeartsX + 16), (float)(kGameplayHeartsY + 16), 16, 16);
}

void DrawMinimap() {
    PortNormalRuntime& rt = gApp->runtime;
    if (!rt.minimaps.ready) return;
    NativeMinimap& map = rt.minimaps.maps[0];
    if (!map.ready || map.draws.empty() || map.textureWidth <= 0 || map.textureHeight <= 0) return;
    const float scale = 1.2f;
    const float baseX = 480.0f - map.textureWidth * scale - 8.0f, baseY = 8.0f;
    const NativeSpriteFrame* panel = MinimapFrame(map.sprite.asset, "Frame", 0);
    int ax = 0, ay = 0;
    if (panel && MinimapAtlasRect(map.sprite.asset, &ax, &ay)) {
        const float px = Bits(panel->geometry[0]), py = Bits(panel->geometry[1]);
        const float pw = Bits(panel->geometry[2]), ph = Bits(panel->geometry[3]);
        if (pw > 0 && ph > 0) TexRect(baseX, baseY, pw * scale, ph * scale, px + ax, py + ay, pw, ph, 0xff000000);
    }
    for (unsigned int i = 0; i < map.draws.size(); ++i) {
        const NativeMinimapDraw& draw = map.draws[i];
        const NativeSpriteFrame* f = MinimapFrame(draw.asset, draw.animation, draw.frame);
        if (!f || !f->visible) continue;
        if (!MinimapAtlasRect(draw.asset, &ax, &ay)) continue;
        const float cropX = Bits(f->geometry[0]), cropY = Bits(f->geometry[1]);
        const float w = Bits(f->geometry[2]), h = Bits(f->geometry[3]);
        const float offX = Bits(f->geometry[4]), offY = Bits(f->geometry[5]);
        float sx = Bits(f->geometry[6]), sy = Bits(f->geometry[7]);
        const float pivotX = Bits(f->geometry[8]), pivotY = Bits(f->geometry[9]);
        if (w <= 0 || h <= 0) continue;
        if (sx == 0 || sy == 0) sx = sy = 1;
        const bool isIcon = draw.asset == rt.minimaps.icons.asset;
        // The icon sheet's 7x7 glyph sits at (3,2) inside a 16x16 cell. Center it on the room cell.
        const float x = isIcon ? baseX + (draw.position.x - 4.0f) * scale : baseX + (draw.position.x + (offX - pivotX) * fabsf(sx)) * scale;
        const float y = isIcon ? baseY + (draw.position.y - 3.0f) * scale : baseY + (draw.position.y + (offY - pivotY) * fabsf(sy)) * scale;
        TexRect(x, y, w * fabsf(sx) * scale, h * fabsf(sy) * scale, cropX + ax, cropY + ay, w, h);
    }
}

void Anim(int id, int frame, float x, float y, float scale = kFrontendScale, float angle = 0, const int* itemIcon = 0,
          unsigned int tint = 0xffffffff, bool flipX = false, const float* extraAdd = 0) {
    const FrontendAnimation& a = kAnimations[id];
    if (frame < 0) frame = 0;
    if (a.loop) frame %= a.count;
    else if (frame >= a.count) frame = a.count - 1;
    const FrontendFrame& f = kFrames[a.first + frame];
    float cs = cosf(angle), sn = sinf(angle);
    for (int i = 0; i < f.count; ++i) {
        const FrontendSprite& s = kSprites[f.first + i];
        float xy[8];
        for (int k = 0; k < 4; ++k) {
            float px = s.corners[k * 2] * scale * (flipX ? -1 : 1), py = s.corners[k * 2 + 1] * scale;
            xy[k * 2] = x + px * cs - py * sn;
            xy[k * 2 + 1] = y + px * sn + py * cs;
        }
        const int* icon = s.itemLayer ? itemIcon : 0;
        float add[3];
        for (int c = 0; c < 3; ++c) add[c] = s.add[c] + (extraAdd ? extraAdd[c] : 0);
        Quad(xy, (float)(icon ? icon[0] : s.x), (float)(icon ? icon[1] : s.y), (float)s.w, (float)s.h, Modulate(s.color, tint), add);
    }
}

void Menu() {
    PortFrontend& ui = gApp->ui;
    const float scale = 1280.0f / 480.0f;
    if (ui.scene == UI_TITLE) {
        Anim(AN_Title, ui.tick, 0, 0, scale);
        return;
    }
    if (ui.scene == UI_SLOTS) {
        Anim(AN_SlotsBase, 0, 0, 0, scale);
        const int on[] = {AN_SlotsFile1, AN_SlotsFile2, AN_SlotsFile3, AN_SlotsDelete, AN_SlotsImport};
        const int off[] = {AN_SlotsFile1Off, AN_SlotsFile2Off, AN_SlotsFile3Off, AN_SlotsDeleteOff, AN_SlotsImportOff};
        for (int i = 0; i < 5; ++i) {
            bool selected = i == ui.selection;
            int frame = selected ? ui.slotAnimTick : (ui.slotPrevious == i ? ui.slotAnimTick : 4);
            // 0091c970 colors the Delete object (1,.6,.4) while picking.
            unsigned int tint = i == 3 && ui.slotMode == 1 ? 0xffff9966 : 0xffffffff;
            Anim(selected ? on[i] : off[i], frame, 0, 0, scale, 0, 0, tint);
            if (i < 3) {
                int anchorFrame = frame < 5 ? frame : 4;
                const SlotAnchor& anchor = kSlotAnchors[i][selected ? 0 : 1][anchorFrame];
                Anim(selected ? AN_SlotsDrawingAnimated : AN_SlotsDrawing, selected ? ui.slotAnimTick : 0, anchor.x * scale,
                     anchor.y * scale, scale * anchor.sx, 0, 0, anchor.tint);
            }
        }
        if (ui.slotMode == 2) Anim(ui.slotTick < 10 ? AN_SlotsConfirmIn : AN_SlotsConfirm, ui.slotTick < 10 ? ui.slotTick : ui.slotChoice, 0, 0, scale);
        if (ui.slotMode == 3) Anim(AN_SlotsConfirmOut, ui.slotTick, 0, 0, scale);
        return;
    }
    if (ui.scene == UI_MAIN) {
        Anim(AN_MainBase, 0, 0, 0, scale);
        Anim(AN_MainNew, 0, 0, 0, scale);
        Anim(AN_MainContinue, ui.hasContinue ? 0 : 1, 0, 0, scale);
        Anim(AN_MainChallenges, 0, 0, 0, scale);
        Anim(AN_MainMods, 0, 0, 0, scale, 0, 0, 0xff909090);   // explicit no-mods scope
        Anim(AN_MainStats, 0, 0, 0, scale);
        Anim(AN_MainOptions, 0, 0, 0, scale);
        Anim(AN_MainCursor, ui.selection, 0, 0, scale);
        return;
    }
    if (ui.scene == UI_CHARACTERS) {
        const int* art = kCharacterArt[ui.characterPage][ui.characterChoice];
        const NativeCharacterDef* character = NativeCharacter(ui.characterPage, ui.characterChoice);
        bool unlocked = NativeCharacterUnlocked(ui.profile, ui.characterPage, ui.characterChoice);
        Anim(AN_CharacterBGIdle, 0, 0, 0, scale);
        if (ui.characterPage) Anim(AN_CharacterBGExtra, 0, 0, 0, scale);
        Anim(ui.characterPage ? AN_CharacterBGFace1 : AN_CharacterBGFace0, 0, 0, 0, scale);
        Anim(art[0], 0, 0, 0, scale);
        Anim(art[unlocked ? 1 : 2], 0, 0, 0, scale);
        if (unlocked)
            for (int i = 0; i < 3; ++i)
                if (ui.profile.Award(character->itemAwards[i])) Anim(art[3 + i], 0, 0, 0, scale);
        NativeCharacterPortrait portraits[18];
        int count = NativeCharacterWheel(ui.profile, ui.characterPage, ui.characterChoice, portraits);
        for (int i = 0; i < count; ++i) {
            const NativeCharacterPortrait& p = portraits[i];
            unsigned int rgb = (unsigned int)(p.scale * 255);
            float add[3] = {(1 - p.scale) * 231 / 255, (1 - p.scale) * 221 / 255, (1 - p.scale) * 220 / 255};
            Anim(kCharacterArt[ui.characterPage][p.choice][6], NativeCharacterUnlocked(ui.profile, ui.characterPage, p.choice) ? 0 : 1,
                 p.x * scale, p.y * scale, scale * p.scale, 0, 0, 0xff000000 | (rgb << 16) | (rgb << 8) | rgb, false, add);
        }
        Anim(ui.profile.Award(341) ? AN_DifficultyAll : AN_Difficulty, ui.characterMode, 365 * scale, 120 * scale, scale);
        Anim(AN_CharacterSeed, 0, 350 * scale, 45 * scale, scale);
        Anim(AN_ControllerButtons, 0, 415 * scale, 58 * scale, scale);
    }
}
}

void PortPresent(PortApp& app, PortDrawList& out) {
    gApp = &app;
    gOut = &out;
    const bool gameplay = app.GameplayVisible();
    out.Clear(gameplay ? 0xff000000 : 0xffcccccc);
    if (gameplay) {
        DrawBackground();
        DrawDoors();
        DrawRocks();
        DrawPickups();
        DrawTrapdoor();
        DrawEnemies();
        DrawDeathFx();
        DrawFamiliars();
        DrawPlayer();
        DrawProjectiles();
        DrawHud();
        DrawMinimap();
    } else if (app.AtlasesLoaded()) {
        Menu();
    }
    gApp = 0;
    gOut = 0;
}

void PortApp::BuildFrame(PortDrawList& out) { PortPresent(*this, out); }
