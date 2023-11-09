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
    result<std::vector<std::string>> 
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
        return { std::move(ret) };
    }
    FWD_DEC_READ(result<std::vector<std::string>>, variable_names);

    template<io::access _Access>
    template<typename>
    result<variable> 
    file<_Access>::get_variable_info(const std::string& name) const
    {
        const auto inq = inquire();
        if (!inq.good()) return { inq.error() };

        int index = -1;
        auto err = ncmpi_inq_varid(handle, name.c_str(), &index);
        if (err != NC_NOERR || index < 0) return { netcdf_error(err) };

        int dimensions = 0;
        variable var{0};
        var.index = index;
        err = ncmpi_inq_varndims(handle, index, &dimensions);
        if (err != NC_NOERR) return { netcdf_error(err) };

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

        return { std::move(var) };
    }
    FWD_DEC_READ(result<variable>, get_variable_info, const std::string&);

    template<io::access _Access>
    template<typename>
    result<value_info>
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

        return { std::move(ret) };
    }
    FWD_DEC_READ(result<value_info>, get_variable_value_info, const std::string&);

    using map_type = std::unordered_map<std::string, MPI_Offset>;
    template<io::access _Access>
    template<typename>
    result<map_type>
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
        return { std::move(map) };
    }
    FWD_DEC_READ(result<map_type>, get_dimension_lengths);

    template<io::access _Access>
    template<typename>
    result<info>
    file<_Access>::inquire() const
    {
        info i;
        int unlimited;
        int error = ncmpi_inq(handle, &i.dimensions, &i.variables, &i.attributes, &unlimited);

        if (error != NC_NOERR) return { netcdf_error(error) };
        
        return { std::move(i) };
    }
    FWD_DEC_READ(result<info>, inquire);
    
    template<io::access _Access>
    template<typename _Type, typename>
    result<std::vector<typename _Type::integral_type>>
    file<_Access>::read_variable_sync(
        const std::string& name,
        const std::vector<MPI_Offset>& start,
        const std::vector<MPI_Offset>& count) const
    {
        const auto promise  = get_variable_values<_Type>(name, start, count);
        if (!promise.good()) return { promise.error() };
        const auto statuses = promise.wait();
        return { promise.template get_data<0>() };
    }
    FWD_DEC_READ(result<std::vector<int>>, read_variable_sync<types::Int>, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(result<std::vector<float>>, read_variable_sync<types::Float>, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(result<std::vector<double>>, read_variable_sync<types::Double>, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    FWD_DEC_READ(result<std::vector<char>>, read_variable_sync<types::Char>, const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);

    template<io::access _Access>
    template<typename _Type, typename>
    const promise<io::access::ro, _Type>
    file<_Access>::get_variable_values(
        const std::string& name, 
        const std::vector<MPI_Offset>& start,
        const std::vector<MPI_Offset>& count) const
    {
        const auto info = get_variable_value_info(name);
        if (!info) return { info.error() };
        if (info.value().type != _Type::nc) return { error_code::TypeMismatch };

        const std::size_t size = std::accumulate(count.begin(), count.end(), 1, std::multiplies<size_t>());

        promise<io::access::ro, _Type> promise(handle, { size });
        
        auto err = ncmpi_begin_indep_data(get_handle());
        if (err != NC_NOERR) return { netcdf_error(err) };

        err = _Type::func(
            handle,
            info.value().index,
            start.data(),
            count.data(),
            promise.template data<0>(),
            promise.requests()
        );
        if (err != NC_NOERR) return { netcdf_error(err) };

        return promise;
    }
    // Need to utilize the macro here (how to deal with that comma...)
    template const promise<io::access::ro, types::Double> file<io::access::ro>::get_variable_values<types::Double>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Float> file<io::access::ro>::get_variable_values<types::Float>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Int> file<io::access::ro>::get_variable_values<types::Int>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Char> file<io::access::ro>::get_variable_values<types::Char>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    
    template const promise<io::access::ro, types::Double> file<io::access::rw>::get_variable_values<types::Double>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Float> file<io::access::rw>::get_variable_values<types::Float>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Int> file<io::access::rw>::get_variable_values<types::Int>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;
    template const promise<io::access::ro, types::Char> file<io::access::rw>::get_variable_values<types::Char>(const std::string&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&) const;

    template<io::access _Access>
    template<typename>
    result<dimension>
    file<_Access>::get_dimension(int id) const
    {
        dimension dim{0};
        dim.id = id;

        char _name[MAX_NAME_LENGTH];
        const auto err = ncmpi_inq_dim(handle, id, _name, &dim.length);
        if (err != NC_NOERR) return { netcdf_error(err) };

        dim.name = std::string(_name);

        return { std::move(dim) };
    }
    
    template<io::access _Access>
    template<typename>
    result<dimension>
    file<_Access>::get_dimension(const std::string& name) const
    {
        int id = 0;
        const auto err = ncmpi_inq_dimid(handle, name.c_str(), &id);
        if (err != NC_NOERR) return { netcdf_error(err) };

        return get_dimension(id);
    }

#pragma endregion READ
    
#pragma region WRITE

    template<io::access _Access>
    template<typename _Type, typename>
    const promise<io::access::wo, _Type>
    file<_Access>::write_variable(
        const std::string& name,
        const typename _Type::integral_type* data,
        const std::size_t& size,
        const std::vector<MPI_Offset>& offset,
        const std::vector<MPI_Offset>& count)
    {
        if (!data || !size) return { error_code::NullData };
        if (offset.size() != count.size()) return { error_code::DimensionSizeMismatch };
        
        // data_size is equivalent to volume of data
        const auto product = std::accumulate(count.begin(), count.end(), 1, std::multiplies<size_t>());
        if (product != size) return { error_code::SizeMismatch };
        
        variable var;
        if constexpr (_Access == io::access::rw)
        {
            const auto info = get_variable_info(name);
            if (!info) { return { info.error() }; };
            var = std::move(info.value());
        }
        else return { 5 };

        if (_Type::nc != var.type) return { error_code::TypeMismatch };
        if (var.dimensions.size() != offset.size()) return { error_code::DimensionSizeMismatch };

        // need to find clever way to *not* require that counts array for this
        // type of promise
        promise<io::access::wo, _Type> promise(get_handle(), { 0 });

        auto err = ncmpi_begin_indep_data(get_handle());
        if (err != NC_NOERR) return { netcdf_error(err) };

        err = ncmpi_iput_vara(
            handle,
            var.index,
            offset.data(),
            count.data(),
            data,
            size,
            MPI_DATATYPE_NULL,
            promise.requests()
        );
        if (err != NC_NOERR) return { netcdf_error(err) };

        return promise;
    }
    // Need to utilize the macro here (how to deal with that comma...)
    template const promise<io::access::wo, types::Double> file<io::access::wo>::write_variable<types::Double>(const std::string&, const double*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Float> file<io::access::wo>::write_variable<types::Float>(const std::string&, const float*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Int> file<io::access::wo>::write_variable<types::Int>(const std::string&, const int*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Char> file<io::access::wo>::write_variable<types::Char>(const std::string&, const char*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    
    template const promise<io::access::wo, types::Double> file<io::access::rw>::write_variable<types::Double>(const std::string&, const double*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Float> file<io::access::rw>::write_variable<types::Float>(const std::string&, const float*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Int> file<io::access::rw>::write_variable<types::Int>(const std::string&, const int*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);
    template const promise<io::access::wo, types::Char> file<io::access::rw>::write_variable<types::Char>(const std::string&, const char*, const std::size_t&, const std::vector<MPI_Offset>&, const std::vector<MPI_Offset>&);

#pragma endregion WRITE

    template struct file<io::access::ro>;
    template struct file<io::access::wo>;
    template struct file<io::access::rw>;
}