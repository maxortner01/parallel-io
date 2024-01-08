/**
 * @file io.hh
 * @author Max Ortner (mortner@lanl.gov)
 * @brief Data structures for basic I/O.
 * @version 0.1
 * @date 2023-10-05
 * 
 * @copyright Copyright (c) 2023, Triad National Security, LLC
 */

#pragma once

#include "./io/type.hh"
#include "./io/result.hh"
#include "./io/promise.hh"
#include "./io/distributor.hh"
#include "./io/span.hh"

#ifndef READ_TEMP
#define READ_TEMP typename = std::enable_if<_Access == io::access::ro || _Access == io::access::rw, bool>
#endif 

#ifndef READ
#define READ template<READ_TEMP>
#endif

#ifndef WRITE_TEMP
#define WRITE_TEMP typename = std::enable_if<_Access == io::access::wo || _Access == io::access::rw, bool>
#endif

#ifndef WRITE
#define WRITE template<WRITE_TEMP>
#endif