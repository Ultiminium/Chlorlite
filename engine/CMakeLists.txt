cmake_minimum_required(VERSION 3.18)
project(Chlorlite VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Options ───────────────────────────────────────────────────────────
option(CC_WINDOWED "Build with GLFW windowed support" ON)
option(CC_HEADLESS "Build with OSMesa headless support" ON)
option(CC_AUDIO    "Build audio system" ON)

# ── Dependencies ──────────────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(X11 QUIET)

find_library(OSMESA_LIB OSMesa)
find_path(OSMESA_INC GL/osmesa.h)
if(OSMESA_LIB AND OSMESA_INC AND CC_HEADLESS)
    set(CC_HAS_OSMESA TRUE)
    message(STATUS "[Chlorlite] OSMesa (headless): FOUND")
elseif(NOT CC_HEADLESS)
    set(CC_HAS_OSMESA FALSE)
    message(STATUS "[Chlorlite] OSMesa: DISABLED (CC_HEADLESS=OFF, windowed-only build)")
else()
    set(CC_HAS_OSMESA FALSE)
    message(STATUS "[Chlorlite] OSMesa: NOT FOUND — headless disabled")
endif()

find_package(OpenGL QUIET)
find_package(glfw3 QUIET)
if(glfw3_FOUND AND OpenGL_FOUND)
    set(CC_HAS_GLFW TRUE)
    message(STATUS "[Chlorlite] GLFW + OpenGL (windowed): FOUND")
else()
    set(CC_HAS_GLFW FALSE)
    message(STATUS "[Chlorlite] GLFW: NOT FOUND — windowed disabled")
endif()

find_library(OPENAL_LIB openal)
find_library(SNDFILE_LIB sndfile)
if(OPENAL_LIB AND SNDFILE_LIB AND CC_AUDIO)
    set(CC_HAS_AUDIO TRUE)
else()
    set(CC_HAS_AUDIO FALSE)
endif()

# ── Qwerty input ──────────────────────────────────────────────────────
# qwerty is a top-level sibling library (moved out of engine/ so it's severable
# and usable standalone). It's one level up from engine/, so give an explicit
# binary dir for the out-of-tree add_subdirectory.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qwerty ${CMAKE_CURRENT_BINARY_DIR}/qwerty)

# ── Engine source ─────────────────────────────────────────────────────
# Gather ALL engine sources by glob so this never drifts from scripts/cc (the
# primary build). Any new src/*.c is picked up automatically.
file(GLOB_RECURSE CC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(claudecore STATIC ${CC_SOURCES})

target_include_directories(claudecore
    PUBLIC  include
    PRIVATE src
            src/stb
            ${CMAKE_CURRENT_SOURCE_DIR}/../qwerty/include
)

target_compile_options(claudecore PRIVATE
    -O3 -ffast-math
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)

# ── Renderer backend defines ──────────────────────────────────────────
if(CC_HAS_OSMESA)
    target_compile_definitions(claudecore PRIVATE CC_USE_OSMESA)
    target_include_directories(claudecore PRIVATE ${OSMESA_INC})
endif()
if(CC_HAS_GLFW)
    target_compile_definitions(claudecore PRIVATE CC_USE_GLFW)
endif()
if(CC_HAS_AUDIO)
    target_compile_definitions(claudecore PRIVATE CC_USE_AUDIO)
endif()

# ── Link ──────────────────────────────────────────────────────────────
target_link_libraries(claudecore
    PUBLIC  Threads::Threads qwerty_static
    PRIVATE m ${CMAKE_DL_LIBS}
)
find_library(ZSTD_LIB zstd)
if(ZSTD_LIB)
    target_link_libraries(claudecore PRIVATE ${ZSTD_LIB})
    target_compile_definitions(claudecore PRIVATE CC_HAS_ZSTD)
endif()
if(CC_HAS_OSMESA)
    target_link_libraries(claudecore PRIVATE ${OSMESA_LIB})
endif()
if(CC_HAS_GLFW)
    target_link_libraries(claudecore PRIVATE glfw OpenGL::GL)
endif()
if(X11_FOUND)
    target_link_libraries(claudecore PRIVATE ${X11_LIBRARIES})
endif()
if(CC_HAS_AUDIO)
    target_link_libraries(claudecore PRIVATE ${OPENAL_LIB} ${SNDFILE_LIB})
endif()

# ── CLI sandbox tool ──────────────────────────────────────────────────
add_executable(cc-sandbox tools/sandbox.c)
target_include_directories(cc-sandbox PRIVATE include ${CMAKE_CURRENT_SOURCE_DIR}/../qwerty/include)
target_link_libraries(cc-sandbox PRIVATE claudecore)
if(CC_HAS_OSMESA)
    target_link_libraries(cc-sandbox PRIVATE ${OSMESA_LIB})
endif()
if(CC_HAS_GLFW)
    target_link_libraries(cc-sandbox PRIVATE glfw OpenGL::GL)
endif()
if(X11_FOUND)
    target_link_libraries(cc-sandbox PRIVATE ${X11_LIBRARIES})
endif()

# ── Summary ───────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔═ Chlorlite ${PROJECT_VERSION} ══════════════════════════════════╗")
message(STATUS "║  Windowed (GLFW+OpenGL) : ${CC_HAS_GLFW}")
message(STATUS "║  Headless (OSMesa)      : ${CC_HAS_OSMESA}")
message(STATUS "║  Audio (OpenAL+sndfile) : ${CC_HAS_AUDIO}")
message(STATUS "║  Input (Qwerty)         : ON")
message(STATUS "╚═══════════════════════════════════════════════════════╝")

add_subdirectory(tools/editor)
