# The Vita's own VPK for the full game (M5b): main.cpp (above) plus the three memory globals every Vita
# executable needs (platform/vita/src/VitaProcess.cpp), linked against mb_app (app/CMakeLists.txt: FrontEnd,
# FrontEndAudio, GameAudio, PlayerInput, PlaySession and Game.cpp, built once there and shared with the
# desktop MuggleBall3), which brings in mb_game and mb_engine with the vitaGL renderer (M4a). Not main.cpp or
# FormViewer.cpp from app/: those are the desktop's own developer harness.
add_executable(MuggleBall3
  main.cpp
  ${PROJECT_SOURCE_DIR}/platform/vita/src/VitaProcess.cpp)
target_link_libraries(MuggleBall3 PRIVATE mb_app)
mb3_vita_vpk(MuggleBall3
  TITLE_ID ${MB3_VITA_TITLE_ID}
  NAME "${MB3_VITA_APP_NAME}"
  VERSION ${MB3_VITA_APP_VERSION})
