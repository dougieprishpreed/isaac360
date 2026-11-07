#include "port_app.h"
#include "../content/port_data.h"
#include "../game/native_room_view.h"
#include "frontend_assets.h"
#include "gameplay_atlas_data.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------ log
static FILE* gLogFile = 0;
static char gLogPrefix[16] = "PORT";

void PortLogOpen(const char* path, const char* prefix) {
    if (gLogFile) fclose(gLogFile);
    gLogFile = path ? fopen(path, "wt") : 0;
    strncpy(gLogPrefix, prefix ? prefix : "PORT", sizeof(gLogPrefix) - 1);
}

void PortLog(const char* fmt, ...) {
    char line[1200];
    int n = snprintf(line, sizeof(line), "%s ", gLogPrefix);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
    va_end(args);
    line[sizeof(line) - 2] = 0;
    strcat(line, "\n");
    fputs(line, stdout);
    fflush(stdout);
    if (gLogFile) {
        fputs(line, gLogFile);
        fflush(gLogFile);
    }
}

void PortLogClose() {
    if (gLogFile) fclose(gLogFile);
    gLogFile = 0;
}

// ------------------------------------------------------------------------------------------------ options
PortAppOptions::PortAppOptions()
    : seed(0), direct(false), greed(false), continueRun(false), boss(false), layout(false), invulnerable(false),
      teleport(false), rewardTest(false), saveOnExit(false), descend(0), frames(0) {}

bool PortAppOptions::Parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool more = i + 1 < argc;
        if (!strcmp(a, "--seed") && more) seed = (unsigned int)strtoul(argv[++i], 0, 10);
        else if (!strcmp(a, "--frames") && more) frames = atoi(argv[++i]);
        else if (!strcmp(a, "--script") && more) script = argv[++i];
        else if (!strcmp(a, "--direct")) direct = true;
        else if (!strcmp(a, "--greed")) direct = greed = true;
        else if (!strcmp(a, "--continue")) direct = continueRun = true;
        else if (!strcmp(a, "--continue-file") && more) continueFile = argv[++i];
        else if (!strcmp(a, "--boss")) boss = true;
        else if (!strcmp(a, "--layout")) direct = layout = true;
        else if (!strcmp(a, "--invulnerable")) invulnerable = true;
        else if (!strcmp(a, "--teleport-pickups")) teleport = true;
        else if (!strcmp(a, "--reward-test")) rewardTest = true;
        else if (!strcmp(a, "--save")) saveOnExit = true;
        else if (!strcmp(a, "--descend") && more) descend = atoi(argv[++i]);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ app
PortApp::PortApp()
    : frontendAtlasW(0), frontendAtlasH(0), gameplayAtlasW(0), gameplayAtlasH(0), roomCameraX(0), loggedEnemyList(-1), loggedPickupList(-1), m_lastMusicTrack(-1),
      m_frame(0), m_actionFrame(0), m_lastAutosave(0), m_exitCode(0), m_frontendReady(false), m_readyLogged(false),
      m_dyingLogged(false), m_stopped(false), m_lastPrerequisite(0) {
    memset(m_prevActions, 0, sizeof(m_prevActions));
    memset(&m_platform, 0, sizeof(m_platform));
    memset(m_profiles, 0, sizeof(m_profiles));
}

static unsigned int LE32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

// The prepared atlases: 16-byte header (magic, width, height) and RGBA rows.
static bool LoadAtlas(const char* path, const char* magic, unsigned int w, unsigned int h, std::vector<unsigned char>& out) {
    std::vector<unsigned char> data;
    if (!PortDataRead(path, data, 16 + w * h * 4)) {
        PortLog("atlas missing %s", path);
        return false;
    }
    if (data.size() != 16 + (size_t)w * h * 4 || memcmp(&data[0], magic, 8) != 0 || LE32(&data[8]) != w ||
        LE32(&data[12]) != h) {
        PortLog("atlas invalid %s", path);
        return false;
    }
    out.assign(data.begin() + 16, data.end());
    return true;
}

