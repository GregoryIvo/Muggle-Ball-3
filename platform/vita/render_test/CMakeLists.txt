# The M4a device render test: tests/render_test.cpp's 19 conformance checks (the same file the host build
# runs, unchanged), built as a VPK. Its own main() is renamed to render_test_main (-Dmain=render_test_main,
# set on that one source file only: main.cpp here keeps its own main()) and MB3_TEST_OUTPUT_DIR points its
# save() and the per-check PNGs at ux0:data/MuggleBall3/render-test/, the same place main.cpp redirects
# stdout and stderr to (results.txt) before calling it. Gets its own title ID so it sits beside the hello and
# the game on the LiveArea.
add_executable(mb3_vita_render_test
  ${PROJECT_SOURCE_DIR}/tests/render_test.cpp
  main.cpp
  ${PROJECT_SOURCE_DIR}/platform/vita/src/VitaProcess.cpp)
set_source_files_properties(${PROJECT_SOURCE_DIR}/tests/render_test.cpp PROPERTIES COMPILE_DEFINITIONS
  "main=render_test_main;MB3_TEST_OUTPUT_DIR=\"ux0:data/MuggleBall3/render-test\"")
target_link_libraries(mb3_vita_render_test PRIVATE mb_engine)
mb3_vita_vpk(mb3_vita_render_test
  TITLE_ID MUGB00005
  NAME "MB3 Render Test"
  VERSION 01.00)
