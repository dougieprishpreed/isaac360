# The recompiled game for Windows (docs/RECOMP_PLAN.md). tools/recomp/build_win.cmd configures this with Ninja
# and clang-cl (x64); the translated sources come from tools/recomp/translate.py in build/recomp/out.
cmake_minimum_required(VERSION 3.22)
project(boi360_recomp C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GEN "${ROOT}/build/recomp/out")
file(GLOB GEN_SOURCES "${GEN}/f*.c")
file(GLOB LUA_SOURCES "${ROOT}/build/recomp/out_lua/f*.c")
file(GLOB SHIM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/shims_*.cpp")

add_library(boi360_guest STATIC ${GEN_SOURCES} "${GEN}/functions.c")
# Lua5.3.3r.dll, recompiled the same way (own folder: its decls.h)
add_library(boi360_guest_lua STATIC ${LUA_SOURCES} "${ROOT}/build/recomp/out_lua/functions.c")
target_include_directories(boi360_guest_lua PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${ROOT}/build/recomp/out_lua")
target_compile_options(boi360_guest_lua PRIVATE /O1 /w /fp:precise -fno-strict-aliasing)
target_include_directories(boi360_guest PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${GEN}")
# the translated code is large and flat: -O1 keeps compile times reasonable; no warnings for generated code
target_compile_options(boi360_guest PRIVATE /O1 /w /fp:precise -fno-strict-aliasing)

add_executable(boi360_recomp host_win32.cpp runtime.cpp gl_vk.cpp vfile.cpp lz.cpp hle_vorbis.cpp ${SHIM_SOURCES})
# gl_shaders.inc: the game's shaders converted by tools/recomp/shaders.py
target_include_directories(boi360_recomp PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${ROOT}/build/recomp" "${ROOT}/src/third_party/vulkan-headers/include")
target_compile_options(boi360_recomp PRIVATE /O2 /W3 /wd4996 /fp:precise -fno-strict-aliasing)
target_compile_definitions(boi360_recomp PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
# msvcp140.dll (the system copy, rebased by tools/recomp/rebase.py)
file(GLOB MSVCP_SOURCES "${ROOT}/build/recomp/out_msvcp/f*.c")
add_library(boi360_guest_msvcp STATIC ${MSVCP_SOURCES} "${ROOT}/build/recomp/out_msvcp/functions.c")
target_include_directories(boi360_guest_msvcp PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${ROOT}/build/recomp/out_msvcp")
target_compile_options(boi360_guest_msvcp PRIVATE /O1 /w /fp:precise -fno-strict-aliasing)

target_link_libraries(boi360_recomp PRIVATE boi360_guest boi360_guest_lua boi360_guest_msvcp user32 gdi32 winmm opengl32)
set_target_properties(boi360_recomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ROOT}/build/recomp")