// Scripted pad segments "first:last:keys": WASD = left stick, J/L/I/K = right stick (fire), X = X (bomb),
// Z = A, C = LB, V = B, E = A (menus), Q = Back, N = Start, Y = Y, arrows as the D-pad: U/H/F/G (up/down/left/right).
static void ApplyKey(char c, PortPad& p) {
    switch (c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c) {
    case 'W': p.ly = 32767; break;
    case 'S': p.ly = -32767; break;
    case 'A': p.lx = -32767; break;
    case 'D': p.lx = 32767; break;
    case 'I': p.ry = 32767; break;
    case 'K': p.ry = -32767; break;
    case 'J': p.rx = -32767; break;
    case 'L': p.rx = 32767; break;
    case 'X': p.buttons |= PAD_X; break;
    case 'Z': case 'E': p.buttons |= PAD_A; break;
    case 'C': p.buttons |= PAD_LB; break;
    case 'V': p.buttons |= PAD_B; break;
    case 'Q': p.buttons |= PAD_BACK; break;
    case 'N': p.buttons |= PAD_START; break;
    case 'Y': p.buttons |= PAD_Y; break;
    case 'U': p.buttons |= PAD_UP; break;
    case 'H': p.buttons |= PAD_DOWN; break;
    case 'F': p.buttons |= PAD_LEFT; break;
    case 'G': p.buttons |= PAD_RIGHT; break;
    default: break;
    }
}

bool PortApp::Init(const PortAppOptions& options, const PortPlatformInfo& platform) {
    m_options = options;
    m_platform = platform;
    PortDataMount(platform.dataRoot);
    PortLog("data root=%s packed=%d", platform.dataRoot, PortDataPacked() ? 1 : 0);
    for (const char* p = options.script.c_str(); *p;) {
        Segment s;
        memset(&s.pad, 0, sizeof(s.pad));
        s.first = atoi(p);
        while (*p && *p != ':') ++p;
        if (*p == ':') ++p;
        s.last = atoi(p);
        while (*p && *p != ':') ++p;
        if (*p == ':') ++p;
        while (*p && *p != ';') ApplyKey(*p++, s.pad);
        m_script.push_back(s);
        if (*p == ';') ++p;
    }

    bool atlases = LoadAtlas("port/frontend/atlas.bin", "GATLAS1\0", kAtlasWidth, kAtlasHeight, frontendAtlas);
    frontendAtlasW = kAtlasWidth;
    frontendAtlasH = kAtlasHeight;
    if (!LoadAtlas("port/gameplay/atlas.bin", "GPLAY1\0\0", kGameplayAtlasWidth, kGameplayAtlasHeight, gameplayAtlas))
        atlases = false;
    gameplayAtlasW = (int)kGameplayAtlasWidth;
    gameplayAtlasH = (int)kGameplayAtlasHeight;
    // The frontend only runs over the original inherited resources it was prepared from.
    bool bindings = false;
    if (const GpcResourceIndex* index = PortDataContentIndex("game/file_index.bin")) {
        bindings = true;
        for (int i = 0; i < kOriginalBindingCount && bindings; ++i) {
            GpcEntry entry;
            GpcResult result = index->FindOriginal(kOriginalBindings[i].request, &entry);
            bindings = result == GPC_OK && entry.package == kOriginalBindings[i].package;
            if (!bindings) PortLog("inherited request=%s result=%d unmatched", kOriginalBindings[i].request, result);
        }
    } else {
        PortLog("content index missing");
    }
    m_frontendReady = atlases && bindings;
    PortLog("atlases=%d bindings=%d", atlases ? 1 : 0, bindings ? 1 : 0);

    ui.Init(false);
    runtime.audio = &audio;
    if (audio.Initialize("port/audio")) {
        if (platform.audio) audio.SetBackend(platform.audio, platform.audio->user);
        PortLog("audio initialized device=%d", audio.deviceReady ? 1 : 0);
    }
    if (!ui.BindSaveDirectory(platform.saveDirectory)) PortLog("save directory bind failed %s", platform.saveDirectory);
    unsigned int menuSeed = options.seed ? options.seed : platform.entropy;
    m_menuRandom.Seed(menuSeed ? menuSeed : 1);
    if (options.direct) {
        memset(&ui.profile, 0, sizeof(ui.profile));
        ui.characterPage = 0;
        ui.characterChoice = 1;
        ui.characterMode = 0;
        if (!StartDirect()) m_stopped = true;     // Tick stops; ExitCode says why
    }
    SyncMusic();
    return true;
}

