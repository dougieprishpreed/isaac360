// steam_api.dll: the console has no Steam. SteamAPI_Init reports failure and every registration is a no-op, so
// the game runs its offline path (its saves are local files). SteamInternal_ContextInit hands back a zeroed
// context: interface pointers stay null, which the game checks before use when Steam did not start.
#include "runtime.h"

namespace {
void st_Init(X86* c) { recomp_log("SteamAPI_Init -> false (offline)"); RET_CDECL(c, 0); }
void st_ret0(X86* c) { RET_CDECL(c, 0); }
uint32_t g_context;
void st_ContextInit(X86* c) {
    if (!g_context) g_context = guest_calloc(1, 256);
    RET_CDECL(c, g_context);
}

const ShimDef kSteam[] = {
    {"steam_api.dll!SteamAPI_Init", st_Init},
    {"steam_api.dll!SteamAPI_Shutdown", st_ret0},
    {"steam_api.dll!SteamAPI_RunCallbacks", st_ret0},
    {"steam_api.dll!SteamAPI_RegisterCallback", st_ret0},
    {"steam_api.dll!SteamAPI_UnregisterCallback", st_ret0},
    {"steam_api.dll!SteamAPI_RegisterCallResult", st_ret0},
    {"steam_api.dll!SteamAPI_UnregisterCallResult", st_ret0},
    {"steam_api.dll!SteamAPI_GetHSteamPipe", st_ret0},
    {"steam_api.dll!SteamAPI_GetHSteamUser", st_ret0},
    {"steam_api.dll!SteamInternal_ContextInit", st_ContextInit},
    {"steam_api.dll!SteamInternal_CreateInterface", st_ret0},
};
}

void shims_steam_register() { shim_register(kSteam, (int)(sizeof(kSteam) / sizeof(kSteam[0]))); }
