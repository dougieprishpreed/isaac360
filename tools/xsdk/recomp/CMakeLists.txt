# The recompiled game for the Xbox 360 on the open xsdk and vulkan4xenon (docs/RECOMP_PLAN.md).
# tools/xsdk/build_recomp.sh writes the shader bundle and configures this with xsdk's toolchain file. The
# translated sources come from tools/recomp/translate.py (build/recomp/out, out_lua, out_msvcp), the converted
# shaders from tools/recomp/shaders.py (build/recomp/gl_shaders.inc).
cmake_minimum_required(VERSION 3.22)
project(boi360_recomp C CXX)

if(NOT XSDK)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=<xsdk>/cmake/xsdk.cmake")
endif()

set(VULKAN4XENON "" CACHE PATH "vulkan4xenon checkout, with build/libvulkan4xenon.a built")
set(BOI_SHADERS "" CACHE PATH "the shader bundle's folder: boi_shaders.c (build_recomp.sh)")
set(BOI_GIT_COMMIT "" CACHE STRING "Source commit for the log's first line")
set(BOI_TITLE_ID "0x42343630" CACHE STRING "The XEX's title ID ('B460')")
set(BOI_GEN_OPT "-O2" CACHE STRING "Optimisation of the translated code")

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(RECOMP "${ROOT}/src/recomp")

# record forms of 64-bit values in 32-bit mode (tools/xsdk/record_forms.py; a change needs a clean build)
set(CMAKE_C_COMPILER_LAUNCHER python3 "${ROOT}/tools/xsdk/record_forms.py")
set(CMAKE_CXX_COMPILER_LAUNCHER python3 "${ROOT}/tools/xsdk/record_forms.py")

# char signed as on the PC, no strict aliasing (guest memory is read through casts and memcpy), no fused
# multiply-adds, so floats come out as on the PC's /fp:precise
set(COMMON_FLAGS -fsigned-char -fno-strict-aliasing -ffp-contract=off -ffunction-sections -fdata-sections)

# the translated images, one library each (each folder has its own decls.h)
foreach(SET out out_lua out_msvcp)
    file(GLOB GEN_${SET} "${ROOT}/build/recomp/${SET}/f*.c")
    add_library(guest_${SET} STATIC ${GEN_${SET}} "${ROOT}/build/recomp/${SET}/functions.c")
    target_include_directories(guest_${SET} PRIVATE "${RECOMP}" "${ROOT}/build/recomp/${SET}")
    # no unwind tables: nothing unwinds through translated frames (a guest throw or longjmp stops the game), and
    # .eh_frame held 1.8 MB of the console's memory for them
    target_compile_options(guest_${SET} PRIVATE ${BOI_GEN_OPT} ${COMMON_FLAGS} -std=gnu99 -w
                           -fno-asynchronous-unwind-tables -fno-unwind-tables)
endforeach()

file(GLOB SHIM_SOURCES "${RECOMP}/shims_*.cpp")
list(FILTER SHIM_SOURCES EXCLUDE REGEX "shims_gl_pt\\.cpp$")
set(HOST_SOURCES "${RECOMP}/host_xsdk.cpp" "${RECOMP}/runtime.cpp" "${RECOMP}/gl_vk.cpp" "${RECOMP}/vfile.cpp" "${RECOMP}/lz.cpp" "${RECOMP}/hle_vorbis.cpp" "${RECOMP}/hle_png.cpp" ${SHIM_SOURCES})
set(C_SOURCES "${ROOT}/src/platform/xsdk/big_alloc.c" "${BOI_SHADERS}/boi_shaders.c" "${ROOT}/src/third_party/miniz/miniz_tinfl.c")

add_executable(boi360 ${HOST_SOURCES} ${C_SOURCES})
target_include_directories(boi360 PRIVATE "${RECOMP}" "${ROOT}/build/recomp")
target_include_directories(boi360 SYSTEM PRIVATE "${ROOT}/src/third_party/vulkan-headers/include" "${VULKAN4XENON}/include")
target_compile_definitions(boi360 PRIVATE BOI360_XSDK=1)
target_compile_options(boi360 PRIVATE -O2 ${COMMON_FLAGS})
set_source_files_properties(${HOST_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-std=gnu++17;-Wall;-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-sign-compare;-Wno-class-memaccess;-Wno-maybe-uninitialized")
set_source_files_properties(${C_SOURCES} PROPERTIES COMPILE_OPTIONS "-std=gnu99;-w")
if(BOI_GIT_COMMIT)
    target_compile_definitions(boi360 PRIVATE BOI_GIT_COMMIT="${BOI_GIT_COMMIT}")
endif()

target_link_libraries(boi360 PRIVATE guest_out guest_out_lua guest_out_msvcp "${VULKAN4XENON}/build/libvulkan4xenon.a")
set_property(TARGET boi360 APPEND PROPERTY LINK_DEPENDS "${VULKAN4XENON}/build/libvulkan4xenon.a")
target_link_options(boi360 PRIVATE "-Wl,--gc-sections,--no-warn-execstack,-Map,${CMAKE_CURRENT_BINARY_DIR}/boi360.map")
# big blocks from the kernel's pages, given back whole (src/platform/xsdk/big_alloc.c)
target_link_options(boi360 PRIVATE "-Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc,--wrap=_free_r,--wrap=_realloc_r")
xsdk_add_xex(boi360 TITLE_ID ${BOI_TITLE_ID} NAME "boi360")