bool PortApp::GameplayVisible() const {
    return runtime.phase == PNR_READY || runtime.phase == PNR_DYING || runtime.phase == PNR_GAME_OVER;
}

NativeSceneFiles PortApp::Files() const {
    NativeSceneFiles files = {"game/file_index.bin", "game/", NativeStageBackdrop(runtime.run.context.stage),
                              runtime.run.context.nativeMode, 0};
    return files;
}

void PortApp::SyncMusic() {
    if (!audio.initialized) return;
    if (ui.music.queuedTrack > 0) {
        ui.music.currentTrack = ui.music.queuedTrack;
        ui.music.queuedTrack = 0;
    }
    const int track = ui.music.currentTrack;
    if (track != m_lastMusicTrack) {
        m_lastMusicTrack = track;
        const NativeMusicDef* definition = NativeMusicDefinition(track);
        if (definition && definition->path && *definition->path) {
            if (audio.PlayMusicSet(definition->intro, definition->path, definition->layerIntro, definition->layer,
                                   definition->loop, ui.music.volume * 1.6f, 0.55f))
                PortLog("music track=%d path=%s", track, definition->path);
            else
                PortLog("music failed track=%d path=%s", track, definition->path);
        } else {
            audio.StopMusic();
        }
    }
    audio.SetMusicVolume(ui.music.volume * 1.6f);
    audio.Update();
}

static float Axis(short value) {
    const float dead = 0.25f;
    float v = value / 32767.0f;
    return fabsf(v) < dead ? 0.0f : v;
}

// The twelve native actions and eight values from one pad (left stick moves, right stick fires).
void PortApp::BuildActions(const PortPad& p, unsigned int frameNumber, NativeActionFrame* frame) {
    frame->Clear(runtime.playerCreation.native153c, runtime.playerBody.entityToken, frameNumber);
    frame->SupplyConnection(true);
    const bool held[12] = {p.lx < -8000, p.lx > 8000, p.ly > 8000, p.ly < -8000,
                           p.rx < -8000, p.rx > 8000, p.ry > 8000, p.ry < -8000,
                           (p.buttons & PAD_X) != 0, (p.buttons & PAD_A) != 0,
                           (p.buttons & PAD_LB) != 0, (p.buttons & PAD_B) != 0};
    const float lx = Axis(p.lx), ly = Axis(p.ly), rx = Axis(p.rx), ry = Axis(p.ry);
    const float values[8] = {lx < 0 ? -lx : 0, lx > 0 ? lx : 0, ly > 0 ? ly : 0, ly < 0 ? -ly : 0,
                             rx < 0 ? -rx : 0, rx > 0 ? rx : 0, ry > 0 ? ry : 0, ry < 0 ? -ry : 0};
    frame->SupplyDrop((p.buttons & PAD_B) != 0);
    for (int i = 0; i < 12; ++i) {
        frame->SupplyHeld(i, held[i]);
        frame->SupplyPressed(i, held[i] && !m_prevActions[i]);
    }
    for (int i = 0; i < 8; ++i) frame->SupplyValue(i, values[i]);
    for (int i = 0; i < 12; ++i) m_prevActions[i] = held[i];
}

PortPad PortApp::ScriptedPad(const PortPad& live) const {
    PortPad p = live;
    for (size_t i = 0; i < m_script.size(); ++i) {
        const Segment& s = m_script[i];
        if (m_frame < s.first || m_frame >= s.last) continue;
        p.buttons |= s.pad.buttons;
        if (s.pad.lx) p.lx = s.pad.lx;
        if (s.pad.ly) p.ly = s.pad.ly;
        if (s.pad.rx) p.rx = s.pad.rx;
        if (s.pad.ry) p.ry = s.pad.ry;
    }
    return p;
}

