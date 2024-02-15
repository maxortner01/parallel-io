cmake_minimum_required(VERSION 3.2)

project(pio
    LANGUAGES CXX
    VERSION 0.1
)

#option(COMPILE_EXEC "Compile test executable" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)

add_subdirectory(pio)