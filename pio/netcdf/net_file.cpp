#include "net_file.hh"

#include <iostream>
#include <numeric>

#define FWD_DEC_WRITE_O(ret, name, acc, ...) template ret file<io::access::acc>::name(__VA_ARGS__)
#define FWD_DEC_READ_O(ret, name, access, ...) FWD_DEC_WRITE_O(ret, name, access, __VA_ARGS__) const

#define FWD_DEC_WRITE(ret, name, ...) FWD_DEC_WRITE_O(ret, name, wo, __VA_ARGS__); FWD_DEC_WRITE_O(ret, name, rw, __VA_ARGS__)
#define FWD_DEC_READ(ret, name, ...) FWD_DEC_READ_O(ret, name, ro, __VA_ARGS__); FWD_DEC_READ_O(ret, name, rw, __VA_ARGS__)

#define NET_CHECK(res) { const auto err = res; if (err != NC_NOERR) return { pio::netcdf::netcdf_error(err) }; }

namespace pio::netcdf
{

error_code::error_code(code c) :
    _netcdf_error(false),
    _code(c)
{   }

error_code::error_code(int c) :
    _netcdf_error(true),
    _netcdf(c)
{   }

std::string
error_code::message() const
{
    switch (_netcdf_error)
    {
    case true:  return "Netcdf error: " + std::string(ncmpi_strerror(_netcdf));
    case false: return "PIO error: " + _to_string(_code);
    }
}

std::string error_code::_to_string(code c)
{
    switch (c)
    {
    case TypeMismatch:          return "Type Mismatch";
    case SizeMismatch:          return "Size Mismatch";
    case DimensionSizeMismatch: return "dimension size mismatch";
    case DimensionDoesntExist:  return "given dimension name doesn't exist";
    case NullData:              return "Data Pointer is Null";
    case NullFile:              return "File reference is corrupted";
    case VariableDoesntExist:   return "Requested variable name doesn't exist";
    case FailedTaskCreation:    return "Failed to create tasks";
    default: return "";
    }
}

/* EXODUS FILE PIO IMPLEMENTATION */

template<io::access _Access>
file<_Access>::exodus_file::exodus_file(file* base_file) :
    _file(base_file)
{   }

// need to make more specific errors
template<io::access _Access>
template<typename>
result<std::vector<std::string>>
file<_Access>::exodus_file::get_variables() const
{
    if (!_file) return { error_code::NullFile };

    // make sure name_elem_var exists
    if (!([&]() -> bool
    {
        const auto res = _file->variable_names();
        if (!res) return false;
        const auto& var_names = res.value();

        const auto it = std::find(var_names.begin(), var_names.end(), "name_elem_var");
        if (it == var_names.end()) return false;

        return true;
    }())) return { error_code::VariableDoesntExist };

    const auto res = _file->get_variable_info("name_elem_var");
    if (!res) return { res.error() };
    const auto& var_info = res.value();

    // The dimensions need to be correct, it needs to be num_elem_var x len_name
    if (var_info.dimensions.size() != 2) return { error_code::DimensionSizeMismatch };
    const auto it_len_name     = std::find_if(var_info.dimensions.begin(), var_info.dimensions.end(), [](const auto& dim) { return dim.name == "len_name"; });
    const auto it_num_elem_var = std::find_if(var_info.dimensions.begin(), var_info.dimensions.end(), [](const auto& dim) { return dim.name == "num_elem_var"; });
    if (it_len_name     == var_info.dimensions.end() || 
        it_num_elem_var == var_info.dimensions.end())
            return { error_code::DimensionSizeMismatch };

    // Read the values from the file
    const auto& [var_count, len_name] = std::tie(it_num_elem_var->length, it_len_name->length);
    const auto var = _file->get_variable_values<types::Char>("name_elem_var", { 0, 0 }, { var_count, len_name });
    if (!var) return { var.error() };
    const auto stat = var.wait();
    return { netcdf::format(var.template get_data<0>(), var_count, len_name) };
}
FWD_DEC_READ(result<std::vector<std::string>>, exodus_file::get_variables);

using coord_values = std::unordered_map<std::string, std::vector<double>>;
template<io::access _Access>
template<typename>
result<coord_values>
file<_Access>::exodus_file::get_node_coordinates(bool get_data) const
{
    if (!_file) return { error_code::NullFile };
    const auto info = _file->get_dimension_lengths();
    if (!info) return { info.error() };

    const auto str_len_name = [&]() -> std::string
    {
        if (info->count("len_name")) return "len_name";
        if (info->count("len_string")) return "len_string";
        return "";
    }();

    if (!info->count("num_dim") || !str_len_name.size()) return { error_code::DimensionDoesntExist };
    
    // get dimension of file
    const auto& dim = info->at("num_dim");
    const auto& len_name = info->at(str_len_name);

    // read in coor_names
    const auto promise = _file->get_variable_values<types::Char>("coor_names", { 0, 0 }, { dim, len_name });
    if (!promise) return { promise.error() };
    promise.wait();
    const auto names = format(promise.template get_data<0>(), dim, len_name);

    coord_values values;
    for (const auto& name : names) values[name];

    if (!get_data) return { std::move(values) };

    // check cdf variables for coord (if it's the old format)
    const auto cdf_vars = _file->variable_names();
    if (!cdf_vars) return { cdf_vars.error() };
    const auto it = std::find(cdf_vars->begin(), cdf_vars->end(), "coord");
    const bool old = (it != cdf_vars->end());

    if (!info->count("num_nodes")) return { error_code::DimensionDoesntExist };
    const auto& num_nodes = info->at("num_nodes");

    if (old)
    {
        // read from variable called coord and split data up        
        const auto value_promise = _file->get_variable_values<types::Double>("coord", { 0, 0 }, { dim, num_nodes });
        if (!value_promise) return { value_promise.error() };
        
        value_promise.wait();
        const auto data = value_promise.template get_data<0>();

        for (uint32_t i = 0; i < names.size(); i++)
            std::copy(data.begin() + i * num_nodes, data.begin() + (i + 1) * num_nodes, std::back_inserter(values.at(names[i])));
    }
    else
    {
        // need to make sure they exist
        for (const auto& name : names)
        {
            if (std::find(cdf_vars->begin(), cdf_vars->end(), "coord" + name) == cdf_vars->end())
                return { error_code::VariableDoesntExist };

            const auto value_promise = _file->get_variable_values<types::Double>("coord" + name, { 0 }, { num_nodes });
            if (!value_promise) return { value_promise.error() };
            
            value_promise.wait();
            const auto data = value_promise.template get_data<0>();

            std::copy(data.begin(), data.end(), std::back_inserter(values.at(name)));
        }
    }

    return { std::move(values) };
}
FWD_DEC_READ(result<coord_values>, exodus_file::get_node_coordinates, bool);

using P = promise<io::access::wo, types::Double>;
template<io::access _Access>
template<typename>
result<std::vector<std::shared_ptr<const P>>>
file<_Access>::exodus_file::write_node_coordinates(
    MPI_Comm comm, 
    const coord_values& data)
{
    if (!_file) return { error_code::NullFile };

    // the list needs to be the same for all processes, so we need to sort to
    // get over the "unordered" part of the map
    const auto names = [&]()
    { 
        std::vector<std::string> r;
        r.reserve(data.size());
        for (const auto& p : data) r.push_back(p.first);
        std::sort(r.begin(), r.end());
        return r;
    }();
    
    io::distributor dist(comm);
    for (uint32_t i = 0; i < names.size(); i++)
    {
        io::distributor::volume volume{0};
        volume.data_index = i;
        volume.data_type = NC_DOUBLE;
        volume.dimensions.push_back(data.at(names[i]).size());
        dist.data_volumes.push_back(volume);
    }

    const auto subvols = dist.get_tasks();
    if (!subvols) return { error_code::FailedTaskCreation };

    std::vector<std::shared_ptr<const P>> promises;
    for (const auto& subvol : *subvols)
    {
        const auto& coord_name = names[dist.data_volumes[subvol.volume_index].data_index];
        const auto& coords = data.at(coord_name);

        const auto promise = _file->write_variable<types::Double>("coord" + coord_name, &coords[0] + subvol.offsets[0], subvol.counts[0], subvol.offsets, subvol.counts);
        promises.push_back(std::make_shared<const P>(promise));
    }

    return { std::move(promises) };
}
FWD_DEC_WRITE(result<std::vector<std::shared_ptr<const P>>>, exodus_file::write_node_coordinates, MPI_Comm, const coord_values&);

/* NETCDF FILE IMPLEMENTATION */

template<io::access _Access>
file<_Access>::file(const std::string& filename) :
    exodus(this)
{
    if constexpr (_Access == io::access::ro)
    {
        err = ncmpi_open(
            MPI_COMM_WORLD, 
            filename.c_str(),
            NC_NOWRITE,
            MPI_INFO_NULL,
            &handle
        );
    }

    if constexpr (_Access == io::access::wo || _Access == io::access::rw)
    {
        err = ncmpi_create(
            MPI_COMM_WORLD, 
            filename.c_str(),
            (_Access == io::access::rw ? NC_NOCLOBBER : NC_CLOBBER) | NC_WRITE | NC_64BIT_OFFSET,
            MPI_INFO_NULL,
            &handle
        );

        if (err == -35) // file exists (should only happen in rw)
        {
            err = ncmpi_open(
                MPI_COMM_WORLD, 
                filename.c_str(),
                NC_NOCLOBBER | NC_WRITE | NC_64BIT_OFFSET,
                MPI_INFO_NULL,
                &handle
            );
        }
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
        char buffer[MAX_STR_LENGTH];
        memset(buffer, 0, MAX_STR_LENGTH);
        ncmpi_inq_varname(handle, i, buffer);
        for (uint32_t j = 0; j < MAX_STR_LENGTH; j++)
            if (buffer[j]) ret[i] += buffer[j];
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
        memset(name, 0, MAX_NAME_LENGTH);
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
    memset(_name, 0, MAX_NAME_LENGTH);
    const auto err = ncmpi_inq_dim(handle, id, _name, &dim.length);
    if (err != NC_NOERR) return { netcdf_error(err) };

    for (uint32_t i = 0; i < MAX_NAME_LENGTH; i++)
        if (_name[i]) dim.name += _name[i];

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
result<void>
file<_Access>::define_variable(const std::string& name, const std::vector<std::string>& dim_names)
{
    std::vector<int> dimensions;
    for (const auto& d_name : dim_names)
    {
        const auto res = get_dimension(d_name);
        if (!res) return { error_code::DimensionDoesntExist };
        dimensions.push_back(res.value().id);
    }

    int var_id;
    NET_CHECK(ncmpi_def_var(handle, name.c_str(), _Type::nc, dimensions.size(), dimensions.data(), &var_id));
    return { };
}
template result<void> file<io::access::wo>::define_variable<types::Double>(const std::string&, const std::vector<std::string>&); // since this uses a getter maybe we only should decalre for r/w
template result<void> file<io::access::wo>::define_variable<types::Float>(const std::string&, const std::vector<std::string>&);
template result<void> file<io::access::wo>::define_variable<types::Int>(const std::string&, const std::vector<std::string>&);
template result<void> file<io::access::wo>::define_variable<types::Char>(const std::string&, const std::vector<std::string>&);

template result<void> file<io::access::rw>::define_variable<types::Double>(const std::string&, const std::vector<std::string>&);
template result<void> file<io::access::rw>::define_variable<types::Float>(const std::string&, const std::vector<std::string>&);
template result<void> file<io::access::rw>::define_variable<types::Int>(const std::string&, const std::vector<std::string>&);
template result<void> file<io::access::rw>::define_variable<types::Char>(const std::string&, const std::vector<std::string>&);

template<io::access _Access>
template<typename>
result<void>
file<_Access>::define(std::function<result<void>()> function)
{
    NET_CHECK(ncmpi_redef(handle));

    const auto res = function();

    NET_CHECK(ncmpi_enddef(handle));

    if (!res) return { res.error() };
    return { };
}
FWD_DEC_WRITE(result<void>, define, std::function<result<void>()>);

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
    if (size != product) return { error_code::SizeMismatch };
    
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

    NET_CHECK(ncmpi_iput_vara(
        handle,
        var.index,
        offset.data(),
        count.data(),
        data,
        size,
        MPI_DATATYPE_NULL,
        promise.requests()
    ));

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