// Tests: start a run straight away (the Windows target's original flow), set up to its ready boundary.
bool PortApp::StartDirect() {
    const unsigned int seed = m_options.seed ? m_options.seed : 1;
    PortGameSession session(m_menuRandom, ui);
    NativeActionFrame af;
    NativeFrameActions fa(af);
    PortPad idle;
    memset(&idle, 0, sizeof(idle));
    BuildActions(idle, 0, &af);
    NativeSceneFiles startFiles = {"game/file_index.bin", "game/", 1, 0, 0};
    if (m_options.continueRun) {
        std::string primary, rerun;
        std::string path = m_options.continueFile;
        if (path.empty()) {
            if (!ui.LocalRunFilenames(&primary, &rerun)) {
                PortLog("continue selected-slot path failed");
                m_exitCode = 6;
                return false;
            }
            path = primary;
        }
        if (!session.ContinueNativeSave(path.c_str(), runtime, startFiles, 0)) {
            PortLog("native continue failed seed=%u phase=%d next=%08x", seed, runtime.phase, runtime.missingNativePrerequisite);
            m_exitCode = 6;
            return false;
        }
        PortLog("native continue file=%s seed=%u stage=%d room=%d", path.c_str(), runtime.run.seeds.start,
                runtime.run.context.stage, runtime.world.currentList);
    } else {
        if (!ui.music.StartCharacter(ui.characterPage)) {
            PortLog("character music stinger failed");
            m_exitCode = 5;
            return false;
        }
        SyncMusic();
        PortSessionStart start;
        if (m_options.greed) {
            ui.characterMode = 2;
            start = session.StartGreedCharacter(seed, runtime, startFiles);
        } else {
            start = session.StartNormalCharacter(seed, runtime);
        }
        PortLog("start result=%d seed=%u player=%d greed=%d", start.result, seed, start.player, m_options.greed ? 1 : 0);
        if (start.result != NPS_WORLD_PENDING) {
            m_exitCode = 3;
            return false;
        }
    }
    NativeSceneFiles files = Files();
    int setup = 0;
    // One setup call runs the complete reached boundary; continue only while the flow itself still owns
    // generation/room-component phases.
    while (runtime.phase == PNR_GENERATING || runtime.phase == PNR_ROOM_COMPONENTS) {
        BuildActions(idle, (unsigned int)setup, &af);
        NormalRunResult r = session.AdvanceNormalSetup(runtime, files, &fa);
        ++setup;
        if (r == NFR_BAD_REQUEST) {
            PortLog("setup rejected prerequisite=%08x", runtime.missingNativePrerequisite);
            break;
        }
    }
    PortLog("setup phase=%d steps=%d resident=%d prerequisite=%08x", runtime.phase, setup, runtime.world.residentCount,
            runtime.missingNativePrerequisite);
    if (runtime.phase != PNR_READY) {
        m_exitCode = 4;
        return false;
    }
    files = Files();
    for (int d = 0; d < m_options.descend; ++d) {
        if (!session.DescendNormal(runtime, files, &fa)) {
            PortLog("descend test failed at %d prerequisite=%08x", d, runtime.missingNativePrerequisite);
            break;
        }
        PortLog("descend test stage=%d room=%d backdrop=%d", runtime.run.context.stage, runtime.world.currentList, files.backdrop);
    }
    if (m_options.boss) {
        int grid = -1, dimension = 0;
        const NativePlacedRoom* target = 0;
        for (int i = 0; i < runtime.world.layout.count; ++i) {
            const NativePlacedRoom& p = runtime.world.layout.rooms[i];
            const NativeRoomDef* d = NativeRoom(p.definition);
            if (d && d->type == 5) {
                grid = p.gridIndex;
                dimension = p.dimension;
                target = &p;
                break;
            }
        }
        if (grid >= 0) {
            NativeWorldIssue issue;
            files = Files();
            NativeWorldResult result = runtime.world.Enter(grid, dimension, files, &issue, 0);
            if (NativeRoomState* entered = runtime.world.Current()) {
                runtime.playerBody.position.x = (entered->space.left + entered->space.right) * 0.5f;
                runtime.playerBody.position.y = (entered->space.top + entered->space.bottom) * 0.5f;
                runtime.playerBody.previousPosition = runtime.playerBody.position;
                runtime.playerBody.velocity.x = runtime.playerBody.velocity.y = 0;
            }
            NativeWorldResident& live = runtime.world.resident[runtime.world.currentList];
            PortLog("boss enter result=%d grid=%d dim=%d mode=%d def=%d enemies=%d src=%d stage=%d type=%u", result, grid,
                    dimension, files.nativeMode, target ? target->definition : -1, live.enemies.count, issue.source,
                    issue.construction.stage, issue.construction.type);
        }
    }
    if (m_options.layout) {
        const NativeLevelLayout& L = runtime.world.layout;
        for (int i = 0; i < L.count; ++i) {
            const NativePlacedRoom& p = L.rooms[i];
            const NativeRoomDef* d = NativeRoom(p.definition);
            PortLog("layout list=%d grid=%d def=%d source=%d ordinal=%d type=%d variant=%u path=%s", p.listIndex, p.gridIndex,
                    p.definition, d ? d->source : -1, d ? d->ordinal : -1, d ? d->type : -1, d ? d->variant : 0,
                    d && NativeRoomSource(d->source) ? NativeRoomSource(d->source) : "?");
        }
        m_exitCode = 0;
        return false;
    }
    return true;
}

