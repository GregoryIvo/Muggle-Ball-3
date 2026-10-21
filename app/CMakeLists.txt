add_executable(MuggleBall3 main.cpp)
target_link_libraries(MuggleBall3 PRIVATE mb_engine mb_game)

if(APPLE)
  set(MB3_PLIST_ICON_ENTRY "")
  set(MB3_PLIST_MIN_OS_ENTRY "")
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(MB3_PLIST_MIN_OS_ENTRY "<key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>")
  endif()
  set(mb3_icon "${MB3_ASSETS_DIR}/AppIcon.icns")
  if(EXISTS "${mb3_icon}")
    target_sources(MuggleBall3 PRIVATE "${mb3_icon}")
    set_source_files_properties("${mb3_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set(MB3_PLIST_ICON_ENTRY "<key>CFBundleIconFile</key><string>AppIcon</string>")
  else()
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
