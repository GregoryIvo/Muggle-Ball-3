cmake_minimum_required(VERSION 3.24)

# macOS: CMake reads the architectures and the deployment target while project() detects the compiler,
# so both are set before it; set afterwards they only fill the cache and change nothing.
option(MB3_UNIVERSAL "macOS: build arm64 and x86_64 in one binary (needs MB3_FETCH_SDL3, which it turns on)" OFF)
option(MB3_FETCH_SDL3 "Compile SDL3 from source and link it statically instead of using an installed one" OFF)
option(MB3_BUNDLE_ASSETS "macOS: copy the converted game assets into the .app. For your own copy only: never distribute it" OFF)
option(MB3_GAME_ICON "macOS: use the icon converted from your copy of the game (it is the game's artwork: off for apps you hand out)" ON)
set(MB3_ASSETS_DIR "" CACHE PATH "Converted game assets: icon and bundled assets (default: $MB3_ASSETS, else <source>/assets)")
option(MB3_DEV_ASSETS "Let the app fall back to MB3_ASSETS_DIR, this machine's converted assets, wherever it is started from (off for apps you hand out)" ON)

if(CMAKE_HOST_APPLE)
  if(MB3_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
  endif()
  # Homebrew's SDL3 is a dylib for the host architecture only, so a build that names its architectures
  # compiles SDL3 itself.
  if(CMAKE_OSX_ARCHITECTURES)
    set(MB3_FETCH_SDL3 ON)
  endif()
  # A build with its own SDL3 is meant to run on other Macs, so it gets a deployment target; 13.3 is the
  # oldest whose libc++ has std::to_chars for floating point, which std::format needs. A plain build links
  # Homebrew's SDL3, which is built for the current macOS, and keeps targeting that.
  if(MB3_FETCH_SDL3 AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version" FORCE)
  endif()
endif()

project(MuggleBall3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MB3_ASSETS_DIR)
  if(DEFINED ENV{MB3_ASSETS} AND NOT "$ENV{MB3_ASSETS}" STREQUAL "")
    set(MB3_ASSETS_DIR "$ENV{MB3_ASSETS}")
  else()
    set(MB3_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets")
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
