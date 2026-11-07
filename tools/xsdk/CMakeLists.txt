# boi360's Xbox 360 build on the open xsdk (github.com/xenosworks/xsdk) and vulkan4xenon (docs/XSDK_PLAN.md).
# tools/xsdk/build.sh writes the shader bundle and configures this with xsdk's toolchain file.
cmake_minimum_required(VERSION 3.22)
project(boi360 C CXX)

if(NOT XSDK)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=<xsdk>/cmake/xsdk.cmake")
endif()

set(VULKAN4XENON "" CACHE PATH "vulkan4xenon checkout, with build/libvulkan4xenon.a built")
set(BOI_SHADERS "" CACHE PATH "the shader bundle's folder: boi_shaders.c (build.sh)")
set(BOI_GIT_COMMIT "" CACHE STRING "Source commit for the log's first line")
set(BOI_TITLE_ID "0x42343630" CACHE STRING "The XEX's title ID ('B460')")

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# A title runs in 32-bit mode, where a record form sets CR0 from its result's low word: GCC's record forms of
# 64-bit values get a 64-bit compare after them (record_forms.py, from Cuphead360; a change needs a clean build).
set(CMAKE_C_COMPILER_LAUNCHER python3 "${CMAKE_CURRENT_LIST_DIR}/record_forms.py")
set(CMAKE_CXX_COMPILER_LAUNCHER python3 "${CMAKE_CURRENT_LIST_DIR}/record_forms.py")

# char is signed as on the PC (PowerPC GCC's is unsigned), no strict aliasing, and no fused multiply-adds, so
# floats come out as on the Windows binary (/fp:precise).
set(COMMON_FLAGS -O2 -fsigned-char -fno-strict-aliasing -ffp-contract=off -ffunction-sections -fdata-sections)

file(GLOB GAME_SOURCES "${ROOT}/src/game/*.cpp" "${ROOT}/src/content/*.cpp")
set(PORT_SOURCES "${ROOT}/src/app/port_app.cpp" "${ROOT}/src/app/presentation.cpp" "${ROOT}/src/render/sprite_vk.cpp"
    "${ROOT}/src/platform/xsdk/main_xsdk.cpp" "${ROOT}/src/platform/xsdk/audio_xsdk.cpp")
set(C_SOURCES "${ROOT}/src/platform/xsdk/big_alloc.c" "${BOI_SHADERS}/boi_shaders.c")

add_executable(boi360 ${GAME_SOURCES} ${PORT_SOURCES} ${C_SOURCES})
target_include_directories(boi360 PRIVATE "${ROOT}/src/game" "${ROOT}/src/content" "${ROOT}/build/generated")
target_include_directories(boi360 SYSTEM PRIVATE "${ROOT}/src/third_party/vulkan-headers/include" "${VULKAN4XENON}/include")
target_compile_definitions(boi360 PRIVATE BOI360_XSDK=1)
target_compile_options(boi360 PRIVATE ${COMMON_FLAGS})
set_source_files_properties(${GAME_SOURCES} ${PORT_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-std=gnu++17;-Wall;-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-sign-compare;-Wno-misleading-indentation;-Wno-parentheses;-Wno-class-memaccess;-Wno-maybe-uninitialized")
set_source_files_properties(${C_SOURCES} PROPERTIES COMPILE_OPTIONS "-std=gnu99;-w")
if(BOI_GIT_COMMIT)
    target_compile_definitions(boi360 PRIVATE BOI_GIT_COMMIT="${BOI_GIT_COMMIT}")
endif()

target_link_libraries(boi360 PRIVATE "${VULKAN4XENON}/build/libvulkan4xenon.a")
set_property(TARGET boi360 APPEND PROPERTY LINK_DEPENDS "${VULKAN4XENON}/build/libvulkan4xenon.a")
target_link_options(boi360 PRIVATE "-Wl,--gc-sections,--no-warn-execstack,-Map,${CMAKE_CURRENT_BINARY_DIR}/boi360.map")
# Big blocks from the kernel's pages, given back whole (src/platform/xsdk/big_alloc.c).
target_link_options(boi360 PRIVATE "-Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc")
xsdk_add_xex(boi360 TITLE_ID ${BOI_TITLE_ID} NAME "boi360")
