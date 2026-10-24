# The M1a hello: one VPK that opens the SDL window, brings up vitaGL, draws a lit triangle with the real mesh
# shader in GLSL ES, paces at two vblanks, tries the IME, steps a Jolt world and runs the runtime probe of
# docs/VITA_PLAN.md section 5.4b, logging everything to ux0:data/MuggleBall3/. It is how the console is
# first measured; the game follows in later milestones.
add_executable(mb3_vita_hello main.cpp)
target_link_libraries(mb3_vita_hello PRIVATE
  SDL3::SDL3 mb3_vitagl Jolt mb3_vita_compat
  ScePower_stub SceAppUtil_stub SceIofilemgr_stub)
# No explicit pthread: the toolchain's -pthread (which Jolt carries) links the whole archive, and a second
# -lpthread would define every symbol twice.
mb3_vita_vpk(mb3_vita_hello
  TITLE_ID ${MB3_VITA_TITLE_ID}
  NAME "${MB3_VITA_APP_NAME}"
  VERSION ${MB3_VITA_APP_VERSION})
