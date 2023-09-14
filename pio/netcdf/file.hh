#pragma once

#include "../io.hh"

#include <string>
#include <mpi.h>
#include <pnetcdf.h>

// http://cucis.ece.northwestern.edu/projects/PnetCDF/doc/pnetcdf-c
namespace pio::netcdf
{
    struct file
    {
        enum class data_type : int
        {
            CHAR, 
            DOUBLE, 
            FLOAT, 
            INT
        };

        struct info
        {
            int dimensions, variables, attributes;
        };

        struct variable
        {
            int dimensions, attributes;
            data_type type;
            std::vector<int> dimension_ids;
        };

        file(const std::string& file_location, io::access io)
        {
            err = ncmpi_open(
                MPI_COMM_WORLD, 
                file_location.c_str(),
                (io == io::access::ro?NC_NOWRITE:NC_EWRITE),
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

        io::result<std::vector<std::string>>
        variable_names() const
        {
            const auto inq = inquire();
            if (!inq.good()) return { inq.error() };

            std::vector<std::string> ret(inq.value().variables);
            for (uint32_t i = 0; i < inq.value().variables; i++)
            {
                std::string name;
                name.resize(MAX_STR_LENGTH);
                ncmpi_inq_varname(handle, i, &name[0]);
                ret[i] = name;
            }
            return { ret };
        }

        io::result<variable>
        get_variable_info(const std::string& name)
        {
            const auto inq = inquire();
            if (!inq.good()) return { inq.error() };

            int index = -1;
            for (uint32_t i = 0; i < inq.value().variables; i++)
            {
                std::string _name;
                _name.resize(MAX_STR_LENGTH);
                const auto err = ncmpi_inq_varname(handle, i, &_name[0]);
                if (err != NC_NOERR) return { err };

                if (_name == name)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0) return { index };

            variable var;
            auto err = ncmpi_inq_varndims(handle, index, &var.dimensions);
            if (err != NC_NOERR) return { err };

            var.dimension_ids.resize(var.dimensions);

            char var_name[MAX_STR_LENGTH];
            err = ncmpi_inq_var(
                handle, 
                index, 
                var_name, 
                (int*)&var.type, 
                &var.dimensions, 
                var.dimension_ids.data(), 
                &var.attributes
            );
            if (err != NC_NOERR) return { err };

            return { var };
        }

        void 
        test() const
        {
            const auto inq = inquire().value();

            int nprocs, rank;
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

            auto dim_sizes = std::vector<MPI_Offset>(inq.dimensions);
            for (uint32_t i = 0; i < inq.dimensions; i++)
            {
                auto err = ncmpi_inq_dimlen(handle, i, &dim_sizes[i]);
                if (err != NC_NOERR) { return; }
            }

            for (uint32_t i = 0; i < inq.variables; i++)
            {
                int vdims, vatts;
                auto err = ncmpi_inq_varndims(handle, i, &vdims);
                if (err < 0) return;

                std::vector<int> dimids(vdims);

                nc_type type;
                char var_name[MAX_STR_LENGTH];
                err = ncmpi_inq_var(handle, i, var_name, &type, &vdims, dimids.data(), &vatts);
                if (err < 0) return;

                std::cout << "Name: " << var_name << ", dimensions: " << vdims << ", type: " << type << "\n";

                auto start = std::vector<MPI_Offset>(vdims);
                auto count = std::vector<MPI_Offset>(vdims);

                start[0] = (dim_sizes[dimids[0]] / nprocs)*rank;
                count[0] = (dim_sizes[dimids[0]] / nprocs);
                auto var_size = count[0];

                for (uint32_t j = 1; j < vdims; j++)
                {
                    start[j] = 0;
                    count[j] = dim_sizes[dimids[j]];
                    var_size *= count[j];
                }

                for (uint32_t j = 0; j < vatts; j++)
                {
                    char att_name[MAX_STR_LENGTH];
                    err = ncmpi_inq_attname(handle, i, j, att_name);
                    if (err < 0) return;

                    MPI_Offset lenp;
                    nc_type att_type;
                    err = ncmpi_inq_att(handle, i, att_name, &att_type, &lenp);
                    if (err < 0) return;

                    std::cout << "  " << att_name << ", len: " << lenp << ", type: " << att_type << "\n";
                    
                    if (lenp > 0)
                    {

                    switch (att_type)
                    {
                    case NC_CHAR:
                    {
                        std::string values;
                        values.resize(lenp);
                        err = ncmpi_get_att(handle, i, att_name, &values[0]); 
                        std::cout << "    attribute value: " << values << "\n";
                        break;
                    }
                    
                    default:
                        break;
                    }

                    }

                }

                switch (type)
                {
                case NC_DOUBLE:
                {
                    std::vector<double> values(var_size);
                    err = ncmpi_get_vara_double_all(handle, i, start.data(), count.data(), values.data());
                    if (err < 0) return;
                    for (const auto& d : values)
                        std::cout << "  " << d << "\n";
                    break;
                }
                case NC_INT:
                {
                    std::vector<int> values(var_size);
                    err = ncmpi_get_vara_int_all(handle, i, start.data(), count.data(), values.data());
                    if (err < 0) return;
                    for (const auto& d : values)
                        std::cout << "  " << d << "\n";
                    break;
                }
                case NC_CHAR:
                {
                    std::string values;
                    values.resize(var_size);
                    err = ncmpi_get_vara_text_all(handle, i, start.data(), count.data(), &values[0]);
                    if (err < 0) return;
                    std::cout << values << "\n";
                    break;
                }
                }

                /*
                */
                
                //ncmpi_get_var_c
                //ncmpi_inq_att(handle, i, )

            }
        }

        io::result<info> 
        inquire() const
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

const char* operator*(pio::netcdf::file::data_type t)
{
    switch (t)
    {
    case pio::netcdf::file::data_type::CHAR:   return "char";
    case pio::netcdf::file::data_type::DOUBLE: return "double";
    case pio::netcdf::file::data_type::FLOAT:  return "float";
    case pio::netcdf::file::data_type::INT:    return "int";
    default: return "";
    }
}