bool PortApp::Tick(const PortPad& livePad) {
    if (m_stopped || (m_options.frames > 0 && m_frame >= m_options.frames)) return false;
    const PortPad pad = ScriptedPad(livePad);
    NativeActionFrame actionFrame;
    NativeFrameActions frameActions(actionFrame);
    BuildActions(pad, m_options.direct ? (unsigned int)m_frame : ++m_actionFrame, &actionFrame);
    if (m_options.direct) {
        TestHooks();
        TickGameplay(frameActions);
    } else {
        TickMenu(pad, frameActions);
    }
    SyncMusic();
    if (m_frame % 30 == 0 && GameplayVisible()) LogTick();
    ++m_frame;
    return true;
}

void PortApp::TestHooks() {
    if (runtime.world.currentList < 0 || runtime.world.currentList >= kNativeWorldRoomLimit) return;
    NativeWorldResident& tr = runtime.world.resident[runtime.world.currentList];
    if (m_options.rewardTest && m_frame >= 20 && m_frame < 24) {
        int variant = m_frame == 20 ? 10 : (m_frame == 21 ? 20 : (m_frame == 22 ? 30 : 40));
        if (tr.pickups.count < 64) {
            NativePickup* p = new NativePickup;
            NativePoint at = runtime.playerBody.position;
            if (p->Begin(5, variant, 0, 0, 1, 0, at)) {
                tr.pickups.entries[tr.pickups.count++] = p;
                PortLog("reward-test inject variant=%d", variant);
            } else {
                delete p;
            }
        }
    }
    if (m_options.teleport) {
        int index = (m_frame >= 70 && m_frame < 80) ? 0 : ((m_frame >= 100 && m_frame < 110) ? 1 : -1);
        if (index >= 0 && index < tr.pickups.count && tr.pickups.entries[index]) {
            runtime.playerBody.position = tr.pickups.entries[index]->position;
            runtime.playerBody.previousPosition = runtime.playerBody.position;
            runtime.playerBody.velocity.x = runtime.playerBody.velocity.y = 0;
            PortLog("teleport frame=%d pickup=%d variant=%d", m_frame, index, tr.pickups.entries[index]->variant);
        }
    }
}

