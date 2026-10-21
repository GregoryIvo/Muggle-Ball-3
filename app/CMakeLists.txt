add_executable(MuggleBall3 main.cpp FormViewer.cpp)
target_link_libraries(MuggleBall3 PRIVATE mb_engine mb_game)

if(APPLE)
  set_target_properties(MuggleBall3 PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gregoryivo.muggleball3"
    MACOSX_BUNDLE_BUNDLE_NAME "Muggle Ball 3"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()
