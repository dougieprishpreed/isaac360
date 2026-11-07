# boi360's Windows build (docs/XSDK_PLAN.md): the day-to-day binary, boi360.exe on Vulkan, and the host
# simulation checks, from the same sources as the console's default.xex (tools/xsdk/build.sh).
#   cmake -S . -B build/win -G "Visual Studio 17 2022" -A Win32
#   cmake --build build/win --config Release
# Win32 keeps pointers 32-bit as on the console. Generated tables come from the tools/prepare_*.py scripts
# into build/generated (tools/build_windows_target.ps1 runs them first).
cmake_minimum_required(VERSION 3.22)
project(boi360 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  # /fp:precise doesn't contract multiply-adds, matching GCC's -ffp-contract=off on the console.
  # /MP2: two compilers, the user's machine is shared.
  add_compile_options(/W3 /utf-8 /EHsc /fp:precise /MP2 /wd4996 /wd4244 /wd4267 /wd4305 /wd4018)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  # char signed and no fused multiply-adds, as on the console
  add_compile_options(-fsigned-char -fno-strict-aliasing -ffp-contract=off)
endif()

set(GENERATED ${CMAKE_SOURCE_DIR}/build/generated)
file(GLOB GAME_SRC CONFIGURE_DEPENDS src/game/*.cpp src/content/*.cpp)
add_library(boi360_game STATIC ${GAME_SRC})
target_include_directories(boi360_game PUBLIC src/game src/content ${GENERATED})

if(WIN32)
  add_executable(boi360
    src/app/port_app.cpp src/app/presentation.cpp src/render/sprite_vk.cpp
    src/platform/win32/main_win32.cpp src/platform/win32/audio_win32.cpp)
  target_include_directories(boi360 PRIVATE src/third_party/vulkan-headers/include)
  target_link_libraries(boi360 PRIVATE boi360_game xinput winmm user32)
  target_link_options(boi360 PRIVATE /STACK:8388608)
  set_target_properties(boi360 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/windows
                                          RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/windows
                                          RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/build/windows)
endif()
