#include "ex_file.hh"

#include "../external.hh"

#define FWD_DEC_WRITE_O(word, ret, name, acc, ...) template ret file<word, io::access::acc>::name(__VA_ARGS__)
#define FWD_DEC_READ_O(word, ret, name, access, ...) FWD_DEC_WRITE_O(word, ret, name, access, __VA_ARGS__) const

#define FWD_DEC_WRITE(word, ret, name, ...) FWD_DEC_WRITE_O(word, ret, name, wo, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, rw, __VA_ARGS__)
#define FWD_DEC_READ(word, ret, name, ...) FWD_DEC_READ_O(word, ret, name, ro, __VA_ARGS__); FWD_DEC_READ_O(word, ret, name, rw, __VA_ARGS__)
#define FWD_DEC_ALL(word, ret, name, ...) FWD_DEC_WRITE_O(word, ret, name, ro, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, rw, __VA_ARGS__); FWD_DEC_WRITE_O(word, ret, name, wo, __VA_ARGS__)

namespace pio::exodus
{

error_code::error_code(code c) :
    _code(c)
{   }

error_code::error_code(int c) :
    _exodus(c)
{   }

std::string
error_code::message() const
{
    switch(_exodus_error)
    {
    case true:  return "Exodus error: " + std::string(ex_strerror(_exodus));
    case false: return "PIO error: " + _to_string(_code);
    }
}

std::string
error_code::_to_string(code c)
{
    switch (c)
    {
    case code::VarNotPresent:               return "requested variable name not present";
    case code::InquireError:                return "inquiry error";
    case code::FileNotGood:                 return "file not good";
    case code::TimeStepError:               return "error with time step count";
    case code::TimeStepNotPresent:          return "requested time step does not exist";
    case code::TimeStepIndexOutOfBounds:    return "time step indices start at 1";
    case code::VariableIndexOutOfBounds:    return "variable indices start at 1";
    default: return "Error error";
    }
}

template<typename _Word, io::access _Access>
file<_Word, _Access>::file(const std::string& filename)
{
    int comp_ws = sizeof(_Word);
    int io_ws = sizeof(_Word);
    float version;
    _handle = ex_open(filename.c_str(), (_Access == io::access::ro?EX_READ:EX_WRITE) | EX_LARGE_MODEL, &comp_ws, &io_ws, &version); 

    if constexpr (_Access == io::access::wo)
        if (_handle < 0) _handle = ex_create(filename.c_str(), EX_WRITE, &comp_ws, &io_ws);

    _good = (_handle < 0?false:true);
}
FWD_DEC_ALL(unsigned long, , file, const std::string&);

template<typename _Word, io::access _Access>
file<_Word, _Access>::file(file&& f) :
    _handle(f._handle)
{
    f._handle = 0;
}
FWD_DEC_ALL(unsigned long, , file, file&&);

template<typename _Word, io::access _Access>
file<_Word, _Access>::~file()
{
    close();
}
FWD_DEC_ALL(unsigned long, , ~file);

template<typename _Word, io::access _Access>
void file<_Word, _Access>::close()
{
    if (_handle) // need to verify _handle = 0 is not a valid id
    {
        ex_close(_handle);
        _handle = 0;
    }
}
FWD_DEC_ALL(unsigned long, void, close);

#pragma region WRITE

template<typename _Word, io::access _Access>
template<typename>
result<void> 
file<_Word, _Access>::set_init_params(const info& info)
{
    if (!good()) return { error_code::FileNotGood };

    const auto err = ex_put_init(
        _handle, 
        info.title.c_str(), 
        info.num_dim, 
        info.num_nodes, 
        info.num_elem,
        info.num_elem_blk,
        info.num_node_sets,
        info.num_side_sets
        );

    if (err < 0) return { exodus_error(err) };
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, set_init_params, const info&);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::write_time_step(_Word value)
{
    if (!good()) return { error_code::FileNotGood };

    const auto err = ex_put_time(_handle, _time_steps++, &value);

    if (err < 0) return { exodus_error(err) };
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, write_time_step, unsigned long);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::create_block(const Block<_Word>& block)
{
    const auto err = ex_put_block(
        _handle, 
        EX_ELEM_BLOCK,
        _block_counter++,
        block.type.c_str(),
        block.elements,
        block.nodes_per_elem,
        0, 0,
        block.attributes
    );

    if (err < 0) return { exodus_error(err) };
    return { };
}
FWD_DEC_WRITE(unsigned long, result<void>, create_block, const Block<unsigned long>&);

#pragma endregion WRITE


#pragma region READ

template<typename _Word, io::access _Access>
template<typename>
result<info>
file<_Word, _Access>::get_info() const
{
    info i;
    char title[MAX_LINE_LENGTH];
    const auto err = ex_get_init(
        _handle,
        title,
        &i.num_dim,
        &i.num_nodes,
        &i.num_elem,
        &i.num_elem_blk,
        &i.num_node_sets,
        &i.num_side_sets
    );

    if (err < 0) return { exodus_error(err) };
    i.title = std::string(title);
    return { std::move(i) };
}
FWD_DEC_READ(unsigned long, result<info>, get_info);

template<typename _Word, io::access _Access>
template<typename>
result<coordinates<_Word>>
file<_Word, _Access>::get_node_coordinates() const
{
    const auto info = get_info();
    if (!info) return { info.error() };

    coordinates<_Word> c;

    const auto& [dim, nodes] = 
        std::make_pair(info.value().num_dim, info.value().num_nodes);
    if (dim >= 1) c.x.resize(nodes);
    if (dim >= 2) c.y.resize(nodes);
    if (dim == 3) c.z.resize(nodes);

    const auto err = ex_get_coord(_handle, &c.x[0], &c.y[0], &c.z[0]);
    if (err < 0) return { exodus_error(err) };
    return { std::move(c) };
}
FWD_DEC_READ(unsigned long, result<coordinates<unsigned long>>, get_node_coordinates);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<_Word>>
file<_Word, _Access>::get_time_values() const
{
    const auto value_count = ex_inquire_int(_handle, EX_INQ_TIME);
    if (value_count < 0) return { error_code::InquireError };
    if (!value_count)    return { std::vector<_Word>() };

    std::vector<_Word> time_values(value_count);
    const auto err = ex_get_all_times(_handle, &time_values[0]);
    if (err < 0) return { exodus_error(err) };

    return { std::move(time_values) };
}
FWD_DEC_READ(unsigned long, result<std::vector<unsigned long>>, get_time_values);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<std::string>>
file<_Word, _Access>::get_variable_names(const scope& _scope) const
{
    const char* c = [&]()
    {
        switch (_scope)
        {
        case scope::element: return "e";
        case scope::global:  return "g";
        default: return "n";
        }
    }();

    int num_vars;
    {
        const auto err = ex_get_var_param(
            _handle, c, &num_vars
        );
        if (err < 0) return { exodus_error(err) };
    }

    if (!num_vars) return { std::vector<std::string>() };
    std::vector<std::string> variable_names(num_vars);
    for (auto& v : variable_names) v.resize(MAX_STR_LENGTH + 1);

    const auto err = ex_get_var_names(
        _handle, c, num_vars, (char**)&variable_names[0]
    );
    if (err < 0) return { exodus_error(err) };

    // Possibly make variable names a char[][] and then construct the vector
    // using the std::string constructor on each char[]
    for (auto& v : variable_names) v.shrink_to_fit();
    return { std::move(variable_names) };
}
FWD_DEC_READ(unsigned long, result<std::vector<std::string>>, get_variable_names, const scope&);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<Block<_Word>>>
file<_Word, _Access>::get_blocks() const
{
   const auto res = get_info();
   if (!res) return { res.error() } ;

    std::vector<int> ids(res.value().num_elem_blk);
    {
        const auto err = ex_get_ids(_handle, EX_ELEM_BLOCK, ids.data());
        if (err < 0) return { exodus_error(err) };
    }

    std::vector<Block<_Word>> blocks(ids.size());
    for (uint32_t i = 0; i < blocks.size(); i++)
    {
        ex_block block{0};
        char type[MAX_STR_LENGTH];
        const auto err = ex_get_block(
            _handle, 
            EX_ELEM_BLOCK,
            ids[i],
            type,
            &blocks[i].elements,
            &blocks[i].nodes_per_elem,
            &block.num_edges_per_entry,
            &block.num_faces_per_entry,
            &blocks[i].attributes
        );
        if (err < 0) return { exodus_error(err) };

        blocks[i].id = ids[i];
        blocks[i].type = std::string(type);
    }
    return { std::move(blocks) };
}
FWD_DEC_READ(unsigned long, result<std::vector<Block<unsigned long>>>, get_blocks);

template<typename _Word, io::access _Access>
template<typename>
result<void>
file<_Word, _Access>::get_block_data(
    const std::string& name, 
    uint32_t time_step, 
    Block<_Word>& block) const
{
    const auto var_names_res = get_variable_names(scope::element);
    if (!var_names_res) return { var_names_res.error() };

    const auto& var_names = var_names_res.value();

    int index = -1;
    for (int i = 0; i < var_names.size(); i++)
        if (var_names[i] == name)
        {
            index = i;
            break;
        }
    
    if (index == -1) return { error_code::VarNotPresent };

    // If there is no data at all, construct the map
    if (!block.data.has_value()) block.data.emplace();
    auto& map = block.data.value();

    // If the map is missing the variable name construct the vector
    // otherwise reset the memory
    if (!map.count(name)) map.insert(std::pair(name, std::vector<_Word>()));
    else map.at(name).clear();

    auto& vec = map.at(name);
    const auto err = ex_get_var(_handle, time_step, EX_ELEM_BLOCK, index, block.id, block.elements, vec.data());
    if (err < 0) return { exodus_error(err) };
    return { };
}
FWD_DEC_READ(unsigned long, result<void>, get_block_data, const std::string&, uint32_t, Block<unsigned long>&);

template<typename _Word, io::access _Access>
template<typename>
result<std::vector<std::vector<_Word>>>
file<_Word, _Access>::get_element_variable_values(
    const uint32_t time_step, 
    const uint32_t var_ind) const
{
    const auto time_step_res = get_time_values();
    if (!time_step_res) return { time_step_res.error() };
    const auto& time_steps = time_step_res.value();

    if (time_step > time_steps.size()) return { error_code::TimeStepNotPresent };
    if (!time_step) return { error_code::TimeStepIndexOutOfBounds };
    if (!var_ind)   return { error_code::VariableIndexOutOfBounds };

    const auto res = get_info();
    if (!res) return { res.error() };

    const auto& info = res.value();

    std::vector<std::vector<_Word>> ret;
    if (!info.num_elem_blk) return { std::move(ret) };

    ret.resize(info.num_elem_blk);

    std::vector<int> ids(info.num_elem_blk);
    auto error = ex_get_elem_blk_ids(_handle, ids.data());
    if (error < 0) return { exodus_error(error) };

    for (uint32_t i = 0; i < info.num_elem_blk; i++)
    {
        char elem_type[MAX_STR_LENGTH];
        int num_elem_this_blk, num_nodes_per_elem, num_attr;

        error = ex_get_elem_block(_handle, ids[i], elem_type, &num_elem_this_blk, &num_nodes_per_elem, &num_attr);
        if (error < 0) return { exodus_error(error) };

        if (!num_elem_this_blk) continue;
        ret[i].resize(num_elem_this_blk);
        error = ex_get_elem_var(_handle, time_step, var_ind, ids[i], num_elem_this_blk, ret[i].data());
        if (error < 0) return { exodus_error(error) };
    }

    return { std::move(ret) };
}
FWD_DEC_READ(unsigned long, result<std::vector<std::vector<unsigned long>>>, get_element_variable_values, const uint32_t, const uint32_t);

#pragma endregion READ

}