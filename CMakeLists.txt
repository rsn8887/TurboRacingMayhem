cmake_minimum_required(VERSION 3.13)
project(TurboRacingMayhem VERSION 1.4.0 LANGUAGES CXX)
add_compile_definitions(GAME_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(PLATFORM "DESKTOP" CACHE STRING "DESKTOP or SWITCH (Android is detected from the NDK toolchain)")
# 2 = render at 1280x720 (default), 1 = render at 640x360 and scale up (for slow machines)
set(TURBO_RENDER_SCALE 2 CACHE STRING "Render resolution multiplier of 640x360 (1 or 2)")

# the game: loads its graphics from PNG atlases in assets/
set(SOURCES
  src/main.cpp src/canvas.cpp src/sprites.cpp src/track.cpp src/race.cpp src/render.cpp
  src/audio.cpp src/input.cpp src/text.cpp src/cars.cpp src/assets.cpp src/painter.cpp)
# the asset tool: paints / renders all graphics procedurally and writes the PNG atlases
set(TOOL_SOURCES
  tools/assetgen.cpp tools/icongen.cpp src/canvas.cpp src/painter.cpp src/sprites.cpp src/spritegen.cpp src/track.cpp
  src/text.cpp src/cars.cpp src/cargen.cpp src/assets.cpp)

# the TrueType fonts are compiled into the executable (no asset files to ship)
include(cmake/EmbedFiles.cmake)
set(FONT_FILES ${CMAKE_SOURCE_DIR}/fonts/RussoOne-Regular.ttf ${CMAKE_SOURCE_DIR}/fonts/ChakraPetch-BoldItalic.ttf)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FONT_FILES})
embed_files(${CMAKE_BINARY_DIR}/generated/font_data.h ${FONT_FILES})

if(PLATFORM STREQUAL "SWITCH")
  # Configure with the devkitPro toolchain, e.g.
  #   cmake -B build-switch -DPLATFORM=SWITCH -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET SDL2_ttf sdl2)
  add_executable(turboracingmayhem ${SOURCES})
  target_include_directories(turboracingmayhem PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party/stb)
  target_compile_definitions(turboracingmayhem PRIVATE TURBO_RENDER_SCALE=${TURBO_RENDER_SCALE})
  target_compile_options(turboracingmayhem PRIVATE -O3 -ffunction-sections)
  target_link_libraries(turboracingmayhem PRIVATE PkgConfig::SDL2)
  # the assets folder goes into the .nro's romfs (read as romfs:/assets)
  file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/romfs)
  file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/romfs)
  nx_generate_nacp(turboracingmayhem.nacp NAME "Turbo Racing Mayhem" AUTHOR "rsn8887" VERSION ${PROJECT_VERSION})
  nx_create_nro(turboracingmayhem NACP turboracingmayhem.nacp ICON ${CMAKE_SOURCE_DIR}/icon/icon_switch.jpg ROMFS ${CMAKE_BINARY_DIR}/romfs)
elseif(ANDROID)
  # Android: configured by Gradle (see android/), which passes the SDL2 and SDL2_ttf source trees. SDL's Java
  # activity loads libSDL2.so and the game, libmain.so; SDL2_ttf (with its bundled FreeType) is linked in statically.
  # The assets folder is packed into the APK by Gradle and read through SDL (SDL_RWFromFile reads APK assets).
  if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt" OR NOT EXISTS "${SDL2_TTF_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Android: set SDL2_SOURCE_DIR and SDL2_TTF_SOURCE_DIR to the SDL2 and SDL2_ttf source trees")
  endif()
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2)
  # SDL2_ttf is built static, so it looks for the static SDL2 target: point it at the shared one
  if(NOT TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2-static ALIAS SDL2)
  endif()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SDL2_TTF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2_ttf EXCLUDE_FROM_ALL)
  add_library(main SHARED ${SOURCES})
  target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party/stb)
  target_compile_definitions(main PRIVATE TURBO_RENDER_SCALE=${TURBO_RENDER_SCALE})
  target_compile_options(main PRIVATE -Wall -Wextra -O3)
  target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_ttf)
  find_library(ANDROID_LOG log)
  if(ANDROID_LOG)
    target_link_libraries(main PRIVATE ${ANDROID_LOG})
  endif()
