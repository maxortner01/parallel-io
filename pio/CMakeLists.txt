
set(CMAKE_CXX_STANDARD 17)

add_library(pio
    ${CMAKE_CURRENT_SOURCE_DIR}/exodus/ex_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/netcdf/net_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/type.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/distributor.cpp
)
add_library(pio::pio ALIAS pio)

target_include_directories(pio 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE 
        ${MPICH_INCLUDE_DIR}
)

set_target_properties(pio PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Grab include directories and lib files
find_library(EXODUS exodus REQUIRED)
find_library(PNETCDF pnetcdf REQUIRED)
find_library(MPI mpi REQUIRED)
find_path(MPICH_INCLUDE_DIR mpi.h)

target_link_libraries(pio PUBLIC ${MPI} ${PNETCDF} ${EXODUS})

#if (COMPILE_EXEC)
#    add_executable(cpfile main.cpp)
#    target_link_libraries(cpfile pio)
#endif()

include(GNUInstallDirs)

install(TARGETS pio
    EXPORT pio-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/pio
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT pio-targets
    FILE pio-targets.cmake
    NAMESPACE pio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pio
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/pio-config.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/pio-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pio
)

write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/cmake/pio-config-version.cmake
    VERSION ${pio_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
    FILES
        ${CMAKE_BINARY_DIR}/cmake/pio-config.cmake
        ${CMAKE_BINARY_DIR}/cmake/pio-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pio
)

export(EXPORT pio-targets
    FILE ${CMAKE_BINARY_DIR}/cmake/pio-targets.cmake
    NAMESPACE pio::
)
