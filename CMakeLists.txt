cmake_minimum_required(VERSION 3.20)

project(probable_disco_voxel_forest LANGUAGES CXX)

set(WEB_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Static Web/WASM output directory")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(voxel_core STATIC
  engine/core/app.cpp
  engine/core/audio.cpp
  engine/core/forest_audio.cpp
  engine/core/subtitles.cpp
  engine/render/bitmap_font.cpp
  engine/render/gles_renderer.cpp
  engine/render/quad_batch.cpp
  engine/world/chunk.cpp
  engine/world/generator.cpp
  engine/world/mesher.cpp
  engine/game/camera.cpp
  engine/game/fox.cpp
)

target_include_directories(voxel_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/core
)

target_compile_features(voxel_core PUBLIC cxx_std_17)

if(EMSCRIPTEN)
  target_compile_options(voxel_core PUBLIC "SHELL:-s USE_SDL=2")

  add_executable(voxel_forest_web
    platform/web/main_web.cpp
    platform/web/web_renderer.cpp
  )

  target_include_directories(voxel_forest_web
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/platform/web
  )

  target_link_libraries(voxel_forest_web PRIVATE voxel_core)

  target_link_options(voxel_forest_web
    PRIVATE
      "SHELL:-s WASM=1"
      "SHELL:-s USE_WEBGL2=1"
      "SHELL:-s MIN_WEBGL_VERSION=2"
      "SHELL:-s MAX_WEBGL_VERSION=2"
      "SHELL:-s FULL_ES3=1"
      "SHELL:-s USE_SDL=2"
      "SHELL:-s ALLOW_MEMORY_GROWTH=1"
      "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html"
  )

  set_target_properties(voxel_forest_web PROPERTIES
    OUTPUT_NAME "index"
    RUNTIME_OUTPUT_DIRECTORY "${WEB_DIST_DIR}"
    SUFFIX ".html"
  )

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET voxel_forest_web POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${WEB_DIST_DIR}/assets"
      COMMENT "Copying web assets to ${WEB_DIST_DIR}/assets"
    )
  endif()
endif()
