#pragma once

#include "../io.hh"

#include <string>
#include <functional>

// http://cucis.ece.northwestern.edu/projects/PnetCDF/doc/pnetcdf-c
namespace pio::netcdf
{
    struct file
    {
        struct info
        {
            int dimensions, variables, attributes;
        };

        struct variable
        {
            int index, dimensions, attributes, type;
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
        get_variable_info(const std::string& name) const
        {
            const auto inq = inquire();
            if (!inq.good()) return { inq.error() };

            int index = -1;
            auto err = ncmpi_inq_varid(handle, name.c_str(), &index);
            if (err != NC_NOERR || index < 0) return { err };

            variable var;
            var.index = index;
            err = ncmpi_inq_varndims(handle, index, &var.dimensions);
            if (err != NC_NOERR) return { err };

            var.dimension_ids.resize(var.dimensions);

            char var_name[MAX_STR_LENGTH];
            err = ncmpi_inq_var(
                handle, 
                index, 
                var_name, 
                &var.type, 
                &var.dimensions, 
                var.dimension_ids.data(), 
                &var.attributes
            );
            if (err != NC_NOERR) return { err };

            return { var };
        }

        template<nc_type _Type>
        const io::promise<typename io::Type<_Type>::type>
        get_variable_values(const std::string& name) const
        {
            const auto info = get_variable_info(name);
            if (!info.good()) return { info.error() };
            
            int nprocs, rank;
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

            auto start = std::vector<MPI_Offset>(info.value().dimensions);
            auto count = std::vector<MPI_Offset>(info.value().dimensions);

            const auto dim_sizes = get_dimension_lengths().value();
            start[0] = (dim_sizes[info.value().dimension_ids[0]] / nprocs)*rank;
            count[0] = (dim_sizes[info.value().dimension_ids[0]] / nprocs);
            auto var_size = count[0];

            for (uint32_t j = 1; j < info.value().dimensions; j++)
            {
                start[j] = 0;
                count[j] = dim_sizes[info.value().dimension_ids[j]];
                var_size *= count[j];
            }

            //err = ncmpi_get_vara_double_all(handle, i, start.data(), count.data(), values.data());

            using type_t = io::Type<_Type>;
            io::promise<typename io::Type<_Type>::type> promise(handle, var_size, 1);
            const auto err = type_t::func(handle, info.value().index, start.data(), count.data(), promise.value(), promise.requests());

            return promise;


            /*
            using type_t = Type<_Type>;
            io::promise<typename Type<_Type>::type> promise(handle, 10, 1);

            std::cout << "writing to location " << (size_t)promise.value() << "\n";
            const auto err = type_t::func(handle, info.value().index, promise.value(), promise.requests());Z
            
            return promise;*/
        }

        io::result<std::vector<MPI_Offset>>
        get_dimension_lengths() const
        {
            const auto inq = inquire();
            if (!inq.good()) return { inq.error() };

            auto dim_sizes = std::vector<MPI_Offset>(inq.value().dimensions);
            for (uint32_t i = 0; i < dim_sizes.size(); i++)
            {
                auto err = ncmpi_inq_dimlen(handle, i, &dim_sizes[i]);
                if (err != NC_NOERR) return { err };
            }
            return { dim_sizes };
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