else()
  find_package(SDL2 REQUIRED)
  find_package(Threads REQUIRED)
  if(APPLE)
    # macOS: the game is an app bundle (TurboRacingMayhem.app) with its icon and the assets in Contents/Resources
    set(ICNS ${CMAKE_SOURCE_DIR}/icon/TurboRacingMayhem.icns)
    set_source_files_properties(${ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(turboracingmayhem MACOSX_BUNDLE ${SOURCES} ${ICNS})
    set_target_properties(turboracingmayhem PROPERTIES
      OUTPUT_NAME TurboRacingMayhem
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "Turbo Racing Mayhem"
      MACOSX_BUNDLE_GUI_IDENTIFIER com.rsn8887.turboracingmayhem
      MACOSX_BUNDLE_ICON_FILE TurboRacingMayhem.icns
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_COPYRIGHT "rsn8887")
    # room in the header so make_dmg.sh can point the library references into the app
    target_link_options(turboracingmayhem PRIVATE -Wl,-headerpad_max_install_names)
    set(ASSETS_DEST $<TARGET_BUNDLE_CONTENT_DIR:turboracingmayhem>/Resources/assets)
  else()
    add_executable(turboracingmayhem ${SOURCES})
    set(ASSETS_DEST $<TARGET_FILE_DIR:turboracingmayhem>/assets)
  endif()
  add_executable(turbo_assetgen ${TOOL_SOURCES})
  set(GAME_TARGETS turboracingmayhem turbo_assetgen)
  find_package(SDL2_ttf CONFIG QUIET)
  if(NOT TARGET SDL2_ttf::SDL2_ttf AND NOT TARGET SDL2_ttf::SDL2_ttf-static)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
  endif()
  foreach(T ${GAME_TARGETS})
    target_link_libraries(${T} PRIVATE Threads::Threads)
    target_include_directories(${T} PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/third_party/stb)
    target_compile_definitions(${T} PRIVATE TURBO_RENDER_SCALE=${TURBO_RENDER_SCALE})
    if(TARGET SDL2::SDL2)
      target_link_libraries(${T} PRIVATE $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> SDL2::SDL2)
    else()
      target_include_directories(${T} PRIVATE ${SDL2_INCLUDE_DIRS})
      target_link_libraries(${T} PRIVATE ${SDL2_LIBRARIES})
    endif()
    if(TARGET SDL2_ttf::SDL2_ttf)
      target_link_libraries(${T} PRIVATE SDL2_ttf::SDL2_ttf)
    elseif(TARGET SDL2_ttf::SDL2_ttf-static)
      target_link_libraries(${T} PRIVATE SDL2_ttf::SDL2_ttf-static)
    else()
      target_link_libraries(${T} PRIVATE PkgConfig::SDL2TTF)
    endif()
    if(MSVC)
      target_compile_options(${T} PRIVATE /W3 /D_USE_MATH_DEFINES)
    else()
      target_compile_options(${T} PRIVATE -Wall -Wextra -O3)
    endif()
  endforeach()
  # the game looks for assets/ next to its executable (macOS: in the app's Resources): copy them there after every build
  # (on every build, so regenerated or edited PNGs are picked up even when the game itself is unchanged)
  add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${ASSETS_DEST}
    COMMENT "Copying assets/ next to the game")
  # cmake --build build --target assets   regenerates assets/ in the source tree (overwrites hand edits!)
  add_custom_target(assets COMMAND turbo_assetgen ${CMAKE_SOURCE_DIR}/assets DEPENDS turbo_assetgen
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMENT "Generating assets/ with turbo_assetgen")
  # cmake --build build --target icons   repaints the app icons in icon/
  add_custom_target(icons COMMAND turbo_assetgen --icons ${CMAKE_SOURCE_DIR}/icon DEPENDS turbo_assetgen
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMENT "Painting the app icons into icon/")
  if(WIN32)
    set_target_properties(turboracingmayhem PROPERTIES WIN32_EXECUTABLE ON)
  endif()
endif()
