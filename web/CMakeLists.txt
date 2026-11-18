cmake_minimum_required(VERSION 3.24)
project(SeedLens VERSION 1.0.0 LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
  enable_language(RC)
  if(NOT MINGW)
    message(FATAL_ERROR "Use MinGW-w64 (w64devkit recommended) or Emscripten.")
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EMSCRIPTEN)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT HAVE_IPO)
  if(HAVE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  add_compile_options(-fwrapv -fno-strict-aliasing -ffp-contract=off -fmerge-all-constants -ffunction-sections -fdata-sections)
  add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
  add_link_options(-static -s -Wl,--gc-sections,--stack,8388608,--no-insert-timestamp)
else()
  # WebAssembly (Emscripten) 共通コンパイル設定
  add_compile_options(-fwrapv -fno-strict-aliasing -ffp-contract=off -sUSE_SDL=2)
endif()

# --- ワールド生成エンジン（Java / Bedrock 共通） ---
file(GLOB JAVA_CORE CONFIGURE_DEPENDS third_party/cubiomes_java/*.c)
list(FILTER JAVA_CORE EXCLUDE REGEX "tests\\.c$")
file(GLOB_RECURSE JAVA_FEATURES CONFIGURE_DEPENDS third_party/cubiomes_java/features/*.c third_party/cubiomes_java/loot/*.c)
add_library(java_worldgen STATIC ${JAVA_CORE} ${JAVA_FEATURES} src/backend_java.c)
target_compile_options(java_worldgen PRIVATE -O2 -w)

file(GLOB BEDROCK_CORE CONFIGURE_DEPENDS third_party/cubiomes_bedrock/*.c)
list(FILTER BEDROCK_CORE EXCLUDE REGEX "tests\\.c$")
add_library(bedrock_worldgen STATIC ${BEDROCK_CORE} src/backend_bedrock.c)
target_compile_options(bedrock_worldgen PRIVATE -O2 -w -include ${CMAKE_CURRENT_SOURCE_DIR}/src/bedrock_symbols.h)

add_library(seedlens_core STATIC src/engine.cpp src/persistence.cpp)
target_include_directories(seedlens_core PUBLIC src)
target_link_libraries(seedlens_core PUBLIC java_worldgen bedrock_worldgen)

if(NOT EMSCRIPTEN)
  target_link_libraries(seedlens_core PUBLIC ole32 shell32 uuid)
endif()

# --- ImGui と実行ファイル（プラットフォーム別分岐） ---
if(NOT EMSCRIPTEN)
  # [Windows ビルド]
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
  )
  target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
  target_compile_options(imgui PRIVATE -Os -w)

  add_executable(SeedLens WIN32 src/main.cpp src/ui.cpp src/export.cpp src/export_common.cpp src/platform_win32.cpp assets/app.rc)
  target_include_directories(SeedLens PRIVATE assets ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(SeedLens PRIVATE -Os -Wall -Wextra)
  target_link_options(SeedLens PRIVATE -municode)
  target_link_libraries(SeedLens PRIVATE seedlens_core imgui d3d11 dxgi d3dcompiler dwmapi comdlg32 windowscodecs user32 gdi32 imm32 advapi32)
else()
  # [WebAssembly ビルド]
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
  target_compile_options(imgui PRIVATE -Os -w -sUSE_SDL=2)

  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  add_executable(SeedLens src/main_web.cpp src/ui.cpp src/export_common.cpp src/platform_web.cpp)
  target_include_directories(SeedLens PRIVATE assets ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(SeedLens PRIVATE -O3 -Wall -Wextra -sUSE_SDL=2)
  target_link_libraries(SeedLens PRIVATE seedlens_core imgui)
  target_link_options(SeedLens PRIVATE
    -sUSE_SDL=2
    -sUSE_WEBGL2=1
    -sFULL_ES3=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=8388608
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']
    --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
  )
endif()
