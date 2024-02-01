#pragma once

/** \mainpage 
 * 
 * \section Important Information 
 * The current state of PIO is WIP, feel free to submit <a href="https://github.com/maxortner01/parallel-io/issues">an issue on github</a>. It is 
 * maintained and created by the Los Alamos National Laboratory.
 * 
 * \subsection Getting Started
 * 
 * \subsubsection Building
 * Currently, this software has three dependencies: <a href="https://www.osti.gov/servlets/purl/10102115">Exodus II</a>, 
 * <a href="https://parallel-netcdf.github.io/">Parallel NetCDF</a>, and <a href="https://www.open-mpi.org/">MPI</a>. Each of these dependencies are
 * expected to be easily found by the CMake using `find_package`. 
 * 
 * This software was made to be easily used with <a href="https://spack.io">spack</a>. Simply use 
 * \verbatim 
 * spack env create pio
 * spack env activate pio
 * spack add exodusii parallel-netcdf mpi
 * spack install \endverbatim
 * Then proceed to build in cmake as usual.
 * 
 * \subsubsection Important Objects
 * Currently only ExodusII and NetCDF files are supported. Each file implementation uses template arguments to specify the access privileges 
 * which limits methods inside the implementation that can be used via SFINAE. Exodus file functionality is handled in an instance of \ref `pio::exodus::file`
 * whereas the NetCDF file and the parallel-io along with that is handled in \ref `pio::netcdf::file`.
 * 
 * \warning The exodus file implementation does not do any parallel IO currently. Becasue of this, the creation/manipulation of an instance of 
 * \ref `pio::exodus::file` should be done in <i>one</i> process (like the root process for example). NetCDF files do <i>not</i> have this restriction.
 * 
 * \todo Adding helper methods for reading and writing using the exodus instance are planned. For now, since exodus files <i>are</i> netcdf files, you can
 * manipulate their data using the latter implementation.
 */

/// Functionality for parallel reading and writing ExodusII and NetCDF files
namespace pio {}

#include "io.hh"

#include "exodus/ex_file.hh"
#include "netcdf/net_file.hh"