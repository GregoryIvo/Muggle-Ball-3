# The game as an application, independent of the harness around it: the front end, its sound, the player's
# input and the level session, plus runGame() itself (Game.cpp). Built once here and linked by both the
# desktop MuggleBall3 below (with main.cpp's developer harness and FormViewer.cpp) and the Vita's own, much
# thinner entry point (platform/vita/game), so neither lists these sources a second time.
add_library(mb_app STATIC FrontEnd.cpp FrontEndAudio.cpp GameAudio.cpp PlayerInput.cpp PlaySession.cpp Game.cpp)
target_include_directories(mb_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mb_app PUBLIC mb_engine mb_game)

# The Vita's entry point (platform/vita/game) links mb_app directly and skips this desktop harness: main.cpp
# is 767 lines of developer tooling (the level viewer, screenshots, sound tests) that the console does not need.
if(MB3_PLATFORM_VITA)
  return()
endif()

add_executable(MuggleBall3 main.cpp FormViewer.cpp)
target_link_libraries(MuggleBall3 PRIVATE mb_app)

# The physics benchmark (docs/VITA_PLAN.md M3b): plays levels with the Autopilot in fixed steps and writes a
# CSV, on the Mac and (platform/vita/bench, which compiles bench/PhysicsBench.cpp straight from here) on the
# Vita, so the two line up level by level. mb_engine and mb_game keep nlohmann_json private; the bench reads
# JSON itself, so it links it too.
add_executable(mb_physics_bench bench/PhysicsBench.cpp bench/main.cpp)
target_include_directories(mb_physics_bench PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_physics_bench PRIVATE mb_game nlohmann_json::nlohmann_json)

if(APPLE)
  set(MB3_PLIST_ICON_ENTRY "")
  set(MB3_PLIST_MIN_OS_ENTRY "")
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(MB3_PLIST_MIN_OS_ENTRY "<key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>")
  endif()
  set(mb3_icon "${MB3_ASSETS_DIR}/AppIcon.icns")
  if(MB3_GAME_ICON AND EXISTS "${mb3_icon}")
    target_sources(MuggleBall3 PRIVATE "${mb3_icon}")
    set_source_files_properties("${mb3_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set(MB3_PLIST_ICON_ENTRY "<key>CFBundleIconFile</key><string>AppIcon</string>")
  elseif(MB3_GAME_ICON)
    message(STATUS "No ${mb3_icon}: the app gets the default icon (tools/make_icon.py builds it, then re-run cmake)")
  endif()

  set_target_properties(MuggleBall3 PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gregoryivo.muggleball3"
    MACOSX_BUNDLE_BUNDLE_NAME "Muggle Ball 3"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

  if(MB3_BUNDLE_ASSETS)
    if(NOT IS_DIRECTORY "${MB3_ASSETS_DIR}/Data")
      message(FATAL_ERROR "MB3_BUNDLE_ASSETS needs the converted assets, but ${MB3_ASSETS_DIR}/Data does not exist "
                          "(run tools/convert_assets.py, or set MB3_ASSETS_DIR)")
    endif()
    message(NOTICE "MB3_BUNDLE_ASSETS: the app will contain the game's files from ${MB3_ASSETS_DIR}. "
                   "They belong to Alawar and Dream Dale: keep this build to yourself and never distribute it.")
    # file(COPY) skips files whose timestamp is unchanged, so rebuilds are quick.
    add_custom_command(TARGET MuggleBall3 POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        -DSRC=${MB3_ASSETS_DIR} -DDST=$<TARGET_BUNDLE_CONTENT_DIR:MuggleBall3>/Resources/assets
        -P ${PROJECT_SOURCE_DIR}/cmake/BundleAssets.cmake
      VERBATIM)
  endif()
endif()