// The run's ticks, shared by the menu flow and the direct test start.
void PortApp::TickGameplay(NativeFrameActions& frameActions) {
    PortGameSession session(m_menuRandom, ui);
    NativeSceneFiles files = Files();
    if (runtime.phase == PNR_READY) {
        if (!m_readyLogged) {
            NativeWorldResident& first = runtime.world.resident[runtime.world.currentList];
            PortLog("normal ready playerToken=%08x doorVisualGaps=%d", runtime.playerBody.entityToken, first.doorVisualGaps);
            m_readyLogged = true;
        }
        // Autosave every 300 ticks through the native selected-slot writer (menu runs only: tests own their saves).
        if (!m_options.direct && runtime.tickFrame > m_lastAutosave + 300) {
            m_lastAutosave = runtime.tickFrame;
            const bool saved = session.SaveSelectedNativeContinue(runtime);
            PortLog("autosave native=%d slot=%d", saved ? 1 : 0, ui.selectedSlot);
        }
        NormalRunResult tick = session.TickNormal(runtime, files, &frameActions);
        if (m_options.invulnerable && runtime.phase == PNR_READY) runtime.playerInvulnerable = 30;
        if (runtime.descendRequested && runtime.phase == PNR_READY) {
            if (session.DescendNormal(runtime, files, &frameActions))
                PortLog("descend stage=%d room=%d def=%d", runtime.run.context.stage, runtime.world.currentList,
                        runtime.world.CurrentDescriptor() ? runtime.world.CurrentDescriptor()->definition : -1);
            else {
                runtime.descendRequested = false;
                PortLog("descend failed stage=%d prerequisite=%08x", runtime.run.context.stage, runtime.missingNativePrerequisite);
            }
        }
        if (tick != NFR_OK && runtime.missingNativePrerequisite && runtime.missingNativePrerequisite != m_lastPrerequisite) {
            m_lastPrerequisite = runtime.missingNativePrerequisite;
            PortLog("tick result=%d prerequisite=%08x room=%d type=%u variant=%u", tick, runtime.missingNativePrerequisite,
                    runtime.world.currentList, runtime.entryIssue.construction.type, runtime.entryIssue.construction.variant);
        }
    } else if (runtime.phase == PNR_DYING || runtime.phase == PNR_GAME_OVER) {
        if (!m_dyingLogged) {
            PortLog("normal death phase=%d hits=%u hp=%d/%d", runtime.phase, runtime.playerHits, runtime.playerState.redHearts,
                    runtime.playerState.maxRedHearts);
            m_dyingLogged = true;
        }
        session.TickNormal(runtime, files, &frameActions);
    }
}

// The frontend (title, files, main menu, characters) and its New Run / Continue intents, then the run.
void PortApp::TickMenu(const PortPad& p, NativeFrameActions& frameActions) {
    PortMenuInput input = {(p.buttons & PAD_UP) != 0 || p.ly > 16000, (p.buttons & PAD_DOWN) != 0 || p.ly < -16000,
                           (p.buttons & PAD_A) != 0, (p.buttons & (PAD_B | PAD_BACK)) != 0, (p.buttons & PAD_START) != 0,
                           (p.buttons & PAD_LEFT) != 0 || p.lx < -16000, (p.buttons & PAD_RIGHT) != 0 || p.lx > 16000,
                           (p.buttons & PAD_X) != 0, (p.buttons & PAD_Y) != 0};
    PortAction action = UI_NONE;
    if (m_frontendReady && !GameplayVisible()) action = ui.Step(input);
    if (audio.initialized && action != UI_NONE && action != UI_CONTINUE_RUN) {
        if (action == UI_START_CHARACTER) audio.PlaySfx("resources/sfx/feedback/character select right.wav", 0.5f);
        else audio.PlaySfx("resources/sfx/v2/menu_scroll.wav", 0.35f);
    }
    PortGameSession session(m_menuRandom, ui);
    // The selected slot owns the native run filename; New Run and Continue are separate intents.
    if (action == UI_SELECT_SLOT) {
        std::string primary, rerun;
        bool resume = false;
        if (ui.LocalRunFilenames(&primary, &rerun)) {
            if (FILE* nativeRun = fopen(primary.c_str(), "rb")) {
                resume = true;
                fclose(nativeRun);
            }
        }
        ui.profile = m_profiles[ui.selectedSlot];
        ui.SlotLoaded(true, resume);
    }
    if (action == UI_DELETE_SLOT) {
        std::string primary, rerun;
        if (ui.LocalRunFilenames(&primary, &rerun)) {
            remove(primary.c_str());
            remove(rerun.c_str());
        }
        memset(&m_profiles[ui.selectedSlot], 0, sizeof(NativeProfile));
        ui.SlotDeleted(true);
    }
    if (action == UI_CONTINUE_RUN) {
        NativeSceneFiles continueFiles = {"game/file_index.bin", "game/", 1, 2, 0};
        std::string primary, rerun;
        if (ui.LocalRunFilenames(&primary, &rerun)) PortLog("native continue path=%s", primary.c_str());
        if (session.ContinueSelectedNativeRun(runtime, continueFiles, &frameActions))
            PortLog("native continue slot=%d seed=%u", ui.selectedSlot, runtime.run.seeds.start);
        else
            PortLog("native continue failed slot=%d phase=%d prereq=%08x entry=%d source=%d next=%08x", ui.selectedSlot,
                    runtime.phase, runtime.missingNativePrerequisite, runtime.entryIssue.result, runtime.entryIssue.source,
                    runtime.entryIssue.nativeNext);
        return;
    }
    if (action == UI_START_CHARACTER) {
        // Always a fresh New Run; existing native files are consumed only by Continue.
        unsigned int runSeed = m_options.seed;
        if (!runSeed) runSeed = m_platform.entropy + (unsigned int)m_frame * 2654435761u;
        if (!runSeed) runSeed = 1;
        NativeSceneFiles startFiles = {"game/file_index.bin", "game/", 1, 2, 0};
        PortSessionStart start;
        if (ui.characterMode >= 2) start = session.StartGreedCharacter(runSeed, runtime, startFiles);
        else start = session.StartNormalCharacter(runSeed, runtime);
        PortLog("normal setup result=%d seed=%u player=%d greed=%d", start.result, runSeed, start.player,
                ui.characterMode >= 2 ? 1 : 0);
    }
    if (ui.characterPending) {
        NormalRunResult result = session.AdvanceNormalSetup(runtime, Files(), &frameActions);
        if (!ui.characterPending)
            PortLog("normal setup boundary phase=%d generation=%d result=%d resident=%d source=%d prerequisite=%08x",
                    runtime.phase, runtime.run.phase, result, runtime.world.residentCount, runtime.entryIssue.source,
                    runtime.missingNativePrerequisite);
    }
    TickGameplay(frameActions);
    if (action != UI_NONE && action != UI_SELECT_SLOT && action != UI_DELETE_SLOT && action != UI_START_CHARACTER)
        PortLog("pending native frontend destination=%d", action);
}

