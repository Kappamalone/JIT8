cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(JIT8)

# Windows specific config
IF (WIN32)
    # Link sfml statically
    set(SFML_STATIC_LIBRARIES TRUE)
ENDIF()

# Find SFML shared libraries
find_package(SFML COMPONENTS system window graphics CONFIG REQUIRED)

add_subdirectory(externals)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories (${SFML_INCLUDE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/externals/xbyak) #needed when using Visual Studio

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/gui.h
  src/jitcommon.h
  src/chip8.cpp
  src/chip8.h
  src/chip8interpreter.h
  src/chip8cachedinterpreter.h
  src/chip8dynarec.h
)

 target_link_libraries (${PROJECT_NAME} PRIVATE sfml-system sfml-network sfml-graphics sfml-window)