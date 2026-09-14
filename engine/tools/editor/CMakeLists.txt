# Link against the built claudecore library instead of raw sources
add_executable(cc-edit cc-edit.c)
target_include_directories(cc-edit PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/stb
    ${CMAKE_SOURCE_DIR}/qwerty/include
)
target_link_libraries(cc-edit PRIVATE claudecore Threads::Threads qwerty_static m)
if(CC_HAS_OSMESA)
    target_compile_definitions(cc-edit PRIVATE CC_USE_OSMESA)
    target_link_libraries(cc-edit PRIVATE ${OSMESA_LIB})
endif()
if(CC_HAS_GLFW)
    target_compile_definitions(cc-edit PRIVATE CC_USE_GLFW)
    target_link_libraries(cc-edit PRIVATE glfw OpenGL::GL)
endif()
if(X11_FOUND)
    target_link_libraries(cc-edit PRIVATE ${X11_LIBRARIES})
endif()
