cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 17)

project(pio_project)

# Grab include directories and lib files
find_library(EXODUS exodus)
find_library(PNETCDF pnetcdf)
find_library(MPI mpi)
find_path(MPICH_INCLUDE_DIR mpi.h)

set(PIO_SOURCE
    ${CMAKE_SOURCE_DIR}/pio/netcdf/file.cpp
    ${CMAKE_SOURCE_DIR}/pio/io/type.cpp
    ${CMAKE_SOURCE_DIR}/pio/io/distributor.cpp)

add_library(pio STATIC ${PIO_SOURCE})

# Include the current spack view
include_directories(pio PRIVATE ${MPICH_INCLUDE_DIR})
link_libraries(pio ${MPI} ${PNETCDF} ${EXODUS})

add_executable(cpfile main.cpp)

target_link_libraries(cpfile pio)
