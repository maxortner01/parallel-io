#pragma once

#include "../io.hh"

#include <cassert>
#include <string>
#include <functional>

// http://cucis.ece.northwestern.edu/projects/PnetCDF/doc/pnetcdf-c
namespace pio::netcdf
{
    // https://stackoverflow.com/questions/54268425/enumerating-over-a-fold-expression
    template<std::size_t... inds, class F>
    constexpr void static_for_impl(std::index_sequence<inds...>, F&& f)
    {
        (f(std::integral_constant<std::size_t, inds>{}), ...);
    }

    template<std::size_t N, class F>
    constexpr void static_for(F&& f)
    {
        static_for_impl(std::make_index_sequence<N>{}, std::forward<F>(f));
    }
    
    template<int N, typename... Ts>
    using NthType = typename std::tuple_element<N, std::tuple<Ts...>>::type;

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

        struct value_info
        {
            uint32_t index, size;
            int type;
            std::vector<MPI_Offset> start, count;

            value_info() : index(0), size(0) { }
        };

        io::result<value_info>
        get_variable_value_info(const std::string& name)
        {
            // Get MPI info
            const auto [nprocs, rank] = []() -> auto
            {
                int nprocs, rank;
                MPI_Comm_rank(MPI_COMM_WORLD, &rank);
                MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
                return std::tuple(nprocs, rank);
            }();

            const auto info = get_variable_info(name);
            if (!info.good()) return { info.error() };

            value_info ret;
            ret.type = info.value().type;
            ret.index = info.value().index;

            ret.start = std::vector<MPI_Offset>(info.value().dimensions);
            ret.count = std::vector<MPI_Offset>(info.value().dimensions);

            const auto dim_sizes = get_dimension_lengths().value();
            ret.start[0] = (dim_sizes[info.value().dimension_ids[0]] / nprocs)*rank;
            ret.count[0] = (dim_sizes[info.value().dimension_ids[0]] / nprocs);
            ret.size = ret.count[0];

            for (uint32_t j = 1; j < info.value().dimensions; j++)
            {
                ret.start[j] = 0;
                ret.count[j] = dim_sizes[info.value().dimension_ids[j]];
                ret.size *= ret.count[j];
            }

            return { ret };
        }

        // Each variable is same type and all share memory region,
        // but still multiple requests
        template<typename _Type>
        const io::promise<_Type>
        get_variable_values(const std::vector<std::string>& names)
        {
            const auto VAR_COUNT = names.size();
            assert(VAR_COUNT);

            // Get MPI info
            const auto [nprocs, rank] = []() -> auto
            {
                int nprocs, rank;
                MPI_Comm_rank(MPI_COMM_WORLD, &rank);
                MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
                return std::tuple(nprocs, rank);
            }();

            uint32_t var_size = 0;
            std::vector<uint32_t> sizes;

            std::vector<io::result<value_info>> infos;
            infos.reserve(VAR_COUNT);
            sizes.reserve(VAR_COUNT);

            for (uint32_t i = 0; i < VAR_COUNT; i++)
            {
                auto info = get_variable_value_info(names[i]);
                assert(info.good());
                assert(info.value().type == _Type::nc);

                var_size += info.value().size;
                sizes.push_back(info.value().size);
                infos.push_back(info);
            }

            // Allocate the data to be retreived
            io::promise<_Type> promise(handle, { var_size }, VAR_COUNT);

            std::size_t offset = 0;
            for (uint32_t i = 0; i < VAR_COUNT; i++)
            {
                std::cout << "_Type::func(\n";
                std::cout << "  " << handle << ",\n";
                std::cout << "  " << infos[i].value().index << ",\n";
                std::cout << "  " << infos[i].value().start.data() << ",\n";
                std::cout << "  " << infos[i].value().count.data() << ",\n";
                std::cout << "  " << promise.template get<0>().value() + offset << ",\n";
                std::cout << "  " << &promise.requests()[i] << "\n)\n";

                const auto err = _Type::func(
                    handle, 
                    infos[i].value().index,
                    infos[i].value().start.data(),
                    infos[i].value().count.data(),
                    promise.template get<0>().value() + offset,
                    &promise.requests()[i]
                );

                offset += sizes[i];
            }

            return promise;
        }

        // Each variable is different type and has unique memory region
        template<typename... _Types>
        const io::promise<_Types...>
        get_variable_values(const std::array<std::string, sizeof...(_Types)>& names)
        {
            constexpr auto TYPE_COUNT = sizeof...(_Types);
            static_assert(TYPE_COUNT);

            // Get MPI info
            const auto [nprocs, rank] = []() -> auto
            {
                int nprocs, rank;
                MPI_Comm_rank(MPI_COMM_WORLD, &rank);
                MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
                return std::tuple(nprocs, rank);
            }();

            // Set up the array of counts
            std::array<uint32_t, TYPE_COUNT> sizes;

            std::vector<io::result<value_info>> infos;
            infos.reserve(TYPE_COUNT);
            for (uint32_t i = 0; i < TYPE_COUNT; i++)
            {
                auto info = get_variable_value_info(names[i]);
                assert(info.good());

                sizes[i] = info.value().size;
                infos.push_back(info);
            }

            // Allocate the data to be retreived
            io::promise<_Types...> promise(handle, sizes);

            // "Iterate" through and make a request for each type supplied
            static_for<TYPE_COUNT>([&](auto n) {
                constexpr std::size_t I = n;

                using _Type = NthType<I, _Types...>;
                assert(infos[I].value().type == _Type::nc);

                // once we fill out start and count this should be it... 
                // we need to check if .good is true, if it's not 
                // maybe promise.template get<I>().make_error(info.error()); where make_error is a non-const request method

                const auto err = _Type::func(
                    handle, 
                    infos[I].value().index,
                    infos[I].value().start.data(),
                    infos[I].value().count.data(),
                    promise.template get<I>().value(),
                    &promise.requests()[I]
                );
            });

            return promise;
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

        /*
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
                
                //ncmpi_get_var_c
                //ncmpi_inq_att(handle, i, )

            }
        }
        */

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