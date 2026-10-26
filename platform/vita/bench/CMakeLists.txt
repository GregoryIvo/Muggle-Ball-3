# The physics benchmark VPK (docs/VITA_PLAN.md M3b): the same bench/PhysicsBench.cpp as the host's
# mb_physics_bench (app/CMakeLists.txt), compiled straight from app/bench/ since a Vita build does not
# add_subdirectory(app). It plays levels with the Autopilot and mb_game's real physics, drawing its progress
# with SDL's own GXM renderer (SDL3's built-in Vita backend) rather than vitaGL, which this VPK never touches.
add_executable(mb3_vita_bench
  main.cpp
  ${PROJECT_SOURCE_DIR}/app/bench/PhysicsBench.cpp)
target_include_directories(mb3_vita_bench PRIVATE ${PROJECT_SOURCE_DIR}/app ${PROJECT_SOURCE_DIR}/app/bench)
target_link_libraries(mb3_vita_bench PRIVATE
  mb_game nlohmann_json::nlohmann_json mb3_vita_compat SDL3::SDL3
  ScePower_stub SceIofilemgr_stub SceDisplay_stub SceGxm_stub SceCommonDialog_stub
  SceTouch_stub SceHid_stub SceCtrl_stub SceSysmem_stub SceProcessmgr_stub SceKernelDmacMgr_stub)
# No explicit pthread: the toolchain's -pthread (which Jolt carries, through mb_game) links the whole
# archive, and a second -lpthread would define every symbol twice (see platform/vita/hello/CMakeLists.txt).
mb3_vita_vpk(mb3_vita_bench
  TITLE_ID MUGB00004
  NAME "MB3 Physics Bench"
  VERSION 01.00)
