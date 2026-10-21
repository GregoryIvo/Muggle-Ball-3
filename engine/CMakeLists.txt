add_library(mb_engine
  src/Log.cpp
  src/Paths.cpp
  src/Window.cpp
  src/Renderer.cpp)
target_include_directories(mb_engine PUBLIC include)
target_link_libraries(mb_engine PUBLIC SDL3::SDL3)
