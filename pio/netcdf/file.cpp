#include "file.hh"

#include <iostream>
#include <numeric>

#define FWD_DEC_WRITE_O(ret, name, acc, ...) template ret file<io::access::acc>::name(__VA_ARGS__)
#define FWD_DEC_READ_O(ret, name, access, ...) FWD_DEC_WRITE_O(ret, name, access, __VA_ARGS__) const

#define FWD_DEC_WRITE(ret, name, ...) FWD_DEC_WRITE_O(ret, name, wo, __VA_ARGS__); FWD_DEC_WRITE_O(ret, name, rw, __VA_ARGS__)
#define FWD_DEC_READ(ret, name, ...) FWD_DEC_READ_O(ret, name, ro, __VA_ARGS__); FWD_DEC_READ_O(ret, name, rw, __VA_ARGS__)

namespace pio::netcdf
{
    template<io::access _Access>
    file<_Access>::file(const std::string& filename)
    {
        if constexpr (_Access == io::access::ro || _Access == io::access::rw)
        {
            err = ncmpi_open(
                MPI_COMM_WORLD, 
                filename.c_str(),
                (_Access == io::access::ro?NC_NOWRITE:NC_WRITE),
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

    template<io::access _Access>
    file<_Access>::~file()
    { close(); }

    template<io::access _Access>
    void file<_Access>::close() 
    { if (_good) err = ncmpi_close(handle); }

#pragma region READ

    template<io::access _Access>
    template<typename>
    io::result<std::vector<std::string>> 
    file<_Access>::variable_names() const
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
    FWD_DEC_READ(io::result<std::vector<std::string>>, variable_names);

    template<io::access _Access>
    template<typename>
    io::result<variable> 
    file<_Access>::get_variable_info(const std::string& name) const
    {
        const auto inq = inquire();
        if (!inq.good()) return { inq.error() };

        int index = -1;
        auto err = ncmpi_inq_varid(handle, name.c_str(), &index);
        if (err != NC_NOERR || index < 0) return { err };

        int dimensions = 0;
        variable var;
        var.index = index;
        err = ncmpi_inq_varndims(handle, index, &dimensions);
        if (err != NC_NOERR) return { err };

        std::vector<int> dim_ids(dimensions);
        var.dimensions.resize(dimensions);

        char var_name[MAX_STR_LENGTH];
        err = ncmpi_inq_var(
            handle, 
            index, 
            var_name, 
            &var.type, 
            &dimensions, 
            dim_ids.data(), 
            &var.attributes
        );
        if (err != NC_NOERR) return { err };

        for (uint32_t i = 0; i < dimensions; i++)
            var.dimensions[i] = get_dimension(dim_ids[i]).value();

        return { var };
    }
    FWD_DEC_READ(io::result<variable>, get_variable_info, const std::string&);

    template<io::access _Access>
    template<typename>
    io::result<value_info>
    file<_Access>::get_variable_value_info(const std::string& name) const
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
        ret.size = 1;

        const auto dim_lengths = get_dimension_lengths();
        if (!dim_lengths.good()) return { dim_lengths.error() };

        const auto dim_sizes = dim_lengths.value();

        for (const auto& dim : info.value().dimensions)
            ret.size *= dim.length;

        return { ret };
    }
    FWD_DEC_READ(io::result<value_info>, get_variable_value_info, const std::string&);

    using map_type = std::unordered_map<std::string, MPI_Offset>;
    template<io::access _Access>
    template<typename>
    io::result<map_type>
    file<_Access>::get_dimension_lengths() const
    {
        const auto inq = inquire();
        if (!inq.good()) return { inq.error() };

        std::unordered_map<std::string, MPI_Offset> map;
        for (uint32_t i = 0; i < inq.value().dimensions; i++)
        {
            char name[MAX_NAME_LENGTH];
            MPI_Offset offset = 0;
            auto err = ncmpi_inq_dim(handle, i, name, &offset);
            if (err != NC_NOERR) return { err };

            map.insert(std::pair(std::string(name), offset));
        }
        return { map };
    }
    FWD_DEC_READ(io::result<map_type>, get_dimension_lengths);

    template<io::access _Access>
    template<typename>
    io::result<info>
    file<_Access>::inquire() const
    {
        info i;
        int unlimited;
        int error = ncmpi_inq(handle, &i.dimensions, &i.variables, &i.attributes, &unlimited);

        if (error != NC_NOERR) return { error };
        
        return { i };
    }
    FWD_DEC_READ(io::result<info>, inquire);

    template<io::access _Access>
    template<typename _Type, typename>
    const io::promise<_Type>
    file<_Access>::get_variable_values(
        const std::string& name, 
        const std::vector<MPI_Offset>& start,
        const std::vector<MPI_Offset>& count) const
    {
        const auto info = get_variable_value_info(name);
        if (!info.good()) return { info.error() };
        if (info.value().type != _Type::nc) return { -1 };

        io::promise<_Type> promise(handle, { info.value().size }, 1);
        const auto err = _Type::func(
            handle,
            info.value().index,
            start.data(),
            count.data(),
            promise.template get<0>().value(),
            promise.requests()
        );
        if (err != NC_NOERR) return { err };

        return promise;
    }
    FWD_DEC_READ(const io::promise<io::type<NC_CHAR>>, get_variable_values, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(const io::promise<io::type<NC_FLOAT>>, get_variable_values, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(const io::promise<io::type<NC_DOUBLE>>, get_variable_values, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(const io::promise<io::type<NC_INT>>, get_variable_values, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);

    template<io::access _Access>
    template<typename>
    io::result<dimension>
    file<_Access>::get_dimension(int id) const
    {
        dimension dim{0};
        dim.id = id;

        char _name[MAX_NAME_LENGTH];
        const auto err = ncmpi_inq_dim(handle, id, _name, &dim.length);
        if (err != NC_NOERR) return { err };

        dim.name = std::string(_name);

        return { dim };
    }
    
    template<io::access _Access>
    template<typename>
    io::result<dimension>
    file<_Access>::get_dimension(const std::string& name) const
    {
        int id = 0;
        const auto err = ncmpi_inq_dimid(handle, name.c_str(), &id);
        if (err != NC_NOERR) return { err };

        return get_dimension(id);
    }

#pragma endregion READ
    
#pragma region WRITE

    template<io::access _Access>
    template<typename _Type, typename>
    const io::promise<_Type>
    file<_Access>::write_variable(
        const std::string& name,
        const typename _Type::integral_type* data,
        const std::size_t& size,
        const std::vector<MPI_Offset>& offset,
        const std::vector<MPI_Offset>& count)
    {
        if (!data || !size) return { 0 };
        if (offset.size() != count.size()) return { 1 };
        
        // data_size is equivalent to volume of data
        const auto product = std::accumulate(count.begin(), count.end(), 1, std::multiplies<size_t>());
        if (product != size) return { 2 };
        
        variable var{0};
        if constexpr (_Access == io::access::rw)
        {
            const auto info = get_variable_info(name);
            if (!info.good()) return { info.error() };
            var = std::move(info.value());
        }
        else assert(false);

        if (_Type::nc != var.type) return { 3 };
        if (var.dimensions.size() != offset.size()) return { 4 };

        io::promise<_Type> promise(handle, { 0 }, 1);
        const auto err = ncmpi_iput_vara(
            handle,
            var.index,
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
    FWD_DEC_WRITE(const io::promise<types::Char>, write_variable, const std::string&, const char*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_WRITE(const io::promise<types::Int>, write_variable, const std::string&, const int*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_WRITE(const io::promise<types::Double>, write_variable, const std::string&, const double*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_WRITE(const io::promise<types::Float>, write_variable, const std::string&, const float*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);

#pragma endregion WRITE

    template struct file<io::access::ro>;
    template struct file<io::access::wo>;
    template struct file<io::access::rw>;
}