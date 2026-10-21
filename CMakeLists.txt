cmake_minimum_required(VERSION 3.24)
project(MuggleBall3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
  if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
  endif()
endif()

option(MB3_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(app)

if(MB3_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