void PortApp::LogTick() {
    NativeWorldResident& live = runtime.world.resident[runtime.world.currentList];
    const NativePlacedRoom* d = runtime.world.CurrentDescriptor();
    PortLog("tick=%d phase=%d stage=%d room=%d list=%d act=%u pos=(%.2f,%.2f) hp=%d/%d soul=%d enemies=%d shots=%u fired=%u "
            "wall=%u hits=%u pickups=%d collected=%u coins=%d keys=%d bombs=%d transitions=%u boss=%d clear=%d doors=%d "
            "items=%u lastItem=%d bombsActive=%u explosions=%u",
            m_frame, runtime.phase, runtime.run.context.stage, d ? d->definition : -1, runtime.world.currentList,
            live.activations, runtime.playerBody.position.x, runtime.playerBody.position.y, runtime.playerState.redHearts,
            runtime.playerState.maxRedHearts, runtime.playerState.soulHearts, live.enemies.count,
            (unsigned int)runtime.enemyShots.active.size(), (unsigned int)runtime.enemyShots.spawned,
            (unsigned int)runtime.enemyShots.wallHits, runtime.playerHits, live.pickups.count, runtime.pickupsCollected,
            runtime.playerState.coins, runtime.playerState.keys, runtime.playerState.bombs, runtime.roomTransitions,
            runtime.bossDefeated ? 1 : 0, live.room ? live.room->clear : 0, live.doors ? live.doors->attached : 0,
            runtime.collectiblesCollected, runtime.lastCollectible, (unsigned int)runtime.bombs.active.size(),
            runtime.bombs.explosions);
}

void PortApp::Shutdown() {
    if (m_options.saveOnExit && runtime.phase == PNR_READY) {
        PortGameSession session(m_menuRandom, ui);
        const bool saved = session.SaveSelectedNativeContinue(runtime);
        PortLog("save native=%d slot=%d", saved ? 1 : 0, ui.selectedSlot);
    }
    PortLog("done frames=%d phase=%d", m_frame, runtime.phase);
    PortLog("audio summary device=%d submits=%u mixed=%u sfx=%u music_frames=%u sfx_frames=%u loaded=%u", audio.deviceReady ? 1 : 0,
            audio.deviceSubmits, audio.framesMixed, audio.sfxStarted, audio.musicFrames, audio.sfxFrames, audio.sfxLoaded);
    audio.Shutdown();
}
