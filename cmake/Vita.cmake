# PlayStation Vita: the cross build on VitaSDK with vitaGL. The top-level CMakeLists.txt includes this right
# after project() when the toolchain file ($VITASDK/share/vita.toolchain.cmake) set VITA; the toolchain
# file is only read inside project(), so nothing before it can know the target. docs/VITA.md explains the
# port, docs/VITA_PLAN.md the plan behind it.

set(MB3_PLATFORM_VITA ON)

# vita_create_self and vita_create_vpk.
include("${VITASDK}/share/vita.cmake")

# Nothing can run tests in a cross build; the host builds keep them on. A value forced on from the command line
# would add targets that cannot link here, so it is an error rather than a silent override.
set(MB3_BUILD_TESTS OFF CACHE BOOL "Build unit tests (off on Vita: a cross build cannot run them)")
if(MB3_BUILD_TESTS)
  message(FATAL_ERROR "MB3_BUILD_TESTS is not available in a Vita cross build (the tests run on the host)")
endif()

option(MB3_VITA_BUILD_ENGINE "Vita: also cross-compile mb_engine and mb_game (work in progress until M3a)" OFF)

# Title IDs are four letters and five digits. The game and its hello share one; the device benchmarks
# get their own so they can sit beside it on the LiveArea.
set(MB3_VITA_TITLE_ID "MUGB00003" CACHE STRING "Title ID of the game's VPK")
set(MB3_VITA_APP_NAME "Muggle Ball 3" CACHE STRING "Name under the LiveArea bubble")
set(MB3_VITA_APP_VERSION "01.00" CACHE STRING "APP_VER in the VPK's param.sfo (two digits, a dot, two digits)")

# param.sfo values are set explicitly, so a change of vita-mksfoex's defaults cannot change the app.
# ATTRIBUTE2=0 is the normal 256 MiB memory budget; 12 would ask for the 365 MiB one, which docs/VITA_PLAN.md
# keeps as a last resort.
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=0" CACHE STRING "vita-mksfoex flags")

# vitaGL (built from source at the commit pinned in docs/VITA.md and installed into the SDK) with the
# libraries it needs underneath: NEON maths, the runtime shader compiler (vitaShaRK over Sony's
# SceShaccCg, loaded from the console's libshacccg.suprx) and the system modules it calls.
add_library(mb3_vitagl INTERFACE)
target_link_libraries(mb3_vitagl INTERFACE
  vitaGL mathneon vitashark SceShaccCgExt taihen_stub SceShaccCg_stub
  SceGxm_stub SceDisplay_stub SceAppMgr_stub SceCommonDialog_stub SceKernelDmacMgr_stub)

# What newlib lacks and the code needs; see each file's comment. Jolt links it for posix_memalign.
add_library(mb3_vita_compat STATIC ${PROJECT_SOURCE_DIR}/platform/vita/src/posix_memalign.c)

# Wraps an executable target into eboot.bin and a VPK with the placeholder LiveArea set in
# platform/vita/sce_sys (made by tools/vita_livearea.py: text and plain colour, no game art). UNSAFE
# because vitaGL loads the shader compiler module from ur0: and the game sets the CPU clock.
#   mb3_vita_vpk(target TITLE_ID id NAME "name" VERSION 01.00)
function(mb3_vita_vpk target)
  cmake_parse_arguments(arg "" "TITLE_ID;NAME;VERSION" "" ${ARGN})
  if(NOT arg_TITLE_ID OR NOT arg_NAME OR NOT arg_VERSION)
    message(FATAL_ERROR "mb3_vita_vpk(${target}) needs TITLE_ID, NAME and VERSION")
  endif()
  set(sce_sys "${PROJECT_SOURCE_DIR}/platform/vita/sce_sys")
  vita_create_self(${target}.self ${target} UNSAFE)
  vita_create_vpk(${target}.vpk ${arg_TITLE_ID} ${target}.self
    VERSION ${arg_VERSION}
    NAME "${arg_NAME}"
    FILE ${sce_sys}/icon0.png sce_sys/icon0.png
    FILE ${sce_sys}/pic0.png sce_sys/pic0.png
    FILE ${sce_sys}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE ${sce_sys}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE ${sce_sys}/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
endfunction()
