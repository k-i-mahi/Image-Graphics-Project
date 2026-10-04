cmake_minimum_required(VERSION 3.15)
project(NightWatch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # lets editors (clangd) find the include paths

# Configure GLFW build options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/CMakeLists.txt")
    add_subdirectory(extern/glfw)
endif()

# Image processing algorithms: plain C++, no OpenGL, unit-tested
add_library(imageops STATIC src/ImageOps.cpp include/ImageOps.h)
target_include_directories(imageops PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
    src/Shader.cpp
    src/Camera.cpp
    src/Kinematics.cpp
    src/Geometry.cpp
    src/FBO.cpp
    src/DIPProcessor.cpp
    src/Town.cpp
    src/TownActors.cpp
    src/Traffic.cpp
    src/Overlay2D.cpp
    src/FilterLab.cpp
    src/Bloom.cpp
    src/CctvAnalytics.cpp
    src/PerfStats.cpp
    src/Environment.cpp
    src/ShadowMap.cpp
    extern/glad/src/glad.c
)

# Header files
set(HEADERS
    include/Shader.h
    include/Camera.h
    include/Kinematics.h
    include/Bezier.h
    include/Geometry.h
    include/Lighting.h
    include/FBO.h
    include/DIPProcessor.h
    include/Town.h
    include/Traffic.h
    include/Kernels.h
    include/Overlay2D.h
    include/FilterLab.h
    include/Bloom.h
    include/CctvAnalytics.h
    include/PerfStats.h
    include/Environment.h
    include/ShadowMap.h
)

add_executable(NightWatch ${SOURCES} ${HEADERS})

target_include_directories(NightWatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include
)

target_link_libraries(NightWatch PRIVATE
    imageops
    glfw
    opengl32
)

# Copy shaders to output directory at build time
add_custom_command(TARGET NightWatch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:NightWatch>/shaders
)

# MinGW: link the C/C++ runtime statically so the exe runs without MinGW DLLs on PATH
if(MINGW)
    target_link_options(NightWatch PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit tests:  ctest --test-dir build-mingw   (or run imageops_tests.exe)
enable_testing()
add_executable(imageops_tests tests/test_imageops.cpp)
target_link_libraries(imageops_tests PRIVATE imageops)
if(MINGW)
    target_link_options(imageops_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME imageops COMMAND imageops_tests)
