cmake_minimum_required(VERSION 3.18)
project(qwerty VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ──────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -O3 -march=native
    -fvisibility=hidden
)

# ── Sources ─────────────────────────────────────────────────────────────
set(QWERTY_SOURCES
    src/qwerty.c
    src/backend_evdev.c
    src/backend_x11.c
)

# ── X11 optional detection ──────────────────────────────────────────────
find_package(X11 QUIET)
if(X11_FOUND)
    message(STATUS "[qwerty] X11 backend: enabled")
    add_compile_definitions(QWERTY_HAS_X11)
else()
    message(STATUS "[qwerty] X11 backend: disabled (X11 not found)")
endif()

# ── Shared library ──────────────────────────────────────────────────────
add_library(qwerty_shared SHARED ${QWERTY_SOURCES})
set_target_properties(qwerty_shared PROPERTIES OUTPUT_NAME qwerty)
target_include_directories(qwerty_shared PUBLIC include)
target_link_libraries(qwerty_shared PRIVATE pthread)
if(X11_FOUND)
    target_link_libraries(qwerty_shared PRIVATE ${X11_LIBRARIES})
    target_include_directories(qwerty_shared PRIVATE ${X11_INCLUDE_DIR})
endif()

# ── Static library ──────────────────────────────────────────────────────
add_library(qwerty_static STATIC ${QWERTY_SOURCES})
set_target_properties(qwerty_static PROPERTIES OUTPUT_NAME qwerty)
target_include_directories(qwerty_static PUBLIC include)
target_link_libraries(qwerty_static PRIVATE pthread)
if(X11_FOUND)
    target_include_directories(qwerty_static PRIVATE ${X11_INCLUDE_DIR})
endif()

# ── Example: polling ────────────────────────────────────────────────────
add_executable(qwerty_example_poll examples/poll_example.c)
target_link_libraries(qwerty_example_poll PRIVATE qwerty_static pthread)
if(X11_FOUND)
    target_link_libraries(qwerty_example_poll PRIVATE ${X11_LIBRARIES})
endif()
set_target_properties(qwerty_example_poll PROPERTIES OUTPUT_NAME qwerty-poll)

# ── Example: headless (for Chlorlite sandbox) ──────────────────────────
add_executable(qwerty_example_headless examples/headless_example.c)
target_link_libraries(qwerty_example_headless PRIVATE qwerty_static pthread)
if(X11_FOUND)
    target_link_libraries(qwerty_example_headless PRIVATE ${X11_LIBRARIES})
endif()
set_target_properties(qwerty_example_headless PROPERTIES OUTPUT_NAME qwerty-headless)

# ── Install ─────────────────────────────────────────────────────────────
install(TARGETS qwerty_shared qwerty_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/qwerty DESTINATION include)

message(STATUS "[qwerty] Build configured. Backends:")
message(STATUS "  evdev   : always (Linux)")
message(STATUS "  X11     : ${X11_FOUND}")
message(STATUS "  headless: always (Chlorlite sandbox)")
