cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 17)

project(pio)

# Grab include directories and lib files
find_library(EXODUS exodus)
find_library(PNETCDF pnetcdf)
find_library(MPI mpi)
find_path(MPICH_INCLUDE_DIR mpi.h)

# Include the current spack view
include_directories(${MPICH_INCLUDE_DIR})

add_executable(pio main.cpp)

target_link_libraries(pio ${MPI} ${PNETCDF} ${EXODUS})