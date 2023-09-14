#pragma once

#include "../io.hh"

#include <string>
#include <mpi.h>
#include <pnetcdf.h>

namespace pio::netcdf
{
    struct file
    {
        struct info
        {
            int dimensions, variables, attributes;
        };

        file(const std::string& file_location, io::access io)
        {
            err = ncmpi_open(
                MPI_COMM_WORLD, 
                file_location.c_str(),
                (io == io::access::ro?NC_EREAD:NC_EWRITE),
                MPI_INFO_NULL,
                &handle
            );

            if (err != NC_NOERR) _good = false;
            else _good = true;
        }

        file(const file&) = delete;

        ~file()
        { close(); }

        void close()
        {
            if (good())
                err = ncmpi_close(handle);
        }

        io::result<info> inquire() const
        {
            info i;
            int unlimited;
            int error = ncmpi_inq(handle, &i.dimensions, &i.variables, &i.attributes, &unlimited);

            if (error != NC_NOERR) return { error };
            
            return { i };
        }

        auto error_string() const { return std::string(ncmpi_strerror(err)); }
        auto error() const { return err; }
        bool good()  const { return _good; }

        operator bool() const
        {
            return good();
        }

    private:
        int handle, err;
        bool _good;
    };
}