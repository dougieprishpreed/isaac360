// Shim registration and run-time symbol lookup (GetProcAddress, wglGetProcAddress) for the recompiled game.
#include "runtime.h"
#include <map>
#include <string>

void shims_crt_register();
void shims_crt_init();
void shims_kernel32_register();
void shims_steam_register();
void shims_user32_register();
void shims_gl_register();
void shims_al_register();
void shims_input_register();
void shims_crt2_register();
void shims_crt3_register();
uint32_t gl_get_proc(const std::string& name);

namespace {
std::map<std::string, ShimFn> g_dynamic;                 // "module!name" -> host function
std::map<std::string, uint32_t> g_dynamicThunks;
}

void shim_register_dynamic(const char* module, const char* name, ShimFn fn) {
    g_dynamic[std::string(module) + "!" + name] = fn;
}

// Address of a function the game looks up at run time; "" probes whether the module is provided at all.
uint32_t shim_proc_address(const std::string& moduleIn, const std::string& name) {
    std::string module = moduleIn;
    for (size_t i = 0; i < module.size(); ++i) module[i] = (char)tolower((unsigned char)module[i]);
    if (name.empty()) {
        if (module == "opengl32.dll" || module == "opengl32") return 1;
        for (std::map<std::string, ShimFn>::iterator it = g_dynamic.begin(); it != g_dynamic.end(); ++it)
            if (it->first.compare(0, module.size() + 1, module + "!") == 0) return 1;
        std::string dll = module.find('.') == std::string::npos ? module + ".dll" : module;
        return shim_module_known(dll) ? 1 : 0;
    }
    if ((module == "opengl32.dll" || module == "opengl32") && name.compare(0, 2, "gl") == 0) return gl_get_proc(name);
    std::string key = module + "!" + name;
    std::map<std::string, uint32_t>::iterator t = g_dynamicThunks.find(key);
    if (t != g_dynamicThunks.end()) return t->second;
    std::map<std::string, ShimFn>::iterator it = g_dynamic.find(key);
    ShimFn fn = it != g_dynamic.end() ? it->second : 0;
    if (!fn) fn = shim_lookup(module.find('.') == std::string::npos ? module + ".dll!" + name : key);
    if (!fn) return 0;
    uint32_t a = thunk_for(fn, key.c_str());
    g_dynamicThunks[key] = a;
    return a;
}

void shims_register_all() {
    shims_crt_register();
    shims_kernel32_register();
    shims_steam_register();
    shims_user32_register();
    shims_gl_register();
    shims_al_register();
    shims_input_register();
    shims_crt2_register();
    shims_crt3_register();
}

void shims_after_load() { shims_crt_init(); }
