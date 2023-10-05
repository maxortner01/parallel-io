#include "file.hh"

#define FWD_DEC_WRITE_O(ret, args, name, acc) template ret file<io::access::acc>::name(args)
#define FWD_DEC_READ_O(ret, args, name, access) FWD_DEC_WRITE_O(ret, args, name, access) const

#define FWD_DEC_WRITE(ret, args, name) FWD_DEC_WRITE_O(ret, args, name, wo); FWD_DEC_WRITE_O(ret, args, name, rw)
#define FWD_DEC_READ(ret, args, name) FWD_DEC_READ_O(ret, args, name, ro); FWD_DEC_READ_O(ret, args, name, rw)

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
    FWD_DEC_READ(io::result<std::vector<std::string>>, , variable_names);

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
    FWD_DEC_READ(io::result<variable>, const std::string&, get_variable_info);

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

        const auto dim_sizes = get_dimension_lengths().value();

        uint32_t i = 0;
        for (const auto& p : dim_sizes)
        {
            if (i == 0)
            {
                ret.size = p.second;
                i++;
                continue;
            }

            ret.size *= p.second;
        }

        return { ret };
    }
    FWD_DEC_READ(io::result<value_info>, const std::string&, get_variable_value_info);

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
    FWD_DEC_READ(io::result<map_type>, , get_dimension_lengths);

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
    FWD_DEC_READ(io::result<info>, , inquire);

    template<io::access _Access>
    template<typename _Type, typename>
    const io::promise<_Type>
    file<_Access>::get_variable_values(const std::string& name) const
    {

    }
    /*
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
    }*/
    FWD_DEC_READ(const io::promise<io::Type<NC_CHAR>>, const std::string&, get_variable_values);

    template struct file<io::access::ro>;
    template struct file<io::access::wo>;
    template struct file<io::access::rw>;
}