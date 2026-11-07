#ifndef BOI360_PORT_APP_H
#define BOI360_PORT_APP_H
// The game as both platforms run it (docs/XSDK_PLAN.md): frontend, session, autosave, music and the test
// hooks, stepped at the original 30 Hz from one pad state. src/platform/win32 and src/platform/xsdk only
// supply the window or display, the pad, the audio device, time and the folders.
#include "../game/game_session.h"
#include "../game/native_audio.h"
#include "port_draw.h"
#include <string>
#include <vector>

// XInput's button bits, the layout both platforms' pads arrive in.
enum PortPadButton {
    PAD_UP = 0x0001, PAD_DOWN = 0x0002, PAD_LEFT = 0x0004, PAD_RIGHT = 0x0008,
    PAD_START = 0x0010, PAD_BACK = 0x0020, PAD_LS = 0x0040, PAD_RS = 0x0080,
    PAD_LB = 0x0100, PAD_RB = 0x0200, PAD_A = 0x1000, PAD_B = 0x2000, PAD_X = 0x4000, PAD_Y = 0x8000
};
struct PortPad {
    unsigned short buttons;
    short lx, ly, rx, ry;
    unsigned char lt, rt;
    bool connected;
};

// Command-line options (the console reads them from boi360_args.txt beside the XEX).
struct PortAppOptions {
    unsigned int seed;              // 0: from the platform's entropy
    bool direct;                    // start a run at once instead of the title (tests)
    bool greed, continueRun, boss, layout, invulnerable, teleport, rewardTest, saveOnExit;
    int descend;
    std::string continueFile;
    int frames;                     // stop after this many ticks (0: run on)
    std::string script;             // scripted pad: "first:last:keys;..." (see port_app.cpp)
    PortAppOptions();
    bool Parse(int argc, char** argv);
};

struct PortPlatformInfo {
    const char* name;               // log prefix: "WIN" or "X360"
    const char* dataRoot;           // where port_data mounts: game/, port/ or boi360.pak
    const char* saveDirectory;      // writable, ends with '/'
    const NativeAudioBackend* audio; // 0: software mixing only
    unsigned int entropy;           // seeds the menu RNG and runs without a seed
};

class PortApp {
public:
    PortApp();
    bool Init(const PortAppOptions& options, const PortPlatformInfo& platform);
    // One 30 Hz tick. False once a test run has finished (options.frames, a failed direct start).
    bool Tick(const PortPad& pad);
    void BuildFrame(PortDrawList& out);
    void Shutdown();
    int ExitCode() const { return m_exitCode; }
    int Frame() const { return m_frame; }
    bool AtlasesLoaded() const { return m_frontendReady; }
    // The prepared atlases (RGBA bytes), loaded at Init for the renderer.
    std::vector<unsigned char> frontendAtlas, gameplayAtlas;
    int frontendAtlasW, frontendAtlasH, gameplayAtlasW, gameplayAtlasH;

    // Shared state the presentation reads (presentation.cpp).
    PortFrontend ui;
    PortNormalRuntime runtime;
    NativeAudio audio;
    float roomCameraX;
    int loggedEnemyList, loggedPickupList;   // presentation logs a room's actors once
    bool GameplayVisible() const;

private:
    void SyncMusic();
    void BuildActions(const PortPad& pad, unsigned int frameNumber, NativeActionFrame* frame);
    PortPad ScriptedPad(const PortPad& live) const;
    NativeSceneFiles Files() const;
    bool StartDirect();
    void TickMenu(const PortPad& pad, NativeFrameActions& frameActions);
    void TickGameplay(NativeFrameActions& frameActions);
    void TestHooks();
    void LogTick();
    PortAppOptions m_options;
    PortPlatformInfo m_platform;
    NativeMt m_menuRandom;
    NativeProfile m_profiles[3];
    bool m_prevActions[12];
    int m_lastMusicTrack;
    int m_frame;
    unsigned int m_actionFrame;
    unsigned int m_lastAutosave;
    int m_exitCode;
    bool m_frontendReady, m_readyLogged, m_dyingLogged, m_stopped;
    unsigned int m_lastPrerequisite;

    struct Segment { int first, last; PortPad pad; };
    std::vector<Segment> m_script;
    friend void PortPresent(PortApp& app, PortDrawList& out);
};

void PortLogOpen(const char* path, const char* prefix);
void PortLog(const char* fmt, ...);
void PortLogClose();

#endif
