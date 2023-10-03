#pragma once

#include "../io.hh"

#include <numeric>
#include <cassert>
#include <string>
#include <functional>

// http://cucis.ece.northwestern.edu/projects/PnetCDF/doc/pnetcdf-c
namespace pio::netcdf
{
    template<typename T, uint32_t _Dimension>
    struct Point
    {
        T data[_Dimension];

        T& operator[](uint32_t index) 
        {
            assert(index < _Dimension);
            return data[index];
        }
    };

    namespace impl 
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
    
    template<io::access _Access>
    struct file_base
    {
        file_base(const std::string& filename)
        {
            if constexpr (_Access == io::access::ro)
            {
                err = ncmpi_open(
                    MPI_COMM_WORLD, 
                    filename.c_str(),
                    (_Access == io::access::ro?NC_NOWRITE:NC_EWRITE),
                    MPI_INFO_NULL,
                    &handle
                );
            }

            if constexpr (_Access == io::access::wo)
            {
                MPI_Info info;
                MPI_Info_create(&info);

                err = ncmpi_create(
                    MPI_COMM_WORLD, 
                    filename.c_str(),
                    NC_NOCLOBBER | NC_64BIT_OFFSET,
                    info,
                    &handle
                );

                MPI_Info_free(&info);
            }

            if (err != NC_NOERR) _good = false;
            else _good = true;
        }

        file_base(const file_base&) = delete;

        ~file_base()
        { close(); }

        void close()
        {
            if (_good) err = ncmpi_close(handle);
        }

        auto good() const { return _good; }
        operator bool() const { return good(); }

    protected:
        int handle, err;
        bool _good;
    };

    template<io::access _Access>
    struct file_access
    {   };

    template<>
    struct file_access<io::access::ro> : file_base<io::access::ro>
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

        using base = file_base<io::access::ro>;
        using base::file_base;

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
    };

    template<>
    struct file_access<io::access::wo> : file_base<io::access::wo>
    {
    private:
        struct var
        {
            int id;
            nc_type type;
            std::vector<int> dim_ids;
        };

    public:
        using file_base<io::access::wo>::file_base;

        bool define_dimension(const std::string& name, const uint32_t& length)
        {
            int id;
            const auto err = ncmpi_def_dim(handle, name.c_str(), length, &id);
            if (err != NC_NOERR) return false;

            _dimensions.insert(std::pair(name, id));
            return true;
        }

        template<typename _Type>
        bool define_variables(
            const std::string& name, 
            const std::vector<std::string>& dimensions)
        {
            std::vector<int> dimids;
            dimids.reserve(dimensions.size());

            for (const auto& s : dimensions)
            {
                if (!_dimensions.count(s)) return false;
                dimids.push_back(_dimensions.at(s));
            }

            int id;
            const auto err = ncmpi_def_var(
                handle, 
                name.c_str(), 
                _Type::nc, 
                dimids.size(), 
                dimids.data(), 
                &id
            );

            if (err != NC_NOERR) return false;

            var v;
            v.id = id;
            v.type = _Type::nc;
            v.dim_ids = dimids;

            _variables.insert(std::pair(name, v));
            return true;
        }

        void end_define() 
        {
            const auto err = ncmpi_enddef(handle);
            if (err != NC_NOERR) _good = false;
        }
        
        template<typename _Type>
        io::promise<_Type>
        write_variable(
            const std::string& name,
            const typename _Type::type* data,
            const std::size_t& size,
            const std::vector<MPI_Offset>& offset,
            const std::vector<MPI_Offset>& count)
        {
            if (!data) return { 0 };


            if (offset.size() != count.size()) return { 1 };

            // data size needs to be product of counts
            auto product = std::accumulate(count.begin(), count.end(), 1, std::multiplies<size_t>());
            if (product != size) return { 2 };

            if (!_variables.count(name)) return { 3 };

            const auto& var = _variables.at(name);
            if (var.dim_ids.size() != offset.size()) return { 4 };

            if (_Type::nc != var.type) return { 5 };
            //err = ncmpi_iput_vara(ncfile, var1id, &start, &count, &data1, count, MPI_INT, &requests[0]);

            // Allocate the data to be retreived
            io::promise<_Type> promise(handle, { 1 }, 1);
            auto err = ncmpi_iput_vara(
                handle,
                var.id,
                offset.data(),
                count.data(),
                data,
                size,
                MPI_DATATYPE_NULL,
                promise.requests()
            );

            if (err != NC_NOERR) return { err };

            return promise;
        }

    private:
        std::unordered_map<std::string, int> _dimensions;
        std::unordered_map<std::string, var> _variables;
    };

    
    } // namespace impl

    template<io::access _Access>
    struct file : impl::file_access<_Access>
    { using impl::file_access<_Access>::file_access; };
}